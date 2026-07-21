#pragma once
#include "AudioNode.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "clap/clap.h"

struct CLAPPluginDescriptor {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::string description;
    std::vector<std::string> features;
    std::string pluginPath;
    uint32_t pluginIndex = 0;
};

class CLAPPluginNode : public AudioNode {
public:
    CLAPPluginNode(const std::string& libraryPath, uint32_t pluginIndex = 0);
    ~CLAPPluginNode() override;

    std::string getName() const override { return m_name; }
    std::string getPluginURI() const override { return m_path; }
    NodeType getType() const override { return NodeType::CLAPPlugin; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;

    void setParameter(uint32_t index, float value);
    float getParameter(uint32_t index) const;

    bool hasGUI() const;
    const clap_plugin_t* getClapPlugin() const { return m_plugin; }
    const clap_plugin_gui_t* getClapGuiExtension() const { return m_extGui; }

    static std::vector<CLAPPluginDescriptor> scanLibrary(const std::string& path);
    static std::vector<CLAPPluginDescriptor> scanStandardPaths();

private:
    void initHostCallbacks();
    void setupPortsAndParams();

    std::string m_name;
    std::string m_path;
    uint32_t m_pluginIndex = 0;
    void* m_libHandle = nullptr;

    clap_host_t m_host{};
    const clap_plugin_t* m_plugin = nullptr;

    const clap_plugin_params_t* m_extParams = nullptr;
    const clap_plugin_audio_ports_t* m_extAudioPorts = nullptr;
    const clap_plugin_gui_t* m_extGui = nullptr;
    const clap_plugin_state_t* m_extState = nullptr;
    const clap_plugin_latency_t* m_extLatency = nullptr;

    double m_sampleRate = 48000.0;
    int m_maxBlockSize = 256;
    bool m_active = false;
    bool m_processing = false;
    uint64_t m_sampleCount = 0;

    std::vector<float> m_paramValues;
    std::vector<clap_id> m_paramIds;
    std::vector<std::vector<float>> m_audioBuffers;
    std::vector<std::vector<float>> m_audioInputBuffers;
    std::vector<std::vector<float>> m_audioOutputBuffers;

    // Queued parameter events for audio thread
    struct QueuedParamEvent {
        clap_event_param_value_t event;
    };
    std::mutex m_eventMutex;
    std::vector<QueuedParamEvent> m_queuedParamEvents;
};
