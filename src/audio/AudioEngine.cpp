#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <set>
#include <map>
#include <cstring>

SystemAudioNode::SystemAudioNode(const std::string& name, bool isInput, int numChannels)
    : m_name(name), m_isInput(isInput) {
    m_ports.resize(numChannels);
    m_localBuffers.resize(numChannels);
    for (int i = 0; i < numChannels; ++i) {
        m_ports[i].name = "Channel " + std::to_string(i + 1);
        m_ports[i].isInput = !isInput;
        m_ports[i].isStereo = (numChannels == 2);
        m_ports[i].channelIdx = i;
    }
}

void SystemAudioNode::prepare(double sampleRate, int maxBlockSize) {
    for (int i = 0; i < (int)m_ports.size(); ++i) {
        m_localBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_localBuffers[i].data();
    }
}

void SystemAudioNode::process(int numFrames) {
    // System nodes don't do DSP, they just hold JACK input/output buffers
}

AudioEngine::AudioEngine() {
    m_sysInputNode = std::make_shared<SystemAudioNode>("System Input", true, 2);
    m_sysInputNode->uniqueId = "system_input";
    m_sysOutputNode = std::make_shared<SystemAudioNode>("System Output", false, 2);
    m_sysOutputNode->uniqueId = "system_output";
}

AudioEngine::~AudioEngine() {
    stop();
    if (m_jackClient) {
        jack_client_close(m_jackClient);
    }
}

bool AudioEngine::init(const std::string& clientName) {
    jack_options_t options = JackNullOption;
    jack_status_t status;
    
    m_jackClient = jack_client_open(clientName.c_str(), options, &status);
    if (!m_jackClient) {
        std::cerr << "Failed to open JACK client!" << std::endl;
        return false;
    }
    
    m_sampleRate = jack_get_sample_rate(m_jackClient);
    m_bufferSize = jack_get_buffer_size(m_jackClient);
    
    // Register static client ports immediately so they are available for connection mapping
    for (int i = 0; i < 2; ++i) {
        std::string inPortName = "input_" + std::to_string(i + 1);
        std::string outPortName = "output_" + std::to_string(i + 1);
        m_jackInputPorts[i] = jack_port_register(m_jackClient, inPortName.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        m_jackOutputPorts[i] = jack_port_register(m_jackClient, outPortName.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    }
    
    jack_set_process_callback(m_jackClient, processCallback, this);
    jack_set_buffer_size_callback(m_jackClient, bufferSizeCallback, this);
    jack_on_shutdown(m_jackClient, shutdownCallback, this);
    
    m_sysInputNode->prepare(m_sampleRate, 1024);
    m_sysOutputNode->prepare(m_sampleRate, 1024);
    
    addNode(m_sysInputNode);
    addNode(m_sysOutputNode);
    
    rebuildGraph();
    
    return true;
}

bool AudioEngine::start() {
    if (!m_jackClient) return false;
    
    if (jack_activate(m_jackClient) != 0) {
        std::cerr << "Failed to activate JACK client!" << std::endl;
        return false;
    }
    
    // Auto-detect hardware port defaults if not loaded from configuration
    if (m_hwInputLeft.empty() || m_hwOutputLeft.empty()) {
        auto physicalInputs = getPhysicalInputs();
        auto physicalOutputs = getPhysicalOutputs();
        
        if (m_hwInputLeft.empty() && !physicalInputs.empty()) {
            m_hwInputLeft = physicalInputs[0];
            if (physicalInputs.size() > 1) {
                m_hwInputRight = physicalInputs[1];
            } else {
                m_hwInputRight = physicalInputs[0];
            }
        }
        if (m_hwOutputLeft.empty() && !physicalOutputs.empty()) {
            m_hwOutputLeft = physicalOutputs[0];
            if (physicalOutputs.size() > 1) {
                m_hwOutputRight = physicalOutputs[1];
            } else {
                m_hwOutputRight = physicalOutputs[0];
            }
        }
    }
    
    updateHardwareConnections();
    return true;
}

void AudioEngine::setHardwareInputPorts(const std::string& left, const std::string& right, bool isStereo) {
    m_hwInputLeft = left;
    m_hwInputRight = right;
    m_hwInputStereo = isStereo;
    updateHardwareConnections();
}

void AudioEngine::setHardwareOutputPorts(const std::string& left, const std::string& right, bool isStereo) {
    m_hwOutputLeft = left;
    m_hwOutputRight = right;
    m_hwOutputStereo = isStereo;
    updateHardwareConnections();
}

void AudioEngine::updateHardwareConnections() {
    if (!m_jackClient) return;
    
    std::string clientName = jack_get_client_name(m_jackClient);
    std::string myInput1 = clientName + ":input_1";
    std::string myInput2 = clientName + ":input_2";
    std::string myOutput1 = clientName + ":output_1";
    std::string myOutput2 = clientName + ":output_2";
    
    // Disconnect our client ports from any existing physical capture/playback channels
    jack_port_t* inPort1 = jack_port_by_name(m_jackClient, myInput1.c_str());
    jack_port_t* inPort2 = jack_port_by_name(m_jackClient, myInput2.c_str());
    jack_port_t* outPort1 = jack_port_by_name(m_jackClient, myOutput1.c_str());
    jack_port_t* outPort2 = jack_port_by_name(m_jackClient, myOutput2.c_str());
    
    if (inPort1) jack_port_disconnect(m_jackClient, inPort1);
    if (inPort2) jack_port_disconnect(m_jackClient, inPort2);
    if (outPort1) jack_port_disconnect(m_jackClient, outPort1);
    if (outPort2) jack_port_disconnect(m_jackClient, outPort2);
    
    // Connect selected hardware input ports to client input ports
    if (m_hwInputStereo) {
        if (!m_hwInputLeft.empty()) {
            jack_connect(m_jackClient, m_hwInputLeft.c_str(), myInput1.c_str());
        }
        if (!m_hwInputRight.empty()) {
            jack_connect(m_jackClient, m_hwInputRight.c_str(), myInput2.c_str());
        }
    } else {
        // MONO Input: Route single input to both client input channels
        if (!m_hwInputLeft.empty()) {
            jack_connect(m_jackClient, m_hwInputLeft.c_str(), myInput1.c_str());
            jack_connect(m_jackClient, m_hwInputLeft.c_str(), myInput2.c_str());
        }
    }
    
    // Connect client output ports to selected hardware output ports
    if (m_hwOutputStereo) {
        if (!m_hwOutputLeft.empty()) {
            jack_connect(m_jackClient, myOutput1.c_str(), m_hwOutputLeft.c_str());
        }
        if (!m_hwOutputRight.empty()) {
            jack_connect(m_jackClient, myOutput2.c_str(), m_hwOutputRight.c_str());
        }
    } else {
        // MONO Output: Route both client output channels to single output
        if (!m_hwOutputLeft.empty()) {
            jack_connect(m_jackClient, myOutput1.c_str(), m_hwOutputLeft.c_str());
            jack_connect(m_jackClient, myOutput2.c_str(), m_hwOutputLeft.c_str());
        }
    }
}

void AudioEngine::stop() {
    if (m_jackClient) {
        jack_deactivate(m_jackClient);
    }
}

void AudioEngine::addNode(std::shared_ptr<AudioNode> node) {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    // Check if node already exists
    for (const auto& n : m_nodes) {
        if (n->uniqueId == node->uniqueId) return;
    }
    
    node->prepare(m_sampleRate, m_bufferSize);
    m_nodes.push_back(node);
}

void AudioEngine::removeNode(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    // Remove connections
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [&](const AudioConnection& conn) {
                return conn.srcNodeId == nodeId || conn.dstNodeId == nodeId;
            }),
        m_connections.end()
    );
    
    // Remove node
    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(),
            [&](const std::shared_ptr<AudioNode>& n) {
                return n->uniqueId == nodeId;
            }),
        m_nodes.end()
    );
}

void AudioEngine::connectPorts(const std::string& srcId, int srcPort, const std::string& dstId, int dstPort,
                               float gain, std::shared_ptr<std::atomic<float>> liveGain) {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    // Verify node IDs
    bool srcFound = false, dstFound = false;
    for (const auto& n : m_nodes) {
        if (n->uniqueId == srcId) srcFound = true;
        if (n->uniqueId == dstId) dstFound = true;
    }
    if (!srcFound || !dstFound) return;
    
    // Parallel lanes may intentionally produce the same graph edge.  Fold
    // those routes into one connection with their gains summed.
    for (auto& conn : m_connections) {
        if (conn.srcNodeId == srcId && conn.srcPortIdx == srcPort &&
            conn.dstNodeId == dstId && conn.dstPortIdx == dstPort &&
            !conn.liveGain && !liveGain) {
            conn.gain += gain;
            return;
        }
    }
    
    m_connections.push_back({srcId, srcPort, dstId, dstPort, gain, std::move(liveGain)});
}

void AudioEngine::disconnectPorts(const std::string& srcId, int srcPort, const std::string& dstId, int dstPort) {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [&](const AudioConnection& conn) {
                return conn.srcNodeId == srcId && conn.srcPortIdx == srcPort &&
                       conn.dstNodeId == dstId && conn.dstPortIdx == dstPort;
            }),
        m_connections.end()
    );
}

void AudioEngine::clearConnections() {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    m_connections.clear();
}

void AudioEngine::clearGraph() {
    std::lock_guard<std::mutex> lock(m_graphMutex);

    m_connections.clear();
    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(),
            [&](const std::shared_ptr<AudioNode>& n) {
                return n->uniqueId != "system_input" && n->uniqueId != "system_output";
            }),
        m_nodes.end()
    );
}

void AudioEngine::setBufferSize(int size) {
    if (m_jackClient) {
        jack_set_buffer_size(m_jackClient, size);
    }
}

float AudioEngine::getCPULoad() const {
    if (!m_jackClient) return 0.0f;
    return jack_cpu_load(m_jackClient);
}

std::vector<std::shared_ptr<AudioNode>> AudioEngine::getNodes() const {
    return m_nodes;
}

std::vector<AudioConnection> AudioEngine::getConnections() const {
    return m_connections;
}

void AudioEngine::topologicalSort(std::vector<AudioNode*>& sorted) {
    // Kahn's algorithm for topological sorting
    std::map<AudioNode*, int> inDegree;
    std::map<AudioNode*, std::vector<AudioNode*>> adj;
    std::set<AudioNode*> allNodes;
    
    for (auto& n : m_nodes) {
        allNodes.insert(n.get());
        inDegree[n.get()] = 0;
    }
    
    for (const auto& conn : m_connections) {
        AudioNode* src = nullptr;
        AudioNode* dst = nullptr;
        for (auto& n : m_nodes) {
            if (n->uniqueId == conn.srcNodeId) src = n.get();
            if (n->uniqueId == conn.dstNodeId) dst = n.get();
        }
        if (src && dst) {
            adj[src].push_back(dst);
            inDegree[dst]++;
        }
    }
    
    std::queue<AudioNode*> q;
    for (auto* n : allNodes) {
        if (inDegree[n] == 0) {
            q.push(n);
        }
    }
    
    while (!q.empty()) {
        AudioNode* u = q.front();
        q.pop();
        sorted.push_back(u);
        
        for (AudioNode* v : adj[u]) {
            inDegree[v]--;
            if (inDegree[v] == 0) {
                q.push(v);
            }
        }
    }
    
    // Add any cycles or remaining nodes to make sure we don't drop them
    for (auto* n : allNodes) {
        if (std::find(sorted.begin(), sorted.end(), n) == sorted.end()) {
            sorted.push_back(n);
        }
    }
}

void AudioEngine::rebuildGraph() {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    // Nodes are prepared on creation. Re-preparing every routing edit can
    // restart hosted plugins and is unnecessary while their buffers are stable.
    if (m_needsNodePrepare.exchange(false, std::memory_order_acq_rel)) {
        for (auto& n : m_nodes) {
            n->prepare(m_sampleRate, m_bufferSize);
        }
    }
    
    // 2. Compute topological sort
    std::vector<AudioNode*> sorted;
    topologicalSort(sorted);
    
    // 3. Build new RT graph data
    auto pending = std::make_shared<RTGraphData>();
    pending->nodeRefs = m_nodes;
    pending->executionOrder = sorted;
    
    for (const auto& conn : m_connections) {
        AudioNode* src = nullptr;
        AudioNode* dst = nullptr;
        for (auto& n : m_nodes) {
            if (n->uniqueId == conn.srcNodeId) src = n.get();
            if (n->uniqueId == conn.dstNodeId) dst = n.get();
        }
        if (src && dst) {
            float* srcBuffer = nullptr;
            int outCount = 0;
            for (const auto& port : src->getPorts()) {
                if (!port.isInput) {
                    if (outCount == conn.srcPortIdx) {
                        srcBuffer = port.buffer;
                        break;
                    }
                    outCount++;
                }
            }
            
            float* dstBuffer = nullptr;
            int inCount = 0;
            for (const auto& port : dst->getPorts()) {
                if (port.isInput) {
                    if (inCount == conn.dstPortIdx) {
                        dstBuffer = port.buffer;
                        break;
                    }
                    inCount++;
                }
            }
            
            if (srcBuffer && dstBuffer) {
                RTGraphData::RTConnection rtConn;
                rtConn.srcBuffer = srcBuffer;
                rtConn.dstBuffer = dstBuffer;
                rtConn.gain = conn.gain;
                rtConn.liveGain = conn.liveGain;
                rtConn.currentGain = conn.liveGain
                    ? conn.liveGain->load(std::memory_order_relaxed) * conn.gain
                    : conn.gain;
                pending->rtConnections.push_back(rtConn);
            }
        }
    }
    
    // 4. Atomically swap graph structure
    if (m_suspendedGraphData) {
        m_suspendedGraphData = std::move(pending);
    } else {
        m_rtGraphData.store(std::move(pending), std::memory_order_release);
    }
}

void AudioEngine::suspendProcessing() {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    if (m_suspendedGraphData) return;
    
    m_suspendedGraphData = m_rtGraphData.exchange(nullptr, std::memory_order_acq_rel);
}

void AudioEngine::resumeProcessing() {
    std::lock_guard<std::mutex> lock(m_graphMutex);
    if (m_suspendedGraphData) {
        m_rtGraphData.store(std::move(m_suspendedGraphData), std::memory_order_release);
    }
}

int AudioEngine::processCallback(jack_nframes_t nframes, void* arg) {
    auto* engine = static_cast<AudioEngine*>(arg);
    engine->processAudio(nframes);
    return 0;
}

int AudioEngine::bufferSizeCallback(jack_nframes_t nframes, void* arg) {
    auto* engine = static_cast<AudioEngine*>(arg);
    engine->m_bufferSize = nframes;
    engine->m_needsNodePrepare = true;
    engine->rebuildGraph();
    return 0;
}

void AudioEngine::shutdownCallback(void* arg) {
    std::cerr << "JACK server shut down!" << std::endl;
}

void AudioEngine::processAudio(int numFrames) {
    // Retrieve system JACK buffers and zero out output buffers first
    float* jackInBuffers[2] = { nullptr, nullptr };
    float* jackOutBuffers[2] = { nullptr, nullptr };
    
    for (int i = 0; i < 2; ++i) {
        if (m_jackInputPorts[i]) {
            jackInBuffers[i] = (float*)jack_port_get_buffer(m_jackInputPorts[i], numFrames);
        }
        if (m_jackOutputPorts[i]) {
            jackOutBuffers[i] = (float*)jack_port_get_buffer(m_jackOutputPorts[i], numFrames);
            if (jackOutBuffers[i]) {
                std::memset(jackOutBuffers[i], 0, numFrames * sizeof(float));
            }
        }
    }

    const auto graph = m_rtGraphData.load(std::memory_order_acquire);
    if (!graph) return;
    
    // 1. Copy physical JACK input buffers to SystemInput, apply input gain & measure input peak
    float inPeak = 0.0f;
    float inGain = m_inputGain.load(std::memory_order_relaxed);
    for (int i = 0; i < 2; ++i) {
        float* src = jackInBuffers[i];
        float* dst = m_sysInputNode->getPorts()[i].buffer;
        if (src && dst) {
            for (int k = 0; k < numFrames; ++k) {
                float val = src[k] * inGain;
                dst[k] = val;
                float absVal = std::abs(val);
                if (absVal > inPeak) inPeak = absVal;
            }
        } else if (dst) {
            std::memset(dst, 0, numFrames * sizeof(float));
        }
    }
    float previousInputPeak = m_inputPeak.load(std::memory_order_relaxed);
    while (inPeak > previousInputPeak &&
           !m_inputPeak.compare_exchange_weak(previousInputPeak, inPeak, std::memory_order_relaxed)) {}
    
    // 2. Zero out all input buffers for all nodes in execution order except SystemInput
    for (AudioNode* node : graph->executionOrder) {
        if (node->getType() != NodeType::SystemInput) {
            for (auto& port : node->getPorts()) {
                if (port.isInput && port.buffer) {
                    std::memset(port.buffer, 0, numFrames * sizeof(float));
                }
            }
        }
    }
    
    // 3. Traverse nodes in order, sum/move connections, and process
    for (AudioNode* node : graph->executionOrder) {
        // Run DSP processing for the node
        node->process(numFrames);
        
        // Push this node's outputs forward to all connected target input ports
        for (auto& conn : graph->rtConnections) {
            // Find if connection source belongs to this node's output buffers
            bool isSrcOfNode = false;
            for (const auto& port : node->getPorts()) {
                if (!port.isInput && port.buffer == conn.srcBuffer) {
                    isSrcOfNode = true;
                    break;
                }
            }
            if (isSrcOfNode) {
                const float targetGain = conn.liveGain
                    ? conn.liveGain->load(std::memory_order_relaxed) * conn.gain
                    : conn.gain;
                const float gainStep = (targetGain - conn.currentGain) / std::max(1, numFrames);
                float gain = conn.currentGain;
                for (int i = 0; i < numFrames; ++i) {
                    gain += gainStep;
                    conn.dstBuffer[i] += conn.srcBuffer[i] * gain;
                }
                conn.currentGain = targetGain;
            }
        }
    }
    
    // 4. Copy SystemOutput node buffers to physical JACK, apply output gain & measure output peak
    float outPeak = 0.0f;
    float outGain = m_outputGain.load(std::memory_order_relaxed);
    for (int i = 0; i < 2; ++i) {
        float* src = m_sysOutputNode->getPorts()[i].buffer;
        float* dst = jackOutBuffers[i];
        if (src && dst) {
            for (int k = 0; k < numFrames; ++k) {
                float val = src[k] * outGain;
                dst[k] = val;
                float absVal = std::abs(val);
                if (absVal > outPeak) outPeak = absVal;
            }
        }
    }
    float previousOutputPeak = m_outputPeak.load(std::memory_order_relaxed);
    while (outPeak > previousOutputPeak &&
           !m_outputPeak.compare_exchange_weak(previousOutputPeak, outPeak, std::memory_order_relaxed)) {}
}

std::vector<std::string> AudioEngine::getPhysicalInputs() const {
    std::vector<std::string> list;
    if (!m_jackClient) return list;
    const char** ports = jack_get_ports(m_jackClient, nullptr, nullptr, JackPortIsPhysical | JackPortIsOutput);
    if (ports) {
        for (int i = 0; ports[i]; ++i) {
            list.push_back(ports[i]);
        }
        jack_free(ports);
    }
    return list;
}

std::vector<std::string> AudioEngine::getPhysicalOutputs() const {
    std::vector<std::string> list;
    if (!m_jackClient) return list;
    const char** ports = jack_get_ports(m_jackClient, nullptr, nullptr, JackPortIsPhysical | JackPortIsInput);
    if (ports) {
        for (int i = 0; ports[i]; ++i) {
            list.push_back(ports[i]);
        }
        jack_free(ports);
    }
    return list;
}

void AudioEngine::setInputGain(float db) {
    float gain = std::pow(10.0f, db / 20.0f);
    m_inputGain.store(gain, std::memory_order_relaxed);
}

void AudioEngine::setOutputGain(float db) {
    float gain = std::pow(10.0f, db / 20.0f);
    m_outputGain.store(gain, std::memory_order_relaxed);
}

float AudioEngine::getInputGainDB() const {
    float gain = m_inputGain.load(std::memory_order_relaxed);
    return 20.0f * std::log10(std::max(0.0001f, gain));
}

float AudioEngine::getOutputGainDB() const {
    float gain = m_outputGain.load(std::memory_order_relaxed);
    return 20.0f * std::log10(std::max(0.0001f, gain));
}
