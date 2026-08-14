#pragma once
#include <string>
#include <vector>
#include <atomic>

enum class NodeType {
    SystemInput,
    SystemOutput,
    LV2Plugin,
    VST3Plugin,
    CLAPPlugin
};

struct AudioPort {
    std::string name;
    bool isInput;
    bool isStereo; // true for stereo connection, false for mono
    int channelIdx; // 0 for Mono / Left, 1 for Right
    float* buffer = nullptr; // Pointer to the audio buffer
};

struct ControlPort {
    struct ScalePoint {
        float value;
        std::string label;
    };

    std::string name;
    uint32_t index;
    float value;
    float minVal;
    float maxVal;
    float defaultVal;
    bool isOutput;
    bool isToggle = false;
    bool isInteger = false;
    bool isEnumeration = false;
    std::vector<ScalePoint> scalePoints;
};

class AudioNode {
public:
    virtual ~AudioNode() = default;
    
    virtual std::string getName() const = 0;
    virtual std::string getPluginURI() const { return ""; }
    virtual NodeType getType() const = 0;
    virtual bool isMissing() const { return false; }
    virtual std::string getMissingURI() const { return ""; }
    
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(int numFrames) = 0;
    
    bool isBypassed() const { return m_bypassed.load(std::memory_order_relaxed); }
    void setBypassed(bool bypass) { m_bypassed.store(bypass, std::memory_order_relaxed); }
    
    std::vector<AudioPort>& getPorts() { return m_ports; }
    std::vector<ControlPort>& getControlPorts() { return m_controlPorts; }
    
    int getAudioInputCount() const {
        int n = 0;
        for (auto& p : m_ports) if (p.isInput) ++n;
        return n;
    }
    int getAudioOutputCount() const {
        int n = 0;
        for (auto& p : m_ports) if (!p.isInput) ++n;
        return n;
    }
    
    // Set a control parameter value safely from the GUI thread
    virtual void setParameter(uint32_t index, float value) {
        for (auto& port : m_controlPorts) {
            if (port.index == index) {
                port.value = value;
                break;
            }
        }
    }
    
    struct FileProperty {
        std::string uri;
        std::string label;
        std::string fileValue; // Current loaded file path
    };
    virtual std::vector<FileProperty> getFileProperties() const { return {}; }
    virtual void setFileProperty(const std::string& uri, const std::string& path) {}

    virtual void loadModelFile(const std::string& path) {}
    virtual const std::string& getModelFilePath() const {
        static const std::string empty = "";
        return empty;
    }
    void setModelDisplayName(const std::string& name) { m_modelDisplayName = name; }
    const std::string& getModelDisplayName() const { return m_modelDisplayName; }
    void setModelSourceUrl(const std::string& url) { m_modelSourceUrl = url; }
    const std::string& getModelSourceUrl() const { return m_modelSourceUrl; }

    struct ModelVariant {
        std::string name;
        std::string url;
        std::string localPath;
    };

    struct ModelMetadata {
        std::string toneId;
        std::string toneTitle;
        std::string toneSlug;
        std::string author;
        std::string gearType;
        std::string tags;
        std::string description;
        std::string version;
        std::string architecture;
        std::string modeledBy;
        std::string gearMake;
        std::string gearModel;
        std::string imageUrl;
        double loudness = 0.0;
        double sampleRate = 0.0;
    };

    const ModelMetadata& getModelMetadata() const { return m_modelMetadata; }
    void setModelMetadata(const ModelMetadata& meta) { m_modelMetadata = meta; }

    void clearModelPresentation() {
        m_modelDisplayName.clear();
        m_modelSourceUrl.clear();
        m_modelVariants.clear();
        m_modelMetadata = {};
    }

    const std::vector<ModelVariant>& getModelVariants() const { return m_modelVariants; }
    void setModelVariants(const std::vector<ModelVariant>& vars) { m_modelVariants = vars; }
    void setModelVariantLocalPath(size_t index, const std::string& path) {
        if (index < m_modelVariants.size()) {
            m_modelVariants[index].localPath = path;
        }
    }

    // Grid coordinates in UI
    int gridX = 0;
    int gridY = 0;
    std::string uniqueId;

protected:
    std::vector<AudioPort> m_ports;
    std::vector<ControlPort> m_controlPorts;
    std::atomic<bool> m_bypassed{false};
    std::string m_modelDisplayName;
    std::string m_modelSourceUrl;
    std::vector<ModelVariant> m_modelVariants;
    ModelMetadata m_modelMetadata;
};
