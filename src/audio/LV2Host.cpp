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

const void* state_retrieve(LV2_State_Handle handle,
                           uint32_t key,
                           size_t* size,
                           uint32_t* type,
                           uint32_t* flags);

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
    
    return node->queueResponse(data, size);
}

static LV2_Worker_Status worker_schedule_work(LV2_Worker_Schedule_Handle handle,
                                              uint32_t                    size,
                                              const void*                 data) {
    auto* node = static_cast<LV2PluginNode*>(handle);
    if (!node) return LV2_WORKER_ERR_UNKNOWN;
    
    return node->queueWork(data, size);
}

LV2PluginNode::WorkerState::WorkerState() {
    for (size_t i = 0; i < requests.size(); ++i) {
        requests[i].sequence.store(i, std::memory_order_relaxed);
    }
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
    scanFileProperties();
}

LV2PluginNode::~LV2PluginNode() {
    stopWorkerThread();
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
    m_sequenceUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
    
    if (m_instance) {
        stopWorkerThread();
        lilv_instance_deactivate(m_instance);
        lilv_instance_free(m_instance);
        m_instance = nullptr;
    }
    
    m_instance = lilv_plugin_instantiate(m_plugin, sampleRate, m_features);
    if (!m_instance) {
        std::cerr << "Failed to instantiate LV2 plugin: " << m_name << std::endl;
        return;
    }

    startWorkerThread();
    
    // Allocate local buffers for each port
    m_audioBuffers.resize(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_audioBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_audioBuffers[i].data();
    }
    m_audioInputPortIndices.clear();
    m_audioOutputPortIndices.clear();
    m_audioInputPortIndices.reserve(m_ports.size());
    m_audioOutputPortIndices.reserve(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        if (m_ports[i].isInput) {
            m_audioInputPortIndices.push_back(i);
        } else {
            m_audioOutputPortIndices.push_back(i);
        }
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
        seq->atom.type = m_sequenceUrid;
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
    } else if (!m_filePropertiesMap.empty()) {
        const auto* stateInterface = static_cast<const LV2_State_Interface*>(
            lilv_instance_get_extension_data(m_instance, "http://lv2plug.in/ns/ext/state#interface"));
        if (stateInterface && stateInterface->restore) {
            stateInterface->restore(
                m_instance->lv2_handle,
                state_retrieve,
                this,
                0,
                m_features);
        }
    }
}

void LV2PluginNode::process(int numFrames) {
    if (!m_instance) return;
    
    if (isBypassed()) {
        const size_t commonCount = std::min(m_audioInputPortIndices.size(), m_audioOutputPortIndices.size());
        for (size_t i = 0; i < commonCount; ++i) {
            float* input = m_ports[m_audioInputPortIndices[i]].buffer;
            float* output = m_ports[m_audioOutputPortIndices[i]].buffer;
            if (input && output) {
                std::copy(input, input + numFrames, output);
            }
        }
        // Zero out any remaining outputs
        for (size_t i = commonCount; i < m_audioOutputPortIndices.size(); ++i) {
            float* output = m_ports[m_audioOutputPortIndices[i]].buffer;
            if (output) {
                std::fill(output, output + numFrames, 0.0f);
            }
        }
        return;
    }
    
    // Reset both input and output atom ports to empty sequences before processing
    for (auto& atomPort : m_atomPorts) {
        LV2_Atom_Sequence* seq = reinterpret_cast<LV2_Atom_Sequence*>(atomPort.buffer.data());
        seq->atom.type = m_sequenceUrid;
        if (atomPort.isInput) {
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        } else {
            seq->atom.size = atomPort.buffer.size() - sizeof(LV2_Atom);
        }
        seq->body.unit = 0;
        seq->body.pad = 0;
    }
    
    lilv_instance_run(m_instance, numFrames);
    deliverWorkerResponses();
}

void LV2PluginNode::setParameter(uint32_t index, float value) {
    AudioNode::setParameter(index, value);
    // Control ports values are modified directly since we connected the pointer in prepare()
}

void LV2PluginNode::startWorkerThread() {
    if (!m_instance) return;

    const auto* worker = static_cast<const LV2_Worker_Interface*>(
        lilv_instance_get_extension_data(m_instance, LV2_WORKER__interface));
    if (!worker || !worker->work) {
        m_workerState.reset();
        return;
    }

    if (!m_workerState) {
        m_workerState = std::make_unique<WorkerState>();
    }
    if (m_workerState->thread.joinable()) return;

    m_workerState->interface = worker;
    m_workerState->thread = std::thread([this]() { workerLoop(); });
}

void LV2PluginNode::stopWorkerThread() {
    if (!m_workerState) return;

    m_workerState->stopping.store(true, std::memory_order_release);
    m_workerState->wakeCondition.notify_one();
    if (m_workerState->thread.joinable()) {
        m_workerState->thread.join();
    }
    m_workerState.reset();
}

LV2_Worker_Status LV2PluginNode::queueWork(const void* data, uint32_t size) {
    WorkerState* state = m_workerState.get();
    if (!state && !m_instance) {
        // Instantiation is non-RT, but plugins may schedule their first task
        // before Lilv returns the completed instance to the host.
        m_workerState = std::make_unique<WorkerState>();
        state = m_workerState.get();
    }
    if (!state || size > kWorkerMessageCapacity || (size > 0 && !data)) {
        return LV2_WORKER_ERR_NO_SPACE;
    }

    size_t write = state->requestWrite.load(std::memory_order_relaxed);
    while (true) {
        WorkerRequestSlot& slot = state->requests[write % kWorkerQueueCapacity];
        const size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(write);
        if (difference == 0) {
            if (state->requestWrite.compare_exchange_weak(
                    write, write + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                if (size > 0) {
                    std::memcpy(slot.message.data.data(), data, size);
                }
                slot.message.size = size;
                slot.sequence.store(write + 1, std::memory_order_release);
                state->wakeCondition.notify_one();
                return LV2_WORKER_SUCCESS;
            }
        } else if (difference < 0) {
            return LV2_WORKER_ERR_NO_SPACE;
        } else {
            write = state->requestWrite.load(std::memory_order_relaxed);
        }
    }
}

LV2_Worker_Status LV2PluginNode::queueResponse(const void* data, uint32_t size) {
    WorkerState* state = m_workerState.get();
    if (!state || size > kWorkerMessageCapacity || (size > 0 && !data)) {
        return LV2_WORKER_ERR_NO_SPACE;
    }

    const size_t write = state->responseWrite.load(std::memory_order_relaxed);
    const size_t nextWrite = (write + 1) % kWorkerQueueCapacity;
    if (nextWrite == state->responseRead.load(std::memory_order_acquire)) {
        return LV2_WORKER_ERR_NO_SPACE;
    }

    WorkerMessage& message = state->responses[write];
    if (size > 0) {
        std::memcpy(message.data.data(), data, size);
    }
    message.size = size;
    state->responseWrite.store(nextWrite, std::memory_order_release);
    return LV2_WORKER_SUCCESS;
}

void LV2PluginNode::workerLoop() {
    WorkerState* state = m_workerState.get();
    if (!state) return;

    while (!state->stopping.load(std::memory_order_acquire)) {
        const size_t read = state->requestRead.load(std::memory_order_relaxed);
        WorkerRequestSlot& slot = state->requests[read % kWorkerQueueCapacity];
        const size_t sequence = slot.sequence.load(std::memory_order_acquire);
        if (sequence != read + 1) {
            std::unique_lock<std::mutex> lock(state->wakeMutex);
            state->wakeCondition.wait(lock, [state, read]() {
                return state->stopping.load(std::memory_order_acquire) ||
                    state->requests[read % kWorkerQueueCapacity].sequence.load(std::memory_order_acquire) == read + 1;
            });
            continue;
        }

        state->interface->work(
            m_instance->lv2_handle,
            worker_respond,
            this,
            slot.message.size,
            slot.message.data.data());
        slot.sequence.store(read + kWorkerQueueCapacity, std::memory_order_release);
        state->requestRead.store(read + 1, std::memory_order_relaxed);
    }
}

void LV2PluginNode::deliverWorkerResponses() {
    WorkerState* state = m_workerState.get();
    if (!state) return;

    while (state->responseRead.load(std::memory_order_relaxed) !=
           state->responseWrite.load(std::memory_order_acquire)) {
        const size_t read = state->responseRead.load(std::memory_order_relaxed);
        WorkerMessage& message = state->responses[read];
        if (state->interface->work_response) {
            state->interface->work_response(m_instance->lv2_handle, message.size, message.data.data());
        }
        state->responseRead.store((read + 1) % kWorkerQueueCapacity, std::memory_order_release);
    }

    if (state->interface->end_run) {
        state->interface->end_run(m_instance->lv2_handle);
    }
}

const void* state_retrieve(LV2_State_Handle handle,
                                  uint32_t         key,
                                  size_t*          size,
                                  uint32_t*        type,
                                  uint32_t*        flags) {
    auto* node = static_cast<LV2PluginNode*>(handle);
    if (!node) return nullptr;
    
    const char* keyUri = unmap_uri(nullptr, key);
    if (!keyUri) return nullptr;

    std::string uriStr(keyUri);
    LV2_URID modelPathTypeUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Path");

    // Look up in our map first
    auto it = node->m_filePropertiesMap.find(uriStr);
    if (it != node->m_filePropertiesMap.end()) {
        *size = it->second.size() + 1; // include null terminator
        *type = modelPathTypeUrid;
        *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
        return it->second.c_str();
    }

    // Fallback for NAM model key
    LV2_URID modelKeyUrid = map_uri(nullptr, "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model");
    if (key == modelKeyUrid) {
        *size = node->getModelFilePath().size() + 1;
        *type = modelPathTypeUrid;
        *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
        return node->getModelFilePath().c_str();
    }
    
    return nullptr;
}

static bool parsePatchSet(const void* buffer, uint32_t size, std::string& outPropertyUri, std::string& outPath) {
    if (size < 16) return false;

    // URIDs
    LV2_URID patchSetUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/patch#Set");
    LV2_URID patchPropertyUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/patch#property");
    LV2_URID patchValueUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/patch#value");
    LV2_URID pathUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Path");
    LV2_URID uridUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#URID");
    LV2_URID objectUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Object");

    const uint32_t* p = static_cast<const uint32_t*>(buffer);
    uint32_t atomSize = p[0];
    uint32_t atomType = p[1];
    uint32_t otype = p[2];
    uint32_t context = p[3];

    if (atomType != objectUrid || otype != patchSetUrid) return false;

    // Iterator
    const uint8_t* start = static_cast<const uint8_t*>(buffer);
    const uint8_t* end = start + 8 + atomSize;
    const uint8_t* curr = start + 16;

    std::string propertyUri = "";
    std::string filePath = "";

    while (curr + 8 <= end) {
        uint32_t key = *reinterpret_cast<const uint32_t*>(curr);
        uint32_t propContext = *reinterpret_cast<const uint32_t*>(curr + 4);
        curr += 8;

        if (curr + 8 > end) break;
        uint32_t valSize = *reinterpret_cast<const uint32_t*>(curr);
        uint32_t valType = *reinterpret_cast<const uint32_t*>(curr + 4);
        curr += 8;

        auto align8 = [](size_t size) { return (size + 7) & ~7; };
        size_t paddedSize = align8(valSize);
        if (curr + paddedSize > end) break;

        if (key == patchPropertyUrid && valType == uridUrid) {
            uint32_t propUrid = *reinterpret_cast<const uint32_t*>(curr);
            const char* unmapped = unmap_uri(nullptr, propUrid);
            if (unmapped) propertyUri = unmapped;
        } else if (key == patchValueUrid && valType == pathUrid) {
            filePath = std::string(reinterpret_cast<const char*>(curr), valSize);
            if (!filePath.empty() && filePath.back() == '\0') {
                filePath.pop_back();
            }
        }

        curr += paddedSize;
    }

    if (!propertyUri.empty() && !filePath.empty()) {
        outPropertyUri = propertyUri;
        outPath = filePath;
        return true;
    }

    return false;
}

bool LV2PluginNode::handlePortEvent(uint32_t portIndex, uint32_t protocol, const void* buffer, uint32_t size) {
    LV2_URID seqUrid = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
    if (protocol != seqUrid) return false;

    std::string propUri = "";
    std::string filePath = "";
    if (parsePatchSet(buffer, size, propUri, filePath)) {
        setFileProperty(propUri, filePath);
        return true;
    }
    return false;
}

void LV2PluginNode::loadModelFile(const std::string& path) {
    m_modelFilePath = path;
    
    std::string key = "";
    if (getPluginURI().find("neural-amp-modeler") != std::string::npos ||
        getPluginURI() == "http://github.com/mikeoliphant/neural-amp-modeler-lv2") {
        key = "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model";
    } else if (getPluginURI().find("toob-nam") != std::string::npos) {
        key = "http://two-play.com/plugins/toob-nam#modelFile";
    } else if (getPluginURI().find("toob-ml") != std::string::npos) {
        key = "http://two-play.com/plugins/toob-ml#modelFile";
    } else if (getPluginURI().find("toob-cab-ir") != std::string::npos) {
        key = "http://two-play.com/plugins/toob-cab-ir#impulseFile";
    } else if (getPluginURI().find("convolver") != std::string::npos) {
        key = "http://gareus.org/oss/lv2/convolver#irfile";
    } else if (getPluginURI().find("toob.mx/plugins/ir") != std::string::npos) {
        key = "http://toob.mx/plugins/ir#irFile";
    }

    if (!key.empty()) {
        m_filePropertiesMap[key] = path;
        for (auto& fp : m_fileProperties) {
            if (fp.uri == key) {
                fp.fileValue = path;
                break;
            }
        }
    }
    
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
        
    }
}

void LV2PluginNode::setFileProperty(const std::string& uri, const std::string& path) {
    const bool isNamModel = uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model";
    if (isNamModel && path != m_modelFilePath) {
        // A file selected by the plugin UI has no associated TONE3000 context.
        clearModelPresentation();
    }

    m_filePropertiesMap[uri] = path;
    for (auto& fp : m_fileProperties) {
        if (fp.uri == uri) {
            fp.fileValue = path;
            break;
        }
    }

    if (isNamModel) {
        m_modelFilePath = path;
    }

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
        
    }
}

void LV2PluginNode::scanFileProperties() {
    m_fileProperties.clear();

    // 1. Gather all patch writable properties
    LilvNode* patchWritable = lilv_new_uri(m_world, "http://lv2plug.in/ns/ext/patch#writable");
    LilvNodes* writables = lilv_plugin_get_value(m_plugin, patchWritable);
    std::vector<std::string> discoveredUris;

    if (writables) {
        LILV_FOREACH(nodes, i, writables) {
            const LilvNode* prop = lilv_nodes_get(writables, i);
            
            LilvNode* rangeNode = lilv_new_uri(m_world, "http://www.w3.org/2000/01/rdf-schema#range");
            LilvNode* pathNode = lilv_new_uri(m_world, "http://lv2plug.in/ns/ext/atom#Path");
            LilvNodes* ranges = lilv_world_find_nodes(m_world, prop, rangeNode, nullptr);
            bool isPath = false;
            if (ranges) {
                isPath = lilv_nodes_contains(ranges, pathNode);
                lilv_nodes_free(ranges);
            }
            lilv_node_free(rangeNode);
            lilv_node_free(pathNode);

            if (isPath) {
                discoveredUris.push_back(lilv_node_as_string(prop));
            }
        }
        lilv_nodes_free(writables);
    }
    lilv_node_free(patchWritable);

    // 2. Add fallback list of known properties
    std::vector<std::string> fallbacks = {
        "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model",
        "http://two-play.com/plugins/toob-nam#modelFile",
        "http://two-play.com/plugins/toob-ml#modelFile",
        "http://two-play.com/plugins/toob-cab-ir#impulseFile",
        "http://two-play.com/plugins/toob-cab-ir#impulseFile2",
        "http://two-play.com/plugins/toob-cab-ir#impulseFile3",
        "http://gareus.org/oss/lv2/convolver#irfile",
        "http://toob.mx/plugins/ir#irFile",
        "http://toob.mx/plugins/ir#irfile"
    };

    for (const auto& uriStr : fallbacks) {
        if (std::find(discoveredUris.begin(), discoveredUris.end(), uriStr) == discoveredUris.end()) {
            bool match = false;
            std::string pluginUri = getPluginURI();
            if (uriStr.find("neural-amp-modeler") != std::string::npos && pluginUri.find("neural-amp-modeler") != std::string::npos) match = true;
            else if (uriStr.find("toob-nam") != std::string::npos && pluginUri.find("toob-nam") != std::string::npos) match = true;
            else if (uriStr.find("toob-ml") != std::string::npos && pluginUri.find("toob-ml") != std::string::npos) match = true;
            else if (uriStr.find("toob-cab-ir") != std::string::npos && pluginUri.find("toob-cab-ir") != std::string::npos) match = true;
            else if (uriStr.find("convolver") != std::string::npos && pluginUri.find("convolver") != std::string::npos) match = true;
            else if (uriStr.find("toob.mx/plugins/ir") != std::string::npos && pluginUri.find("toob.mx/plugins/ir") != std::string::npos) match = true;

            if (match) {
                discoveredUris.push_back(uriStr);
            }
        }
    }

    // 3. Populate m_fileProperties
    for (const auto& uriStr : discoveredUris) {
        LilvNode* prop = lilv_new_uri(m_world, uriStr.c_str());
        std::string label = "";
        
        LilvNode* labelNode = lilv_new_uri(m_world, "http://www.w3.org/2000/01/rdf-schema#label");
        LilvNodes* labels = lilv_world_find_nodes(m_world, prop, labelNode, nullptr);
        if (labels) {
            const LilvNode* firstLabel = lilv_nodes_get_first(labels);
            if (firstLabel) {
                label = lilv_node_as_string(firstLabel);
            }
            lilv_nodes_free(labels);
        }
        lilv_node_free(labelNode);

        if (label.empty()) {
            size_t hash = uriStr.find_last_of("#/");
            label = (hash != std::string::npos) ? uriStr.substr(hash + 1) : uriStr;
            if (!label.empty()) label[0] = std::toupper(label[0]);
        }

        lilv_node_free(prop);

        FileProperty fp;
        fp.uri = uriStr;
        fp.label = label;
        fp.fileValue = "";
        m_fileProperties.push_back(fp);
    }
}
