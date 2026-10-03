#pragma once
#include "AudioNode.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nam { class DSP; }
namespace dsp { class ImpulseResponse; }

// RigRoom's own capture block: a NAM model and/or a cab IR, without a plugin.
// Mono in -> input gain -> NAM -> IR -> output gain -> mono out, the same
// layout as the NAM LV2 plugin so it slots into a chain the same way.
//
// Presets store it as an LV2 node with the URI kUri and the files as file
// properties, so a build without this block keeps them (MissingPluginNode).
//
// Threads: load*() and prepare() run on the GUI thread (or a worker for
// load*()). They build a complete processing chain and publish it with one
// atomic pointer swap; the audio thread never allocates, locks or frees.
class CaptureNode : public AudioNode {
public:
    static constexpr const char* kUri = "builtin:capture";
    static constexpr const char* kModelProperty = "builtin:capture#model";
    static constexpr const char* kIrProperty = "builtin:capture#ir";
    enum Param : uint32_t { InputGainDb = 0, OutputGainDb = 1, CabEnabled = 2, IrMix = 3 };

    // What the IR slot holds, for display (the model's details live in
    // AudioNode::ModelMetadata). Saved with presets through saveState().
    struct IrInfo {
        std::string name;
        std::string imageUrl;
        std::string gearType; // cab, pedal, outboard, space...
        std::string sourceUrl;
    };

    CaptureNode();
    ~CaptureNode() override;

    // The capture's name, else the IR's, so the chain shows what is loaded.
    std::string getName() const override;
    std::string getPluginURI() const override { return kUri; }
    // Saved as LV2 so older builds fall back to a passthrough that keeps the data.
    NodeType getType() const override { return NodeType::LV2Plugin; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;

    // Empty path clears that stage. False (and the old stage kept) on failure.
    bool loadModel(const std::string& path, std::string* error = nullptr);
    bool loadIr(const std::string& path, std::string* error = nullptr);
    const std::string& modelPath() const { return m_modelPath; }
    const std::string& irPath() const { return m_irPath; }

    // The model's own sample rate (0 if none) and whether it is being resampled.
    double modelSampleRate() const;
    bool modelIsResampled() const;
    // Loudness the model reports (dB), if any.
    bool modelLoudness(double& db) const;

    std::vector<FileProperty> getFileProperties() const override;
    void setFileProperty(const std::string& uri, const std::string& path) override;
    void loadModelFile(const std::string& path) override { loadModel(path); }
    const std::string& getModelFilePath() const override { return m_modelPath; }

    const IrInfo& irInfo() const { return m_irInfo; }
    void setIrInfo(const IrInfo& info) { m_irInfo = info; }
    std::string saveState() override;
    bool restoreState(const std::string& state) override;

    // Processing stages (defined in the .cpp).
    struct Model;
    struct Ir;
    struct Chain;

private:

    // Builds a chain from the current model/IR for the current rate and
    // publishes it; frees chains the audio thread no longer uses.
    void publish(std::shared_ptr<Model> model, std::shared_ptr<Ir> ir);
    void collectGarbage();
    float controlValue(uint32_t index) const;

    double m_sampleRate = 48000.0;
    int m_maxBlockSize = 512;
    std::string m_modelPath;
    std::string m_irPath;
    std::vector<float> m_inBuffer;
    std::vector<float> m_outBuffer;
    std::vector<float> m_scratch;
    std::vector<float> m_dry;
    IrInfo m_irInfo;

    std::mutex m_guiMutex; // GUI/worker side only; never taken by the audio thread
    std::shared_ptr<Model> m_model;
    std::shared_ptr<Ir> m_ir;
    std::vector<std::pair<std::unique_ptr<Chain>, uint64_t>> m_retired; // chain, generation it was replaced at
    std::unique_ptr<Chain> m_owned;                                     // the published chain
    std::atomic<Chain*> m_active{nullptr};
    std::atomic<uint64_t> m_publishGeneration{0};
    std::atomic<uint64_t> m_audioGeneration{0};
};
