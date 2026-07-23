#pragma once
#include "AudioNode.h"
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
    std::vector<std::vector<float>> m_audioBuffers;
};
