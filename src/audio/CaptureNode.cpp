#include "CaptureNode.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "dsp/ImpulseResponse.h"
// The resampler comes from iPlug2 and expects two of its constants.
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 512
#endif
#include "dsp/ResamplingContainer/ResamplingContainer.h"
#include "json.hpp"
#include <cmath>
#include <filesystem>

namespace {
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
} // namespace

// A loaded NAM model, reset for one engine rate and block size. If the model
// was trained at another rate, it runs inside a resampler at its own rate.
struct CaptureNode::Model {
    std::unique_ptr<nam::DSP> dsp;
    std::unique_ptr<dsp::ResamplingContainer<NAM_SAMPLE, 1, 12>> resampler;
    double expectedRate = 0.0;
    bool hasLoudness = false;
    double loudness = 0.0;
    // Brings the model to NAM's -18 dB loudness target, as the NAM plugin does.
    float normalize = 1.0f;
};

// A cab IR, loaded and resampled to the engine rate. Processes in double.
struct CaptureNode::Ir {
    std::unique_ptr<dsp::ImpulseResponse> ir;
    std::vector<double> in;
};

// What the audio thread runs. Immutable once published, apart from the DSP
// state inside the model and IR.
struct CaptureNode::Chain {
    std::shared_ptr<Model> model;
    std::shared_ptr<Ir> ir;
};

namespace {
std::shared_ptr<CaptureNode::Model> loadModelFor(const std::string& path, double rate, int maxBlock, std::string* error);
}

CaptureNode::CaptureNode() {
    m_ports = {
        {"In", true, false, 0, nullptr},
        {"Out", false, false, 0, nullptr},
    };
    m_controlPorts = {
        {"Input", InputGainDb, 0.0f, -24.0f, 24.0f, 0.0f, false},
        {"Output", OutputGainDb, 0.0f, -40.0f, 24.0f, 0.0f, false},
        {"Cab", CabEnabled, 1.0f, 0.0f, 1.0f, 1.0f, false, true},
        // Wet share of the IR stage; cabs run at 1, room / effect IRs lower.
        {"IR Mix", IrMix, 1.0f, 0.0f, 1.0f, 1.0f, false},
    };
}

CaptureNode::~CaptureNode() = default;

float CaptureNode::controlValue(uint32_t index) const {
    for (const auto& p : m_controlPorts) {
        if (p.index == index) return p.value;
    }
    return 0.0f;
}

namespace {
std::shared_ptr<CaptureNode::Model> loadModelFor(const std::string& path, double rate, int maxBlock, std::string* error) {
    auto model = std::make_shared<CaptureNode::Model>();
    try {
        model->dsp = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return nullptr;
    }
    if (!model->dsp) {
        if (error) *error = "Not a NAM model";
        return nullptr;
    }
    // Older models don't say; NAM's convention for them is 48 kHz.
    model->expectedRate = model->dsp->GetExpectedSampleRate();
    if (model->expectedRate <= 0.0) model->expectedRate = 48000.0;
    model->hasLoudness = model->dsp->HasLoudness();
    if (model->hasLoudness) {
        model->loudness = model->dsp->GetLoudness();
        model->normalize = std::pow(10.0f, static_cast<float>(-18.0 - model->loudness) / 20.0f);
    }

    const bool needsResampling = model->expectedRate > 0.0 && std::abs(model->expectedRate - rate) > 0.5
                                 && !model->dsp->SupportsArbitrarySampleRate();
    if (needsResampling) {
        model->resampler = std::make_unique<dsp::ResamplingContainer<NAM_SAMPLE, 1, 12>>(model->expectedRate);
        model->resampler->Reset(rate, maxBlock);
        // The resampler may hand the model somewhat more frames than one engine block.
        const int inner = static_cast<int>(std::ceil(maxBlock * model->expectedRate / rate)) + 64;
        model->dsp->Reset(model->expectedRate, inner);
    } else {
        model->dsp->Reset(rate > 0.0 ? rate : model->expectedRate, maxBlock);
    }
    return model;
}

std::shared_ptr<CaptureNode::Ir> loadIrFor(const std::string& path, double rate, int maxBlock, std::string* error) {
    auto ir = std::make_shared<CaptureNode::Ir>();
    try {
        ir->ir = std::make_unique<dsp::ImpulseResponse>(path.c_str(), rate);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return nullptr;
    }
    if (ir->ir->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS) {
        if (error) *error = "Could not read the IR (WAV files only)";
        return nullptr;
    }
    ir->in.assign(maxBlock, 0.0);
    // Size the IR's own buffers for this block size now, not on the audio thread.
    double* ptr = ir->in.data();
    ir->ir->Process(&ptr, 1, maxBlock);
    return ir;
}
} // namespace

void CaptureNode::prepare(double sampleRate, int maxBlockSize) {
    const bool changed = sampleRate != m_sampleRate || maxBlockSize != m_maxBlockSize;
    m_sampleRate = sampleRate;
    m_maxBlockSize = std::max(1, maxBlockSize);
    m_inBuffer.assign(m_maxBlockSize, 0.0f);
    m_outBuffer.assign(m_maxBlockSize, 0.0f);
    m_scratch.assign(m_maxBlockSize, 0.0f);
    m_dry.assign(m_maxBlockSize, 0.0f);
    m_ports[0].buffer = m_inBuffer.data();
    m_ports[1].buffer = m_outBuffer.data();

    // Rate or block size changed: rebuild from the files, as new objects, so
    // nothing the audio thread may still be using is touched.
    std::shared_ptr<Model> model;
    std::shared_ptr<Ir> ir;
    {
        std::lock_guard<std::mutex> lock(m_guiMutex);
        model = m_model;
        ir = m_ir;
    }
    if (changed || !m_active.load()) {
        if (!m_modelPath.empty()) model = loadModelFor(m_modelPath, m_sampleRate, m_maxBlockSize, nullptr);
        if (!m_irPath.empty()) ir = loadIrFor(m_irPath, m_sampleRate, m_maxBlockSize, nullptr);
    }
    publish(model, ir);
}

bool CaptureNode::loadModel(const std::string& path, std::string* error) {
    std::shared_ptr<Model> model;
    if (!path.empty()) {
        model = loadModelFor(path, m_sampleRate, m_maxBlockSize, error);
        if (!model) return false;
    }
    std::shared_ptr<Ir> ir;
    {
        std::lock_guard<std::mutex> lock(m_guiMutex);
        ir = m_ir;
        m_modelPath = path;
    }
    publish(model, ir);
    return true;
}

bool CaptureNode::loadIr(const std::string& path, std::string* error) {
    std::shared_ptr<Ir> ir;
    if (!path.empty()) {
        ir = loadIrFor(path, m_sampleRate, m_maxBlockSize, error);
        if (!ir) return false;
    }
    std::shared_ptr<Model> model;
    {
        std::lock_guard<std::mutex> lock(m_guiMutex);
        model = m_model;
        m_irPath = path;
    }
    publish(model, ir);
    return true;
}

void CaptureNode::publish(std::shared_ptr<Model> model, std::shared_ptr<Ir> ir) {
    std::lock_guard<std::mutex> lock(m_guiMutex);
    m_model = model;
    m_ir = ir;
    auto chain = std::make_unique<Chain>();
    chain->model = std::move(model);
    chain->ir = std::move(ir);

    Chain* raw = chain.get();
    m_active.store(raw, std::memory_order_release);
    const uint64_t generation = m_publishGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (m_owned) m_retired.emplace_back(std::move(m_owned), generation);
    m_owned = std::move(chain);
    collectGarbage();
}

void CaptureNode::collectGarbage() {
    // A chain replaced at generation G is free once the audio thread has
    // finished a block that started after G was published.
    const uint64_t seen = m_audioGeneration.load(std::memory_order_acquire);
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                   [seen](const auto& entry) { return entry.second <= seen; }),
                    m_retired.end());
}

void CaptureNode::process(int numFrames) {
    const uint64_t generation = m_publishGeneration.load(std::memory_order_acquire);
    Chain* chain = m_active.load(std::memory_order_acquire);
    float* in = m_ports[0].buffer;
    float* out = m_ports[1].buffer;
    if (!in || !out || numFrames > m_maxBlockSize) return;

    const float inGain = dbToGain(controlValue(InputGainDb));
    const float outGain = dbToGain(controlValue(OutputGainDb));
    const bool cabOn = controlValue(CabEnabled) >= 0.5f;

    for (int i = 0; i < numFrames; ++i) out[i] = in[i] * inGain;

    if (chain && chain->model && chain->model->dsp) {
        Model& model = *chain->model;
        NAM_SAMPLE* io[1] = {out};
        if (model.resampler) {
            nam::DSP* dsp = model.dsp.get();
            model.resampler->ProcessBlock(io, io, numFrames, [dsp](NAM_SAMPLE** a, NAM_SAMPLE** b, int n) {
                dsp->process(a, b, n);
            });
        } else {
            NAM_SAMPLE* scratch[1] = {m_scratch.data()};
            model.dsp->process(io, scratch, numFrames);
            std::copy(m_scratch.begin(), m_scratch.begin() + numFrames, out);
        }
        if (model.normalize != 1.0f) {
            for (int i = 0; i < numFrames; ++i) out[i] *= model.normalize;
        }
    }
    if (cabOn && chain && chain->ir && chain->ir->ir) {
        Ir& ir = *chain->ir;
        const float mix = std::clamp(controlValue(IrMix), 0.0f, 1.0f);
        for (int i = 0; i < numFrames; ++i) {
            ir.in[i] = out[i];
            m_dry[i] = out[i];
        }
        double* ptr = ir.in.data();
        double** result = ir.ir->Process(&ptr, 1, numFrames);
        for (int i = 0; i < numFrames; ++i) {
            out[i] = static_cast<float>(result[0][i]) * mix + m_dry[i] * (1.0f - mix);
        }
    }
    for (int i = 0; i < numFrames; ++i) out[i] *= outGain;

    m_audioGeneration.store(generation, std::memory_order_release);
}

double CaptureNode::modelSampleRate() const {
    auto* self = const_cast<CaptureNode*>(this);
    std::lock_guard<std::mutex> lock(self->m_guiMutex);
    return m_model ? m_model->expectedRate : 0.0;
}

bool CaptureNode::modelIsResampled() const {
    auto* self = const_cast<CaptureNode*>(this);
    std::lock_guard<std::mutex> lock(self->m_guiMutex);
    return m_model && m_model->resampler;
}

bool CaptureNode::modelLoudness(double& db) const {
    auto* self = const_cast<CaptureNode*>(this);
    std::lock_guard<std::mutex> lock(self->m_guiMutex);
    if (!m_model || !m_model->hasLoudness) return false;
    db = m_model->loudness;
    return true;
}

std::string CaptureNode::getName() const {
    if (!getModelDisplayName().empty() && !m_modelPath.empty()) return getModelDisplayName();
    if (!m_irInfo.name.empty() && !m_irPath.empty()) return m_irInfo.name;
    if (!m_irPath.empty()) return std::filesystem::path(m_irPath).stem().string();
    return "Capture";
}

std::string CaptureNode::saveState() {
    nlohmann::json state;
    state["irName"] = m_irInfo.name;
    state["irImageUrl"] = m_irInfo.imageUrl;
    state["irGear"] = m_irInfo.gearType;
    state["irSourceUrl"] = m_irInfo.sourceUrl;
    return state.dump();
}

bool CaptureNode::restoreState(const std::string& state) {
    try {
        const auto json = nlohmann::json::parse(state);
        m_irInfo.name = json.value("irName", "");
        m_irInfo.imageUrl = json.value("irImageUrl", "");
        m_irInfo.gearType = json.value("irGear", "");
        m_irInfo.sourceUrl = json.value("irSourceUrl", "");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<AudioNode::FileProperty> CaptureNode::getFileProperties() const {
    return {
        {kModelProperty, "Capture", m_modelPath},
        {kIrProperty, "Cab IR", m_irPath},
    };
}

void CaptureNode::setFileProperty(const std::string& uri, const std::string& path) {
    // Presets restore the model both as model_file_path and as a file property.
    if (uri == kModelProperty && path != m_modelPath) loadModel(path);
    else if (uri == kIrProperty && path != m_irPath) loadIr(path);
}
