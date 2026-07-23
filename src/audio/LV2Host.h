#pragma once
#include "AudioNode.h"
#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/options/options.h>
#include <lv2/log/log.h>
#include <lv2/state/state.h>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <cstdint>
#include <unordered_map>

class LV2PluginNode : public AudioNode {
public:
    LV2PluginNode(LilvWorld* world, const LilvPlugin* plugin);
    ~LV2PluginNode() override;
    
    friend const void* state_retrieve(LV2_State_Handle handle,
                                      uint32_t         key,
                                      size_t*          size,
                                      uint32_t*        type,
                                      uint32_t*        flags);
    
    std::string getName() const override { return m_name; }
    std::string getPluginURI() const override { return m_uri; }
    NodeType getType() const override { return NodeType::LV2Plugin; }
    bool isMissing() const override { return m_plugin == nullptr; }
    
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(int numFrames) override;
    
    void setParameter(uint32_t index, float value) override;
    
    LilvInstance* getInstance() const { return m_instance; }
    double getSampleRate() const { return m_sampleRate; }
    int getMaxBlockSize() const { return m_maxBlockSize; }
    
    struct PendingWorkerTask {
        std::vector<uint8_t> data;
    };
    
    struct PendingResponse {
        std::vector<uint8_t> data;
    };
    
    struct AtomPortData {
        uint32_t index;
        bool isInput;
        std::vector<uint8_t> buffer;
    };
    
    void queueWork(const void* data, uint32_t size);
    void queueResponse(const void* data, uint32_t size);
    void flushWorker();
    
    void loadModelFile(const std::string& path) override;
    const std::string& getModelFilePath() const override { return m_modelFilePath; }
    std::vector<FileProperty> getFileProperties() const override { return m_fileProperties; }
    void setFileProperty(const std::string& uri, const std::string& path) override;
    bool handlePortEvent(uint32_t portIndex, uint32_t protocol, const void* buffer, uint32_t size);
    
    const LilvPlugin* getLilvPlugin() const { return m_plugin; }
    LilvWorld* getLilvWorld() const { return m_world; }
    
private:
    void scanPorts();
    void scanFileProperties();
    
    std::vector<PendingWorkerTask> m_pendingWork;
    std::mutex m_workerMutex;
    
    std::vector<PendingResponse> m_pendingResponses;
    std::mutex m_responseMutex;
    std::vector<AtomPortData> m_atomPorts;
    std::string m_modelFilePath;
    std::vector<FileProperty> m_fileProperties;
    std::unordered_map<std::string, std::string> m_filePropertiesMap;
    
    std::string m_name;
    std::string m_uri;
    LilvWorld* m_world = nullptr;
    const LilvPlugin* m_plugin = nullptr;
    LilvInstance* m_instance = nullptr;
    
    // Internal buffers for the plugin to process
    std::vector<std::vector<float>> m_audioBuffers;
    
    // Persistent features for the plugin instance to prevent stack use-after-free
    LV2_URID_Map m_uridMap;
    LV2_URID_Unmap m_uridUnmap;
    LV2_Worker_Schedule m_workerSchedule;
    LV2_Log_Log m_logInterface;
    LV2_Options_Option m_options[4];
    LV2_State_Map_Path m_mapPath;
    LV2_State_Free_Path m_freePath;
    
    LV2_Feature m_mapFeature;
    LV2_Feature m_unmapFeature;
    LV2_Feature m_workerFeature;
    LV2_Feature m_optionsFeature;
    LV2_Feature m_logFeature;
    LV2_Feature m_mapPathFeature;
    LV2_Feature m_freePathFeature;
    
    const LV2_Feature* m_features[8];
    
    struct PortMapping {
        uint32_t index;
        bool isAudio;
        bool isInput;
        int audioChannelIdx; // for audio ports: 0 or 1
        int controlIdx;      // index in m_controlPorts for control ports
    };
    std::vector<PortMapping> m_portMappings;
    
    double m_sampleRate = 48000.0;
    int m_maxBlockSize = 256;
    float m_optionSampleRate = 48000.0f;
    int32_t m_optionBlockLength = 256;
};
