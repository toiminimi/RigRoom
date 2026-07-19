#pragma once
#include "AudioNode.h"
#include <string>

class VST3PluginNode : public AudioNode {
public:
    VST3PluginNode(const std::string& path);
    ~VST3PluginNode() override;
    
    std::string getName() const override { return m_name; }
    std::string getPluginURI() const override { return m_path; }
    NodeType getType() const override { return NodeType::VST3Plugin; }
    
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;
    
private:
    std::string m_name;
    std::string m_path;
    std::vector<std::vector<float>> m_audioBuffers;
};
