#pragma once
#include "AudioNode.h"
#include <map>
#include <algorithm>

class MissingPluginNode : public AudioNode {
public:
    MissingPluginNode(NodeType origType, const std::string& origUri, const std::string& origName)
        : m_originalType(origType), m_originalUri(origUri), m_originalName(origName)
    {
        m_inL.name = "Input L"; m_inL.isInput = true; m_inL.isStereo = false; m_inL.channelIdx = 0;
        m_inR.name = "Input R"; m_inR.isInput = true; m_inR.isStereo = false; m_inR.channelIdx = 1;
        m_outL.name = "Output L"; m_outL.isInput = false; m_outL.isStereo = false; m_outL.channelIdx = 0;
        m_outR.name = "Output R"; m_outR.isInput = false; m_outR.isStereo = false; m_outR.channelIdx = 1;
        
        m_ports = { m_inL, m_inR, m_outL, m_outR };
    }

    bool isMissing() const override { return true; }
    std::string getMissingURI() const override { return m_originalUri; }
    std::string getName() const override {
        if (!m_originalName.empty()) return m_originalName;
        return "Missing Plugin";
    }
    std::string getPluginURI() const override { return m_originalUri; }
    NodeType getType() const override { return m_originalType; }

    void prepare(double sampleRate, int maxBlockSize) override {}

    void process(int numFrames) override {
        float* inL = m_ports[0].buffer;
        float* inR = m_ports[1].buffer;
        float* outL = m_ports[2].buffer;
        float* outR = m_ports[3].buffer;

        if (inL && outL && inL != outL) {
            std::copy(inL, inL + numFrames, outL);
        }
        if (inR && outR && inR != outR) {
            std::copy(inR, inR + numFrames, outR);
        }
    }

    void setSavedParameter(uint32_t index, float val) {
        m_savedParameters[index] = val;
        bool found = false;
        for (auto& cp : m_controlPorts) {
            if (cp.index == index) {
                cp.value = val;
                found = true;
                break;
            }
        }
        if (!found) {
            ControlPort cp;
            cp.name = "Parameter " + std::to_string(index);
            cp.index = index;
            cp.value = val;
            cp.minVal = 0.0f;
            cp.maxVal = 1.0f;
            cp.defaultVal = val;
            cp.isOutput = false;
            m_controlPorts.push_back(cp);
        }
    }

    const std::map<uint32_t, float>& getSavedParameters() const {
        return m_savedParameters;
    }

    void setSavedFileProperty(const std::string& propUri, const std::string& propVal) {
        m_savedFileProperties[propUri] = propVal;
    }

    const std::map<std::string, std::string>& getSavedFileProperties() const {
        return m_savedFileProperties;
    }

    std::vector<FileProperty> getFileProperties() const override {
        std::vector<FileProperty> result;
        for (const auto& kv : m_savedFileProperties) {
            FileProperty fp;
            fp.uri = kv.first;
            fp.fileValue = kv.second;
            result.push_back(fp);
        }
        return result;
    }

    void setFileProperty(const std::string& uri, const std::string& path) override {
        m_savedFileProperties[uri] = path;
    }

    void setParameter(uint32_t index, float value) override {
        setSavedParameter(index, value);
    }

private:
    NodeType m_originalType;
    std::string m_originalUri;
    std::string m_originalName;
    AudioPort m_inL, m_inR, m_outL, m_outR;
    std::map<uint32_t, float> m_savedParameters;
    std::map<std::string, std::string> m_savedFileProperties;
};
