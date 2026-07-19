#pragma once
#include <jack/jack.h>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include "AudioNode.h"

struct AudioConnection {
    std::string srcNodeId;
    int srcPortIdx;
    std::string dstNodeId;
    int dstPortIdx;
    float gain = 1.0f;
    std::shared_ptr<std::atomic<float>> liveGain;
};

// System node wrapper for JACK inputs and outputs
class SystemAudioNode : public AudioNode {
public:
    SystemAudioNode(const std::string& name, bool isInput, int numChannels);
    ~SystemAudioNode() override = default;
    
    std::string getName() const override { return m_name; }
    NodeType getType() const override { return m_isInput ? NodeType::SystemInput : NodeType::SystemOutput; }
    
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;
    
private:
    std::string m_name;
    bool m_isInput;
    std::vector<std::vector<float>> m_localBuffers;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    
    bool init(const std::string& clientName);
    bool start();
    void stop();
    
    void addNode(std::shared_ptr<AudioNode> node);
    void removeNode(const std::string& nodeId);
    void connectPorts(const std::string& srcId, int srcPort, const std::string& dstId, int dstPort,
                      float gain = 1.0f, std::shared_ptr<std::atomic<float>> liveGain = nullptr);
    void disconnectPorts(const std::string& srcId, int srcPort, const std::string& dstId, int dstPort);
    void clearGraph();
    
    void setBufferSize(int size);
    int getBufferSize() const { return m_bufferSize; }
    double getSampleRate() const { return m_sampleRate; }
    float getCPULoad() const;
    
    std::vector<std::shared_ptr<AudioNode>> getNodes() const;
    std::vector<AudioConnection> getConnections() const;
    
    void setHardwareInputPorts(const std::string& left, const std::string& right, bool isStereo);
    void setHardwareOutputPorts(const std::string& left, const std::string& right, bool isStereo);
    std::string getHardwareInputLeft() const { return m_hwInputLeft; }
    std::string getHardwareInputRight() const { return m_hwInputRight; }
    std::string getHardwareOutputLeft() const { return m_hwOutputLeft; }
    std::string getHardwareOutputRight() const { return m_hwOutputRight; }
    bool isHardwareInputStereo() const { return m_hwInputStereo; }
    bool isHardwareOutputStereo() const { return m_hwOutputStereo; }
    void updateHardwareConnections();
    
    void setInputGain(float db);
    void setOutputGain(float db);
    float getInputGainDB() const;
    float getOutputGainDB() const;
    float getInputPeak() { return m_inputPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getOutputPeak() { return m_outputPeak.exchange(0.0f, std::memory_order_relaxed); }

    // Setup execution order topologically (safe to call from main thread, triggers atomic swap)
    void rebuildGraph();
    
    void suspendProcessing();
    void resumeProcessing();

    // Get list of standard jack physical ports
    std::vector<std::string> getPhysicalInputs() const;
    std::vector<std::string> getPhysicalOutputs() const;

private:
    static int processCallback(jack_nframes_t nframes, void* arg);
    static void shutdownCallback(void* arg);
    static int bufferSizeCallback(jack_nframes_t nframes, void* arg);
    
    void processAudio(int numFrames);
    void topologicalSort(std::vector<AudioNode*>& sorted);

    jack_client_t* m_jackClient = nullptr;
    int m_bufferSize = 256;
    double m_sampleRate = 48000.0;
    
    std::shared_ptr<SystemAudioNode> m_sysInputNode;
    std::shared_ptr<SystemAudioNode> m_sysOutputNode;
    
    jack_port_t* m_jackInputPorts[2] = { nullptr, nullptr };
    jack_port_t* m_jackOutputPorts[2] = { nullptr, nullptr };
    
    std::string m_hwInputLeft;
    std::string m_hwInputRight;
    std::string m_hwOutputLeft;
    std::string m_hwOutputRight;
    bool m_hwInputStereo = true;
    bool m_hwOutputStereo = true;
    
    std::atomic<float> m_inputGain{1.0f};
    std::atomic<float> m_outputGain{1.0f};
    std::atomic<float> m_inputPeak{0.0f};
    std::atomic<float> m_outputPeak{0.0f};
    
    // Graph representation
    std::vector<std::shared_ptr<AudioNode>> m_nodes;
    std::vector<AudioConnection> m_connections;
    
    // Double buffered execution details for real-time safety
    struct RTGraphData {
        std::vector<AudioNode*> executionOrder;
        // Connections stored as pointers for faster RT lookup
        struct RTConnection {
            float* srcBuffer;
            float* dstBuffer;
            float gain = 1.0f;
            float currentGain = 1.0f;
            std::shared_ptr<std::atomic<float>> liveGain;
        };
        std::vector<RTConnection> rtConnections;
    };
    
    RTGraphData* m_activeGraphData = nullptr;
    RTGraphData* m_pendingGraphData = nullptr;
    std::atomic<RTGraphData*> m_rtGraphData{nullptr};
    RTGraphData* m_suspendedGraphData = nullptr;
    
    std::mutex m_graphMutex;
};
