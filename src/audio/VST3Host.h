#pragma once
#include "AudioNode.h"
#include <atomic>
#include <string>
#include <vector>

#include <functional>

namespace Steinberg {
    class IPlugView;
}

class VST3PluginNode : public AudioNode {
public:
    VST3PluginNode(const std::string& path);
    ~VST3PluginNode() override;
    
    std::string getName() const override { return m_name; }
    std::string getPluginURI() const override { return m_path; }
    NodeType getType() const override { return NodeType::VST3Plugin; }
    bool isMissing() const override { return m_component == nullptr; }
    
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;
    
    Steinberg::IPlugView* getPlugView();
    void releasePlugView();
    bool hasEditor() const;
    // An edit made in the plugin's own GUI (IComponentHandler::performEdit).
    void onControllerEdit(uint32_t paramId, float normalized);
    // Push host-side parameter changes to the edit controller so the plugin
    // GUI follows them. Call from the GUI thread.
    // Returns true if any value was sent.
    bool syncControllerParams();
    // IComponentHandler::restartComponent; may arrive from any thread.
    void requestRestart(int32_t flags) { m_pendingRestartFlags.fetch_or(flags); }
    // Apply pending title/value changes reported by the plugin. GUI thread.
    // Returns true if parameter names changed.
    bool applyPendingRestart();
    // Set when the plugin reports a change of its own (setDirty, an edit in its
    // window, or values it changed itself); any thread. Read on the GUI thread.
    void markStateDirty() { m_stateDirty.store(true); }
    bool takeStateDirty() { return m_stateDirty.exchange(false); }
    void clearStateDirty() { m_stateDirty.store(false); }

    std::string saveState() override;
    bool restoreState(const std::string& state) override;
    void* getHostAppUnknown() const { return m_hostApp; }
    
private:
    std::string m_name;
    std::string m_path;
    void* m_libHandle = nullptr;
    Steinberg::IPlugView* m_plugView = nullptr;
    void* m_component = nullptr;
    void* m_controller = nullptr;
    void* m_processor = nullptr;
    void* m_paramChanges = nullptr;
    void* m_outputParamChanges = nullptr;
    void* m_hostApp = nullptr;
    void* m_compHandler = nullptr;
    std::vector<float> m_lastParamValues;
    std::vector<float> m_controllerParamValues; // last value the controller is known to have
    std::atomic<int32_t> m_pendingRestartFlags{0};
    std::atomic<bool> m_stateDirty{false};

    void readParamsFromController();
    std::vector<std::vector<float>> m_audioBuffers;
};
