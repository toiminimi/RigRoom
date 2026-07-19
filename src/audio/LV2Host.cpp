#include "LV2Host.h"
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/worker/worker.h>
#include <lv2/options/options.h>
#include <lv2/log/log.h>
#include <lv2/state/state.h>
#include <cstdarg>
#include <cstring>
#include <cstdint>

// Global URID mapping for LV2 plugins
static std::unordered_map<std::string, LV2_URID> s_uridMap;
static std::mutex s_uridMutex;
static LV2_URID s_nextUrid = 1;

static LV2_URID map_uri(LV2_URID_Map_Handle handle, const char* uri) {
    std::lock_guard<std::mutex> lock(s_uridMutex);
    auto it = s_uridMap.find(uri);
    if (it != s_uridMap.end()) {
        return it->second;
    }
    LV2_URID urid = s_nextUrid++;
    s_uridMap[uri] = urid;
    return urid;
}

static const char* unmap_uri(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
    std::lock_guard<std::mutex> lock(s_uridMutex);
    for (const auto& pair : s_uridMap) {
        if (pair.second == urid) {
            return pair.first.c_str();
        }
    }
    return nullptr;
}

static LV2_URID_Map s_uridMapFeature = { nullptr, map_uri };
static LV2_URID_Unmap s_uridUnmapFeature = { nullptr, unmap_uri };

static int printf_log(LV2_Log_Handle handle, LV2_URID type, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stderr, format, ap);
    va_end(ap);
    return ret;
}

static int vprintf_log(LV2_Log_Handle handle, LV2_URID type, const char* format, va_list ap) {
    return vfprintf(stderr, format, ap);
}

static char* map_abstract_path(LV2_State_Map_Path_Handle handle,
                               const char*               absolute_path) {
    return strdup(absolute_path);
}

static char* map_absolute_path(LV2_State_Map_Path_Handle handle,
                               const char*               abstract_path) {
    return strdup(abstract_path);
}

static void map_free_path(LV2_State_Free_Path_Handle handle,
                          char*                      path) {
    free(path);
}

static LV2_Worker_Status worker_respond(LV2_Worker_Respond_Handle handle,
                                        uint32_t                  size,
                                        const void*               data) {
    auto* node = static_cast<LV2PluginNode*>(handle);
    if (!node) return LV2_WORKER_ERR_UNKNOWN;
    
    node->queueResponse(data, size);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status worker_schedule_work(LV2_Worker_Schedule_Handle handle,
                                              uint32_t                    size,
                                              const void*                 data) {
    auto* node = static_cast<LV2PluginNode*>(handle);
    if (!node) return LV2_WORKER_ERR_UNKNOWN;
    
    node->queueWork(data, size);
    return LV2_WORKER_SUCCESS;
}

LV2PluginNode::LV2PluginNode(LilvWorld* world, const LilvPlugin* plugin)
    : m_world(world), m_plugin(plugin) {
    LilvNode* nameNode = lilv_plugin_get_name(plugin);
    m_name = lilv_node_as_string(nameNode);
    lilv_node_free(nameNode);
    
    const LilvNode* uriNode = lilv_plugin_get_uri(plugin);
    m_uri = lilv_node_as_string(uriNode);
    
    // Initialize persistent features
    m_uridMap.handle = nullptr;
    m_uridMap.map = map_uri;
    
    m_uridUnmap.handle = nullptr;
    m_uridUnmap.unmap = unmap_uri;
    
    m_workerSchedule.handle = this;
    m_workerSchedule.schedule_work = worker_schedule_work;
    
    m_options[0] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
    m_options[1] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
    m_options[2] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
    
    m_logInterface.handle = nullptr;
    m_logInterface.printf = printf_log;
    m_logInterface.vprintf = vprintf_log;
    
    m_mapPath.handle = nullptr;
    m_mapPath.abstract_path = map_abstract_path;
    m_mapPath.absolute_path = map_absolute_path;
    
    m_freePath.handle = nullptr;
    m_freePath.free_path = map_free_path;
    
    m_mapFeature.URI = "http://lv2plug.in/ns/ext/urid#map";
    m_mapFeature.data = &m_uridMap;
    
    m_unmapFeature.URI = "http://lv2plug.in/ns/ext/urid#unmap";
    m_unmapFeature.data = &m_uridUnmap;
    
    m_workerFeature.URI = "http://lv2plug.in/ns/ext/worker#schedule";
    m_workerFeature.data = &m_workerSchedule;
    
    m_optionsFeature.URI = "http://lv2plug.in/ns/ext/options#options";
    m_optionsFeature.data = m_options;
    
    m_logFeature.URI = "http://lv2plug.in/ns/ext/log#log";
    m_logFeature.data = &m_logInterface;
    
    m_mapPathFeature.URI = "http://lv2plug.in/ns/ext/state#mapPath";
    m_mapPathFeature.data = &m_mapPath;
    
    m_freePathFeature.URI = "http://lv2plug.in/ns/ext/state#freePath";
    m_freePathFeature.data = &m_freePath;
    
    m_features[0] = &m_mapFeature;
    m_features[1] = &m_unmapFeature;
    m_features[2] = &m_workerFeature;
    m_features[3] = &m_optionsFeature;
    m_features[4] = &m_logFeature;
    m_features[5] = &m_mapPathFeature;
    m_features[6] = &m_freePathFeature;
    m_features[7] = nullptr;
    
    scanPorts();
}

LV2PluginNode::~LV2PluginNode() {
    if (m_instance) {
        lilv_instance_deactivate(m_instance);
        lilv_instance_free(m_instance);
    }
}

void LV2PluginNode::scanPorts() {
    m_atomPorts.clear();
    
    LilvNode* audioPortClass = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#AudioPort");
    LilvNode* controlPortClass = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#ControlPort");
    LilvNode* inputPortClass = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#InputPort");
    LilvNode* outputPortClass = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#OutputPort");
    LilvNode* atomPortClass = lilv_new_uri(m_world, "http://lv2plug.in/ns/ext/atom#AtomPort");
    LilvNode* toggledProperty = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#toggled");
    LilvNode* integerProperty = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#integer");
    LilvNode* enumerationProperty = lilv_new_uri(m_world, "http://lv2plug.in/ns/lv2core#enumeration");
    
    uint32_t numPorts = lilv_plugin_get_num_ports(m_plugin);
    int audioInCount = 0;
    int audioOutCount = 0;
    
    for (uint32_t i = 0; i < numPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);
        
        bool isAudio = lilv_port_is_a(m_plugin, port, audioPortClass);
        bool isControl = lilv_port_is_a(m_plugin, port, controlPortClass);
        bool isAtom = lilv_port_is_a(m_plugin, port, atomPortClass);
        bool isInput = lilv_port_is_a(m_plugin, port, inputPortClass);
        bool isOutput = lilv_port_is_a(m_plugin, port, outputPortClass);
        
        const LilvNode* symbolNode = lilv_port_get_symbol(m_plugin, port);
        std::string symbol = lilv_node_as_string(symbolNode);
        
        LilvNode* nameNode = lilv_port_get_name(m_plugin, port);
        std::string name = lilv_node_as_string(nameNode);
        lilv_node_free(nameNode);
        
        if (isAudio) {
            PortMapping mapping;
            mapping.index = i;
            mapping.isAudio = true;
            mapping.isInput = isInput;
            mapping.controlIdx = -1;
            
            // Map to audio ports list
            AudioPort audioPort;
            audioPort.name = name;
            audioPort.isInput = isInput;
            
            if (isInput) {
                mapping.audioChannelIdx = audioInCount++;
            } else {
                mapping.audioChannelIdx = audioOutCount++;
            }
            m_portMappings.push_back(mapping);
            m_ports.push_back(audioPort);
        } else if (isControl) {
            PortMapping mapping;
            mapping.index = i;
            mapping.isAudio = false;
            mapping.isInput = isInput;
            mapping.audioChannelIdx = -1;
            
            ControlPort ctrl;
            ctrl.name = name;
            ctrl.index = i;
            ctrl.isOutput = isOutput;
            
            // Get range values
            LilvNode* minVal = nullptr;
            LilvNode* maxVal = nullptr;
            LilvNode* defVal = nullptr;
            lilv_port_get_range(m_plugin, port, &defVal, &minVal, &maxVal);
            
            ctrl.minVal = minVal ? lilv_node_as_float(minVal) : 0.0f;
            ctrl.maxVal = maxVal ? lilv_node_as_float(maxVal) : 1.0f;
            ctrl.defaultVal = defVal ? lilv_node_as_float(defVal) : ctrl.minVal;
            ctrl.value = ctrl.defaultVal;
            ctrl.isToggle = lilv_port_has_property(m_plugin, port, toggledProperty);
            ctrl.isInteger = lilv_port_has_property(m_plugin, port, integerProperty);
            ctrl.isEnumeration = lilv_port_has_property(m_plugin, port, enumerationProperty);

            if (LilvScalePoints* points = lilv_port_get_scale_points(m_plugin, port)) {
                LILV_FOREACH(scale_points, pointIndex, points) {
                    const LilvScalePoint* point = lilv_scale_points_get(points, pointIndex);
                    const LilvNode* value = lilv_scale_point_get_value(point);
                    const LilvNode* label = lilv_scale_point_get_label(point);
                    if (value && label) {
                        ctrl.scalePoints.push_back({
                            lilv_node_as_float(value),
                            lilv_node_as_string(label)
                        });
                    }
                }
                lilv_scale_points_free(points);
                std::sort(ctrl.scalePoints.begin(), ctrl.scalePoints.end(),
                          [](const ControlPort::ScalePoint& left, const ControlPort::ScalePoint& right) {
                              return left.value < right.value;
                          });
            }
            
            if (minVal) lilv_node_free(minVal);
            if (maxVal) lilv_node_free(maxVal);
            if (defVal) lilv_node_free(defVal);
            
            mapping.controlIdx = m_controlPorts.size();
            m_controlPorts.push_back(ctrl);
            m_portMappings.push_back(mapping);
        } else if (isAtom) {
            AtomPortData atomPort;
            atomPort.index = i;
            atomPort.isInput = isInput;
            atomPort.buffer.assign(4096, 0);
            
            if (isInput) {
                LV2_Atom_Sequence* seq = reinterpret_cast<LV2_Atom_Sequence*>(atomPort.buffer.data());
                LV2_URID sequenceUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
                seq->atom.type = sequenceUrid;
                seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
                seq->body.unit = 0;
                seq->body.pad = 0;
            }
            m_atomPorts.push_back(atomPort);
        }
    }
    
    // Group stereo channels if available
    // e.g. if we have 2 audio inputs, map them to Stereo input
    for (size_t i = 0; i < m_ports.size(); ++i) {
        if (m_ports[i].isInput && audioInCount == 2) {
            m_ports[i].isStereo = true;
        } else if (!m_ports[i].isInput && audioOutCount == 2) {
            m_ports[i].isStereo = true;
        } else {
            m_ports[i].isStereo = false;
        }
    }
    
    lilv_node_free(audioPortClass);
    lilv_node_free(controlPortClass);
    lilv_node_free(inputPortClass);
    lilv_node_free(outputPortClass);
    lilv_node_free(atomPortClass);
    lilv_node_free(toggledProperty);
    lilv_node_free(integerProperty);
    lilv_node_free(enumerationProperty);
}

void LV2PluginNode::prepare(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_optionSampleRate = static_cast<float>(sampleRate);
    m_optionBlockLength = maxBlockSize;
    m_options[0] = { LV2_OPTIONS_INSTANCE, 0,
        map_uri(nullptr, "http://lv2plug.in/ns/ext/parameters#sampleRate"), sizeof(float),
        map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Float"), &m_optionSampleRate };
    m_options[1] = { LV2_OPTIONS_INSTANCE, 0,
        map_uri(nullptr, "http://lv2plug.in/ns/ext/buf-size#nominalBlockLength"), sizeof(int32_t),
        map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Int"), &m_optionBlockLength };
    m_options[2] = { LV2_OPTIONS_INSTANCE, 0,
        map_uri(nullptr, "http://lv2plug.in/ns/ext/buf-size#maxBlockLength"), sizeof(int32_t),
        map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Int"), &m_optionBlockLength };
    m_options[3] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
    
    if (m_instance) {
        lilv_instance_deactivate(m_instance);
        lilv_instance_free(m_instance);
        m_instance = nullptr;
    }
    
    m_instance = lilv_plugin_instantiate(m_plugin, sampleRate, m_features);
    if (!m_instance) {
        std::cerr << "Failed to instantiate LV2 plugin: " << m_name << std::endl;
        return;
    }
    
    // Allocate local buffers for each port
    m_audioBuffers.resize(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_audioBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_audioBuffers[i].data();
    }
    
    // Connect ports
    for (const auto& mapping : m_portMappings) {
        if (mapping.isAudio) {
            // Find which index in m_ports corresponds to this mapping index
            int localPortIdx = -1;
            for (size_t j = 0; j < m_portMappings.size(); ++j) {
                if (m_portMappings[j].index == mapping.index) {
                    // Find port corresponding to this audioChannelIdx and isInput
                    int portCount = 0;
                    for (size_t p = 0; p < m_ports.size(); ++p) {
                        if (m_ports[p].isInput == mapping.isInput) {
                            if (portCount == mapping.audioChannelIdx) {
                                localPortIdx = p;
                                break;
                            }
                            portCount++;
                        }
                    }
                    break;
                }
            }
            if (localPortIdx != -1) {
                lilv_instance_connect_port(m_instance, mapping.index, m_ports[localPortIdx].buffer);
            }
        } else {
            // Control port
            lilv_instance_connect_port(m_instance, mapping.index, &m_controlPorts[mapping.controlIdx].value);
        }
    }
    
    // Reset and connect atom ports
    for (auto& atomPort : m_atomPorts) {
        LV2_Atom_Sequence* seq = reinterpret_cast<LV2_Atom_Sequence*>(atomPort.buffer.data());
        LV2_URID sequenceUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
        seq->atom.type = sequenceUrid;
        if (atomPort.isInput) {
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        } else {
            seq->atom.size = atomPort.buffer.size() - sizeof(LV2_Atom);
        }
        seq->body.unit = 0;
        seq->body.pad = 0;
        
        lilv_instance_connect_port(m_instance, atomPort.index, atomPort.buffer.data());
    }
    
    lilv_instance_activate(m_instance);
    if (!m_modelFilePath.empty()) {
        loadModelFile(m_modelFilePath);
    } else {
        flushWorker();
    }
}

void LV2PluginNode::process(int numFrames) {
    if (!m_instance) return;
    
    if (isBypassed()) {
        // Bypass logic: copy input buffers directly to output buffers
        // Simple 1-to-1 copy for matching channels
        int inIdx = 0;
        int outIdx = 0;
        std::vector<float*> inputs;
        std::vector<float*> outputs;
        
        for (auto& port : m_ports) {
            if (port.isInput) {
                inputs.push_back(port.buffer);
            } else {
                outputs.push_back(port.buffer);
            }
        }
        
        size_t commonCount = std::min(inputs.size(), outputs.size());
        for (size_t i = 0; i < commonCount; ++i) {
            std::copy(inputs[i], inputs[i] + numFrames, outputs[i]);
        }
        // Zero out any remaining outputs
        for (size_t i = commonCount; i < outputs.size(); ++i) {
            std::fill(outputs[i], outputs[i] + numFrames, 0.0f);
        }
        return;
    }
    
    // Reset both input and output atom ports to empty sequences before processing
    for (auto& atomPort : m_atomPorts) {
        LV2_Atom_Sequence* seq = reinterpret_cast<LV2_Atom_Sequence*>(atomPort.buffer.data());
        LV2_URID sequenceUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
        seq->atom.type = sequenceUrid;
        if (atomPort.isInput) {
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        } else {
            seq->atom.size = atomPort.buffer.size() - sizeof(LV2_Atom);
        }
        seq->body.unit = 0;
        seq->body.pad = 0;
    }
    
    lilv_instance_run(m_instance, numFrames);
    flushWorker();
}

void LV2PluginNode::setParameter(uint32_t index, float value) {
    AudioNode::setParameter(index, value);
    // Control ports values are modified directly since we connected the pointer in prepare()
}

void LV2PluginNode::queueWork(const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(m_workerMutex);
    PendingWorkerTask task;
    const uint8_t* byteData = static_cast<const uint8_t*>(data);
    task.data.assign(byteData, byteData + size);
    m_pendingWork.push_back(task);
}

void LV2PluginNode::queueResponse(const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(m_responseMutex);
    PendingResponse resp;
    const uint8_t* byteData = static_cast<const uint8_t*>(data);
    resp.data.assign(byteData, byteData + size);
    m_pendingResponses.push_back(resp);
}

void LV2PluginNode::flushWorker() {
    if (!m_instance) return;
    
    const LV2_Worker_Interface* worker = (const LV2_Worker_Interface*)
        lilv_instance_get_extension_data(m_instance, "http://lv2plug.in/ns/ext/worker#interface");
    if (!worker || !worker->work) return;
    
    while (true) {
        std::vector<PendingWorkerTask> tasks;
        {
            std::lock_guard<std::mutex> lock(m_workerMutex);
            if (m_pendingWork.empty()) break;
            tasks.swap(m_pendingWork);
        }
        
        for (const auto& task : tasks) {
            // 1. Run the plugin work method (which calls worker_respond -> queueResponse)
            worker->work(
                m_instance->lv2_handle,
                worker_respond,
                this,
                task.data.size(),
                task.data.data()
            );
            
            // 2. Retrieve responses queued during work execution
            std::vector<PendingResponse> responses;
            {
                std::lock_guard<std::mutex> lock(m_responseMutex);
                responses.swap(m_pendingResponses);
            }
            
            // 3. Process the work_response callbacks sequentially (outside the work callback context)
            for (const auto& resp : responses) {
                if (worker->work_response) {
                    worker->work_response(m_instance->lv2_handle, resp.data.size(), resp.data.data());
                }
            }
            
            // 4. Notify the plugin that the run cycle has finished
            if (worker->end_run) {
                worker->end_run(m_instance->lv2_handle);
            }
        }
    }
}

static const void* state_retrieve(LV2_State_Handle handle,
                                  uint32_t         key,
                                  size_t*          size,
                                  uint32_t*        type,
                                  uint32_t*        flags) {
    auto* node = static_cast<LV2PluginNode*>(handle);
    if (!node) return nullptr;
    LV2_URID modelKeyUrid = map_uri(nullptr, "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model");
    LV2_URID modelPathTypeUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Path");
    
    if (key == modelKeyUrid) {
        *size = node->getModelFilePath().size() + 1; // include null terminator
        *type = modelPathTypeUrid;
        *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
        return node->getModelFilePath().c_str();
    }
    
    return nullptr;
}

void LV2PluginNode::loadModelFile(const std::string& path) {
    m_modelFilePath = path;
    
    if (!m_instance) return;
    
    const LV2_State_Interface* stateInterface = (const LV2_State_Interface*)
        lilv_instance_get_extension_data(m_instance, "http://lv2plug.in/ns/ext/state#interface");
        
    if (stateInterface && stateInterface->restore) {
        stateInterface->restore(
            m_instance->lv2_handle,
            state_retrieve,
            this,
            0,
            m_features
        );
        
        flushWorker();
    }
}
