#pragma once
#include "AudioNode.h"
#include <algorithm>

class BypassNode : public AudioNode {
public:
    BypassNode() {
        // Set up 2 inputs and 2 outputs for stereo dry passthrough
        AudioPort inL = {"In L", true, false, 0, nullptr};
        AudioPort inR = {"In R", true, false, 1, nullptr};
        AudioPort outL = {"Out L", false, false, 0, nullptr};
        AudioPort outR = {"Out R", false, false, 1, nullptr};
        m_ports = {inL, inR, outL, outR};
    }
    
    ~BypassNode() override = default;
    
    std::string getName() const override { return "Bypass / Pass-through"; }
    std::string getPluginURI() const override { return "builtin:bypass"; }
    NodeType getType() const override { return NodeType::LV2Plugin; } // Treat as LV2 to skip generic VST3 stubs
    
    void prepare(double sampleRate, int maxBlockSize) override {
        m_audioBuffers.resize(m_ports.size());
        for (size_t i = 0; i < m_ports.size(); ++i) {
            m_audioBuffers[i].assign(maxBlockSize, 0.0f);
            m_ports[i].buffer = m_audioBuffers[i].data();
        }
    }
    
    void process(int numFrames) override {
        // Copy input buffers to output buffers directly (stereo)
        if (m_ports[0].buffer && m_ports[2].buffer) {
            std::copy(m_ports[0].buffer, m_ports[0].buffer + numFrames, m_ports[2].buffer);
        }
        if (m_ports[1].buffer && m_ports[3].buffer) {
            std::copy(m_ports[1].buffer, m_ports[1].buffer + numFrames, m_ports[3].buffer);
        }
    }

private:
    std::vector<std::vector<float>> m_audioBuffers;
};
