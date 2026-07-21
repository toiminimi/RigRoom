#include "CLAPHost.h"
#include "clap/ext/timer-support.h"
#include "clap/ext/posix-fd-support.h"
#include <dlfcn.h>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <QDir>
#include <QTimer>
#include <QSocketNotifier>
#include <QObject>

class CLAPHostReactor : public QObject {
public:
    explicit CLAPHostReactor(QObject* parent = nullptr) : QObject(parent) {}

    bool registerTimer(const clap_host_t* host, uint32_t period_ms, clap_id* timer_id) {
        if (!host || !host->host_data || !timer_id) return false;
        CLAPPluginNode* node = static_cast<CLAPPluginNode*>(host->host_data);
        const clap_plugin_t* plugin = node->getClapPlugin();
        if (!plugin) return false;

        const clap_plugin_timer_support_t* extTimer = 
            (const clap_plugin_timer_support_t*)plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);
        if (!extTimer || !extTimer->on_timer) return false;

        clap_id id = m_nextTimerId++;
        *timer_id = id;

        QTimer* timer = new QTimer(this);
        timer->setInterval(std::max(15u, period_ms));
        connect(timer, &QTimer::timeout, [plugin, extTimer, id]() {
            extTimer->on_timer(plugin, id);
        });
        m_timers[id] = timer;
        timer->start();
        return true;
    }

    bool unregisterTimer(const clap_host_t* host, clap_id timer_id) {
        auto it = m_timers.find(timer_id);
        if (it != m_timers.end()) {
            it->second->stop();
            delete it->second;
            m_timers.erase(it);
            return true;
        }
        return false;
    }

    bool registerFd(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) {
        if (!host || !host->host_data || fd < 0) return false;
        CLAPPluginNode* node = static_cast<CLAPPluginNode*>(host->host_data);
        const clap_plugin_t* plugin = node->getClapPlugin();
        if (!plugin) return false;

        const clap_plugin_posix_fd_support_t* extFd = 
            (const clap_plugin_posix_fd_support_t*)plugin->get_extension(plugin, CLAP_EXT_POSIX_FD_SUPPORT);
        if (!extFd || !extFd->on_fd) return false;

        unregisterFd(host, fd);

        QSocketNotifier::Type type = QSocketNotifier::Read;
        if (flags & CLAP_POSIX_FD_WRITE) type = QSocketNotifier::Write;

        QSocketNotifier* notifier = new QSocketNotifier(fd, type, this);
        connect(notifier, &QSocketNotifier::activated, [plugin, extFd, fd, flags](int activatedFd) {
            (void)activatedFd;
            extFd->on_fd(plugin, fd, flags);
        });
        m_notifiers[fd] = notifier;
        return true;
    }

    bool modifyFd(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) {
        return registerFd(host, fd, flags);
    }

    bool unregisterFd(const clap_host_t* host, int fd) {
        auto it = m_notifiers.find(fd);
        if (it != m_notifiers.end()) {
            it->second->setEnabled(false);
            delete it->second;
            m_notifiers.erase(it);
            return true;
        }
        return false;
    }

private:
    clap_id m_nextTimerId = 100;
    std::unordered_map<clap_id, QTimer*> m_timers;
    std::unordered_map<int, QSocketNotifier*> m_notifiers;
};

static CLAPHostReactor* get_reactor() {
    static CLAPHostReactor reactor;
    return &reactor;
}

static const void* host_get_extension(const clap_host_t* host, const char* extension_id) {
    if (!host || !extension_id) return nullptr;
    
    if (std::strcmp(extension_id, CLAP_EXT_TIMER_SUPPORT) == 0) {
        static const clap_host_timer_support_t host_timer = {
            [](const clap_host_t* host, uint32_t period_ms, clap_id* timer_id) -> bool {
                return get_reactor()->registerTimer(host, period_ms, timer_id);
            },
            [](const clap_host_t* host, clap_id timer_id) -> bool {
                return get_reactor()->unregisterTimer(host, timer_id);
            }
        };
        return &host_timer;
    }
    if (std::strcmp(extension_id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) {
        static const clap_host_posix_fd_support_t host_posix_fd = {
            [](const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) -> bool {
                return get_reactor()->registerFd(host, fd, flags);
            },
            [](const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) -> bool {
                return get_reactor()->modifyFd(host, fd, flags);
            },
            [](const clap_host_t* host, int fd) -> bool {
                return get_reactor()->unregisterFd(host, fd);
            }
        };
        return &host_posix_fd;
    }
    if (std::strcmp(extension_id, CLAP_EXT_THREAD_CHECK) == 0) {
        static const clap_host_thread_check_t thread_check = {
            [](const clap_host_t* host) -> bool { return true; },
            [](const clap_host_t* host) -> bool { return true; }
        };
        return &thread_check;
    }
    if (std::strcmp(extension_id, CLAP_EXT_PARAMS) == 0) {
        static const clap_host_params_t host_params = {
            [](const clap_host_t* host, clap_param_rescan_flags flags) {},
            [](const clap_host_t* host, clap_id param_id, clap_param_clear_flags flags) {},
            [](const clap_host_t* host) {}
        };
        return &host_params;
    }
    if (std::strcmp(extension_id, CLAP_EXT_STATE) == 0) {
        static const clap_host_state_t host_state = {
            [](const clap_host_t* host) {}
        };
        return &host_state;
    }
    if (std::strcmp(extension_id, CLAP_EXT_LATENCY) == 0) {
        static const clap_host_latency_t host_latency = {
            [](const clap_host_t* host) {}
        };
        return &host_latency;
    }
    if (std::strcmp(extension_id, CLAP_EXT_GUI) == 0) {
        static const clap_host_gui_t host_gui = {
            [](const clap_host_t* host) {},
            [](const clap_host_t* host, uint32_t width, uint32_t height) -> bool { return true; },
            [](const clap_host_t* host) -> bool { return true; },
            [](const clap_host_t* host) -> bool { return true; },
            [](const clap_host_t* host, bool was_destroyed) {}
        };
        return &host_gui;
    }
    return nullptr;
}

static void host_request_restart(const clap_host_t* host) {}
static void host_request_process(const clap_host_t* host) {}
static void host_request_callback(const clap_host_t* host) {}

CLAPPluginNode::CLAPPluginNode(const std::string& libraryPath, uint32_t pluginIndex)
    : m_path(libraryPath), m_pluginIndex(pluginIndex) {

    m_name = std::filesystem::path(libraryPath).stem().string();

    m_libHandle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_libHandle) {
        std::cerr << "CLAPHost: Failed to load library " << libraryPath << ": " << dlerror() << std::endl;
        return;
    }

    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*)dlsym(m_libHandle, "clap_entry");
    if (!entry) {
        std::cerr << "CLAPHost: Failed to find clap_entry symbol in " << libraryPath << std::endl;
        return;
    }

    if (!entry->init(libraryPath.c_str())) {
        std::cerr << "CLAPHost: entry->init failed for " << libraryPath << std::endl;
        return;
    }

    const clap_plugin_factory_t* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory) {
        std::cerr << "CLAPHost: Failed to get CLAP_PLUGIN_FACTORY_ID from " << libraryPath << std::endl;
        return;
    }

    uint32_t count = factory->get_plugin_count(factory);
    if (pluginIndex >= count) {
        std::cerr << "CLAPHost: Invalid plugin index " << pluginIndex << " (count=" << count << ")" << std::endl;
        return;
    }

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, pluginIndex);
    if (!desc) {
        std::cerr << "CLAPHost: Failed to get descriptor at index " << pluginIndex << std::endl;
        return;
    }

    if (desc->name && desc->name[0] != '\0') {
        m_name = desc->name;
    }

    initHostCallbacks();

    m_plugin = factory->create_plugin(factory, &m_host, desc->id);
    if (!m_plugin) {
        std::cerr << "CLAPHost: Failed to create plugin " << desc->id << std::endl;
        return;
    }

    if (!m_plugin->init(m_plugin)) {
        std::cerr << "CLAPHost: Plugin init failed for " << desc->id << std::endl;
        m_plugin = nullptr;
        return;
    }

    m_extParams = (const clap_plugin_params_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS);
    m_extAudioPorts = (const clap_plugin_audio_ports_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_AUDIO_PORTS);
    m_extGui = (const clap_plugin_gui_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_GUI);
    m_extState = (const clap_plugin_state_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_STATE);
    m_extLatency = (const clap_plugin_latency_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_LATENCY);

    setupPortsAndParams();
    std::cout << "CLAPHost: Successfully loaded plugin '" << m_name << "' from " << libraryPath << std::endl;
}

CLAPPluginNode::~CLAPPluginNode() {
    if (m_plugin) {
        if (m_processing) {
            m_plugin->stop_processing(m_plugin);
            m_processing = false;
        }
        if (m_active) {
            m_plugin->deactivate(m_plugin);
            m_active = false;
        }
        m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
    }

    if (m_libHandle) {
        dlclose(m_libHandle);
        m_libHandle = nullptr;
    }
}

void CLAPPluginNode::initHostCallbacks() {
    m_host.clap_version = CLAP_VERSION;
    m_host.host_data = this;
    m_host.name = "PedalBoard";
    m_host.vendor = "PedalBoard";
    m_host.url = "https://github.com/tomi/PedalBoard";
    m_host.version = "1.0.0";
    m_host.get_extension = host_get_extension;
    m_host.request_restart = host_request_restart;
    m_host.request_process = host_request_process;
    m_host.request_callback = host_request_callback;
}

void CLAPPluginNode::setupPortsAndParams() {
    m_ports.clear();
    m_controlPorts.clear();
    m_paramValues.clear();
    m_paramIds.clear();

    int inChannels = 2;
    int outChannels = 2;

    if (m_extAudioPorts) {
        uint32_t numInputs = m_extAudioPorts->count(m_plugin, true);
        if (numInputs > 0) {
            clap_audio_port_info_t info{};
            if (m_extAudioPorts->get(m_plugin, 0, true, &info)) {
                inChannels = info.channel_count;
            }
        }
        uint32_t numOutputs = m_extAudioPorts->count(m_plugin, false);
        if (numOutputs > 0) {
            clap_audio_port_info_t info{};
            if (m_extAudioPorts->get(m_plugin, 0, false, &info)) {
                outChannels = info.channel_count;
            }
        }
    }

    for (int i = 0; i < inChannels; ++i) {
        AudioPort p;
        p.name = (inChannels == 2) ? (i == 0 ? "Input L" : "Input R") : "Input " + std::to_string(i + 1);
        p.isInput = true;
        p.isStereo = (inChannels == 2);
        p.channelIdx = i;
        m_ports.push_back(p);
    }

    for (int i = 0; i < outChannels; ++i) {
        AudioPort p;
        p.name = (outChannels == 2) ? (i == 0 ? "Output L" : "Output R") : "Output " + std::to_string(i + 1);
        p.isInput = false;
        p.isStereo = (outChannels == 2);
        p.channelIdx = i;
        m_ports.push_back(p);
    }

    if (m_extParams) {
        uint32_t numParams = m_extParams->count(m_plugin);
        for (uint32_t i = 0; i < numParams; ++i) {
            clap_param_info_t info{};
            if (m_extParams->get_info(m_plugin, i, &info)) {
                double val = info.default_value;
                m_extParams->get_value(m_plugin, info.id, &val);

                ControlPort cp;
                cp.name = info.name;
                cp.index = static_cast<uint32_t>(m_controlPorts.size());
                cp.minVal = static_cast<float>(info.min_value);
                cp.maxVal = static_cast<float>(info.max_value);
                cp.defaultVal = static_cast<float>(info.default_value);
                cp.value = static_cast<float>(val);
                cp.isOutput = (info.flags & CLAP_PARAM_IS_READONLY);
                cp.isToggle = (info.flags & CLAP_PARAM_IS_STEPPED) && (info.min_value == 0.0) && (info.max_value == 1.0);
                cp.isInteger = (info.flags & CLAP_PARAM_IS_STEPPED);

                m_controlPorts.push_back(cp);
                m_paramValues.push_back(cp.value);
                m_paramIds.push_back(info.id);
            }
        }
    }
}

void CLAPPluginNode::prepare(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    m_audioBuffers.resize(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_audioBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_audioBuffers[i].data();
    }

    if (m_plugin) {
        if (m_processing) {
            m_plugin->stop_processing(m_plugin);
            m_processing = false;
        }
        if (m_active) {
            m_plugin->deactivate(m_plugin);
            m_active = false;
        }

        m_active = m_plugin->activate(m_plugin, sampleRate, maxBlockSize, maxBlockSize);
        if (m_active) {
            m_processing = m_plugin->start_processing(m_plugin);
        }
    }
}

void CLAPPluginNode::setParameter(uint32_t index, float value) {
    if (index < m_controlPorts.size()) {
        m_controlPorts[index].value = value;
        if (index < m_paramValues.size()) {
            m_paramValues[index] = value;
            
            QueuedParamEvent qe{};
            qe.event.header.size = sizeof(clap_event_param_value_t);
            qe.event.header.time = 0;
            qe.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            qe.event.header.type = CLAP_EVENT_PARAM_VALUE;
            qe.event.header.flags = 0;
            qe.event.param_id = m_paramIds[index];
            qe.event.cookie = nullptr;
            qe.event.note_id = -1;
            qe.event.port_index = -1;
            qe.event.channel = -1;
            qe.event.key = -1;
            qe.event.value = value;

            std::lock_guard<std::mutex> lock(m_eventMutex);
            m_queuedParamEvents.push_back(qe);
        }
    }
}

float CLAPPluginNode::getParameter(uint32_t index) const {
    if (index < m_controlPorts.size()) {
        return m_controlPorts[index].value;
    }
    return 0.0f;
}

bool CLAPPluginNode::hasGUI() const {
    return m_extGui != nullptr;
}

void CLAPPluginNode::process(int numFrames) {
    int numInputs = getAudioInputCount();
    int numOutputs = getAudioOutputCount();

    if (isBypassed()) {
        int minChannels = std::min(numInputs, numOutputs);
        for (int c = 0; c < minChannels; ++c) {
            float* src = m_ports[c].buffer;
            float* dst = m_ports[numInputs + c].buffer;
            if (src && dst) {
                std::copy(src, src + numFrames, dst);
            }
        }
        return;
    }

    if (!m_plugin || !m_active || !m_processing) {
        for (int c = 0; c < numOutputs; ++c) {
            float* dst = m_ports[numInputs + c].buffer;
            if (dst) {
                std::memset(dst, 0, numFrames * sizeof(float));
            }
        }
        return;
    }

    std::vector<float*> inPtrs(numInputs);
    for (int i = 0; i < numInputs; ++i) {
        inPtrs[i] = m_ports[i].buffer ? m_ports[i].buffer : m_audioInputBuffers[i].data();
    }

    std::vector<float*> outPtrs(numOutputs);
    for (int i = 0; i < numOutputs; ++i) {
        outPtrs[i] = m_ports[numInputs + i].buffer ? m_ports[numInputs + i].buffer : m_audioOutputBuffers[i].data();
        if (outPtrs[i]) {
            std::memset(outPtrs[i], 0, numFrames * sizeof(float));
        }
    }

    std::vector<QueuedParamEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        events.swap(m_queuedParamEvents);
    }

    clap_input_events_t inEvents{};
    inEvents.ctx = &events;
    inEvents.size = [](const clap_input_events_t* list) -> uint32_t {
        auto* evs = static_cast<std::vector<QueuedParamEvent>*>(list->ctx);
        return static_cast<uint32_t>(evs->size());
    };
    inEvents.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
        auto* evs = static_cast<std::vector<QueuedParamEvent>*>(list->ctx);
        if (index < evs->size()) {
            return &((*evs)[index].event.header);
        }
        return nullptr;
    };

    clap_output_events_t outEvents{};
    outEvents.ctx = this;
    outEvents.try_push = [](const clap_output_events_t* list, const clap_event_header_t* event) -> bool {
        return true;
    };

    clap_audio_buffer_t inBuffer{};
    inBuffer.data32 = inPtrs.data();
    inBuffer.channel_count = numInputs;
    inBuffer.latency = 0;
    inBuffer.constant_mask = 0;

    clap_audio_buffer_t outBuffer{};
    outBuffer.data32 = outPtrs.data();
    outBuffer.channel_count = numOutputs;
    outBuffer.latency = 0;
    outBuffer.constant_mask = 0;

    clap_process_t processStruct{};
    processStruct.steady_time = m_sampleCount;
    processStruct.frames_count = numFrames;
    processStruct.transport = nullptr;
    processStruct.audio_inputs = &inBuffer;
    processStruct.audio_inputs_count = numInputs > 0 ? 1 : 0;
    processStruct.audio_outputs = &outBuffer;
    processStruct.audio_outputs_count = numOutputs > 0 ? 1 : 0;
    processStruct.in_events = &inEvents;
    processStruct.out_events = &outEvents;

    m_plugin->process(m_plugin, &processStruct);
    m_sampleCount += numFrames;
}

std::vector<CLAPPluginDescriptor> CLAPPluginNode::scanLibrary(const std::string& path) {
    std::vector<CLAPPluginDescriptor> result;
    if (!std::filesystem::exists(path)) return result;

    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) return result;

    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
    if (!entry) {
        dlclose(lib);
        return result;
    }

    if (!entry->init(path.c_str())) {
        dlclose(lib);
        return result;
    }

    const clap_plugin_factory_t* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (factory) {
        uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < count; ++i) {
            const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, i);
            if (desc) {
                CLAPPluginDescriptor d;
                d.id = desc->id ? desc->id : "";
                d.name = desc->name ? desc->name : std::filesystem::path(path).stem().string();
                d.vendor = desc->vendor ? desc->vendor : "";
                d.version = desc->version ? desc->version : "";
                d.description = desc->description ? desc->description : "";
                d.pluginPath = path;
                d.pluginIndex = i;

                if (desc->features) {
                    for (int f = 0; desc->features[f] != nullptr; ++f) {
                        d.features.push_back(desc->features[f]);
                    }
                }
                result.push_back(d);
            }
        }
    }

    entry->deinit();
    dlclose(lib);
    return result;
}

std::vector<CLAPPluginDescriptor> CLAPPluginNode::scanStandardPaths() {
    std::vector<CLAPPluginDescriptor> allPlugins;
    std::vector<std::string> dirs = {
        "/usr/lib/clap",
        "/usr/lib64/clap",
        "/usr/local/lib/clap",
        (QDir::homePath() + "/.clap").toStdString()
    };

    QSet<QString> scannedFiles;
    for (const auto& dirPath : dirs) {
        if (!std::filesystem::exists(dirPath)) continue;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                std::string ext = entry.path().extension().string();
                if (ext == ".clap") {
                    QString fullPath = QString::fromStdString(entry.path().string());
                    if (scannedFiles.contains(fullPath)) continue;
                    scannedFiles.insert(fullPath);

                    auto descs = scanLibrary(entry.path().string());
                    allPlugins.insert(allPlugins.end(), descs.begin(), descs.end());
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "CLAPHost: Exception scanning " << dirPath << ": " << e.what() << std::endl;
        }
    }

    return allPlugins;
}
