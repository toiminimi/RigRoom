#include "CaptureNode.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "dsp/Resample.h"
#include <cstring>
#include <fstream>
#include "TwoStageFFTConvolver.h"
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

// An IR, loaded and resampled to the engine rate, convolved by partitioned
// FFT so long room / reverb IRs keep their whole tail.
struct CaptureNode::Ir {
    fftconvolver::TwoStageFFTConvolver convolver;
    size_t length = 0;
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

// Reads a WAV file of any channel count as mono (channels averaged): PCM
// 8/16/24/32-bit, float 32/64, plain or WAVE_EXTENSIBLE, extra fmt bytes and
// unknown chunks skipped. Integer scaling matches NAM's own reader, so cabs
// load to the same values.
bool readWavMono(const std::string& path, std::vector<float>& out, double& rate, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Could not open the file";
        return false;
    }
    auto u32 = [&]() { uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto u16 = [&]() { uint16_t v = 0; f.read(reinterpret_cast<char*>(&v), 2); return v; };
    char id[4];
    f.read(id, 4);
    u32();
    char wave[4];
    f.read(wave, 4);
    if (!f || std::memcmp(id, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
        error = "Not a WAV file";
        return false;
    }
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    bool haveFmt = false;
    while (f.read(id, 4)) {
        const uint32_t size = u32();
        const std::streampos next = f.tellg() + std::streamoff(size + (size & 1));
        if (std::memcmp(id, "fmt ", 4) == 0) {
            format = u16();
            channels = u16();
            sampleRate = u32();
            u32();
            u16();
            bits = u16();
            if (format == 0xFFFE && size >= 40) { // WAVE_EXTENSIBLE: the real format leads the sub-format GUID
                u16();
                u16();
                u32();
                format = u16();
            }
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!haveFmt || channels == 0 || sampleRate == 0) {
                error = "WAV file without a usable format";
                return false;
            }
            const int bytes = bits / 8;
            const bool isFloat = format == 3;
            if (!(format == 1 || isFloat) || bytes == 0 || (isFloat && bytes != 4 && bytes != 8)) {
                error = "Unsupported WAV sample format";
                return false;
            }
            std::vector<char> data(size);
            f.read(data.data(), size);
            const size_t frames = static_cast<size_t>(f.gcount()) / (bytes * channels);
            out.assign(frames, 0.0f);
            const char* p = data.data();
            for (size_t i = 0; i < frames; ++i) {
                double sum = 0.0;
                for (int c = 0; c < channels; ++c, p += bytes) {
                    double v = 0.0;
                    if (isFloat && bytes == 4) { float x; std::memcpy(&x, p, 4); v = x; }
                    else if (isFloat) { double x; std::memcpy(&x, p, 8); v = x; }
                    else if (bytes == 1) v = (static_cast<uint8_t>(*p) - 128) / 128.0;
                    else if (bytes == 2) { int16_t x; std::memcpy(&x, p, 2); v = x / 32768.0; }
                    else if (bytes == 3) {
                        int32_t x = (static_cast<uint8_t>(p[0])) | (static_cast<uint8_t>(p[1]) << 8) | (static_cast<int8_t>(p[2]) * 65536);
                        v = x / 8388608.0;
                    } else { int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.0; }
                    sum += v;
                }
                out[i] = static_cast<float>(sum / channels);
            }
            rate = sampleRate;
            return !out.empty();
        }
        f.seekg(next);
    }
    error = "WAV file without audio data";
    return false;
}

std::shared_ptr<CaptureNode::Ir> loadIrFor(const std::string& path, double rate, int maxBlock, std::string* error) {
    // Resampled as NAM's own IR stage does (AudioDSPTools), so a cab sounds the
    // same as in the NAM plugin. Stereo IRs (most rooms and reverbs) are mixed
    // to mono, as the block is mono.
    std::vector<float> raw;
    double rawRate = 0.0;
    std::string message;
    if (!readWavMono(path, raw, rawRate, message)) {
        if (error) *error = message;
        return nullptr;
    }
    std::vector<float> resampled;
    if (rawRate == rate) {
        resampled = std::move(raw);
    } else {
        std::vector<float> padded(raw.size() + 2, 0.0f);
        std::copy(raw.begin(), raw.end(), padded.begin() + 1);
        dsp::ResampleCubic<float>(padded, rawRate, rate, 0.0, resampled);
    }
    // Ten seconds is more than any room needs.
    resampled.resize(std::min(resampled.size(), static_cast<size_t>(rate * 10.0)));
    // NAM's trim: -18 dB, scaled with the rate so a resampled IR keeps its level.
    const float gain = static_cast<float>(std::pow(10.0, -18.0 * 0.05) * 48000.0 / rate);
    for (float& v : resampled) v *= gain;

    auto ir = std::make_shared<CaptureNode::Ir>();
    ir->length = resampled.size();
    size_t head = 64;
    while (head < static_cast<size_t>(maxBlock) && head < 1024) head *= 2;
    if (!ir->convolver.init(head, std::max<size_t>(head * 16, 4096), resampled.data(), resampled.size())) {
        if (error) *error = "Could not set up the IR";
        return nullptr;
    }
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
    if (cabOn && chain && chain->ir) {
        Ir& ir = *chain->ir;
        const float mix = std::clamp(controlValue(IrMix), 0.0f, 1.0f);
        std::copy(out, out + numFrames, m_dry.begin());
        ir.convolver.process(m_dry.data(), out, numFrames);
        if (mix < 1.0f) {
            for (int i = 0; i < numFrames; ++i) out[i] = out[i] * mix + m_dry[i] * (1.0f - mix);
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
