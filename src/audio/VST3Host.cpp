#include "VST3Host.h"
#include <iostream>
#include <filesystem>
#include <algorithm>

VST3PluginNode::VST3PluginNode(const std::string& path) : m_path(path) {
    std::filesystem::path p(path);
    m_name = p.stem().string();
    
    // Add default input/output ports for representation
    AudioPort inPort;
    inPort.name = "Input";
    inPort.isInput = true;
    inPort.isStereo = true;
    m_ports.push_back(inPort);
    
    AudioPort outPort;
    outPort.name = "Output";
    outPort.isInput = false;
    outPort.isStereo = true;
    m_ports.push_back(outPort);
    
    // Add a couple dummy control ports
    ControlPort ctrl1;
    ctrl1.name = "Gain";
    ctrl1.index = 0;
    ctrl1.minVal = 0.0f;
    ctrl1.maxVal = 1.0f;
    ctrl1.defaultVal = 0.5f;
    ctrl1.value = 0.5f;
    ctrl1.isOutput = false;
    m_controlPorts.push_back(ctrl1);

    ControlPort ctrl2;
    ctrl2.name = "Mix";
    ctrl2.index = 1;
    ctrl2.minVal = 0.0f;
    ctrl2.maxVal = 1.0f;
    ctrl2.defaultVal = 1.0f;
    ctrl2.value = 1.0f;
    ctrl2.isOutput = false;
    m_controlPorts.push_back(ctrl2);
}

VST3PluginNode::~VST3PluginNode() {}

void VST3PluginNode::prepare(double sampleRate, int maxBlockSize) {
    m_audioBuffers.resize(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_audioBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_audioBuffers[i].data();
    }
}

void VST3PluginNode::process(int numFrames) {
    // Basic volume routing for the stub (bypass or simple dry/wet mix)
    if (isBypassed()) {
        std::copy(m_ports[0].buffer, m_ports[0].buffer + numFrames, m_ports[1].buffer);
        return;
    }
    
    float gain = m_controlPorts[0].value;
    for (int i = 0; i < numFrames; ++i) {
        m_ports[1].buffer[i] = m_ports[0].buffer[i] * gain;
    }
}
