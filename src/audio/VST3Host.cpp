#include "VST3Host.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <dlfcn.h>
#include <cstring>
#include <unordered_map>
#include <QTimer>
#include <QSocketNotifier>
#include <fstream>
#include <QLibrary>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/ibstream.h"

// Instantiate core iids
#include "pluginterfaces/base/coreiids.cpp"
#include "pluginterfaces/base/funknown.cpp"

namespace Steinberg {
namespace Vst {
    const FUID IComponent::iid(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
    const FUID IEditController::iid(0xDCD7BBE3, 0x7742448D, 0xA874AACC, 0x979C759E);
    const FUID IAudioProcessor::iid(0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D);
    const FUID IParameterChanges::iid(0xA47E0F7F, 0x1A284F45, 0x93306CC6, 0x89D91D0E);
    const FUID IParamValueQueue::iid(0x0126BCA8, 0x4E7A4A9C, 0xA99B774E, 0x0F0AE07A);
    const FUID IHostApplication::iid(0x58E595CC, 0xDB2D4969, 0x8B6AAF8C, 0x36A664E5);
    const FUID IConnectionPoint::iid(0x70A4156F, 0x6E6E4026, 0x989148BF, 0xAA60D8D1);
    const FUID IComponentHandler::iid(0x93A0BEA3, 0x0BD045DB, 0x8E890B0C, 0xC1E46AC6);
}
const FUID IPlugView::iid(0x5BC32507, 0xD06049EA, 0xA6151B52, 0x2B755B29);
const FUID IPlugFrame::iid(0x367FAF01, 0xAFA94693, 0x8D4DA2A0, 0xED0882A3);
}

bool iidsMatch(const Steinberg::TUID iid1, const Steinberg::FUID& iid2) {
    if (std::memcmp(iid1, iid2.toTUID(), 16) == 0) return true;
    unsigned char swapped[16];
    std::memcpy(swapped, iid2.toTUID(), 16);
    std::swap(swapped[0], swapped[3]);
    std::swap(swapped[1], swapped[2]);
    std::swap(swapped[4], swapped[5]);
    std::swap(swapped[6], swapped[7]);
    return (std::memcmp(iid1, swapped, 16) == 0);
}

class MemoryStream : public Steinberg::IBStream {
public:
    MemoryStream() : m_pos(0), m_refCount(1) {}
    virtual ~MemoryStream() = default;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (iidsMatch(iid, Steinberg::IBStream::iid) ||
            iidsMatch(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesRead) override {
        if (m_pos >= (Steinberg::int64)m_data.size()) {
            if (numBytesRead) *numBytesRead = 0;
            return Steinberg::kResultOk;
        }
        Steinberg::int32 toRead = std::min((Steinberg::int64)numBytes, (Steinberg::int64)m_data.size() - m_pos);
        std::memcpy(buffer, m_data.data() + m_pos, toRead);
        m_pos += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesWritten) override {
        if (m_pos + numBytes > (Steinberg::int64)m_data.size()) {
            m_data.resize(m_pos + numBytes);
        }
        std::memcpy(m_data.data() + m_pos, buffer, numBytes);
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode, Steinberg::int64* result = nullptr) override {
        if (mode == kIBSeekSet) {
            m_pos = pos;
        } else if (mode == kIBSeekCur) {
            m_pos += pos;
        } else if (mode == kIBSeekEnd) {
            m_pos = (Steinberg::int64)m_data.size() + pos;
        }
        if (m_pos < 0) m_pos = 0;
        if (result) *result = m_pos;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override {
        if (pos) *pos = m_pos;
        return Steinberg::kResultOk;
    }

private:
    std::vector<char> m_data;
    Steinberg::int64 m_pos;
    std::atomic<uint32_t> m_refCount;
};

class QtRunLoopHelper : public QObject {
public:
    explicit QtRunLoopHelper(QObject* parent = nullptr) : QObject(parent) {}
    ~QtRunLoopHelper() override {
        for (auto& pair : m_timers) {
            pair.second->stop();
            delete pair.second;
        }
        for (auto& pair : m_notifiers) {
            pair.second->setEnabled(false);
            delete pair.second;
        }
    }

    void registerTimer(Steinberg::Linux::ITimerHandler* handler, Steinberg::Linux::TimerInterval ms) {
        unregisterTimer(handler);
        QTimer* timer = new QTimer(this);
        Steinberg::Linux::TimerInterval clampedMs = ms;
        if (clampedMs < 15) clampedMs = 15;
        timer->setInterval(clampedMs);
        QObject::connect(timer, &QTimer::timeout, [handler]() {
            handler->onTimer();
        });
        m_timers[handler] = timer;
        timer->start();
    }

    void unregisterTimer(Steinberg::Linux::ITimerHandler* handler) {
        auto it = m_timers.find(handler);
        if (it != m_timers.end()) {
            it->second->stop();
            delete it->second;
            m_timers.erase(it);
        }
    }

    void registerEventHandler(Steinberg::Linux::IEventHandler* handler, Steinberg::Linux::FileDescriptor fd) {
        unregisterEventHandler(handler);
        QSocketNotifier* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        QObject::connect(notifier, &QSocketNotifier::activated, [handler, fd]() {
            handler->onFDIsSet(fd);
        });
        m_notifiers[handler] = notifier;
    }

    void unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) {
        auto it = m_notifiers.find(handler);
        if (it != m_notifiers.end()) {
            it->second->setEnabled(false);
            delete it->second;
            m_notifiers.erase(it);
        }
    }

private:
    std::unordered_map<Steinberg::Linux::ITimerHandler*, QTimer*> m_timers;
    std::unordered_map<Steinberg::Linux::IEventHandler*, QSocketNotifier*> m_notifiers;
};

class ComponentHandler : public Steinberg::Vst::IComponentHandler,
                         public Steinberg::Vst::IComponentHandler2,
                         public Steinberg::IPlugFrame {
public:
    ComponentHandler(VST3PluginNode* node) : m_node(node), m_refCount(1) {}
    virtual ~ComponentHandler() = default;
    
    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        char buf[64];
        sprintf(buf, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            (unsigned char)iid[0], (unsigned char)iid[1], (unsigned char)iid[2], (unsigned char)iid[3],
            (unsigned char)iid[4], (unsigned char)iid[5], (unsigned char)iid[6], (unsigned char)iid[7],
            (unsigned char)iid[8], (unsigned char)iid[9], (unsigned char)iid[10], (unsigned char)iid[11],
            (unsigned char)iid[12], (unsigned char)iid[13], (unsigned char)iid[14], (unsigned char)iid[15]);
        std::cout << "VST3Host: ComponentHandler::queryInterface queried IID: " << buf << std::endl;

        if (iidsMatch(iid, Steinberg::Vst::IComponentHandler::iid) ||
            iidsMatch(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        static const unsigned char straight[16] = { 0xF0, 0x40, 0xB4, 0xB3, 0xA3, 0x60, 0x45, 0xEC, 0xAB, 0xCD, 0xC0, 0x45, 0xB4, 0xD5, 0xA2, 0xCC };
        static const unsigned char swapped[16] = { 0xB3, 0xB4, 0x40, 0xF0, 0x60, 0xA3, 0xEC, 0x45, 0xAB, 0xCD, 0xC0, 0x45, 0xB4, 0xD5, 0xA2, 0xCC };
        if (std::memcmp(iid, straight, 16) == 0 || std::memcmp(iid, swapped, 16) == 0) {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            return Steinberg::kResultOk;
        }
        if (iidsMatch(iid, Steinberg::IPlugFrame::iid)) {
            addRef();
            *obj = static_cast<Steinberg::IPlugFrame*>(this);
            return Steinberg::kResultOk;
        }
        static const unsigned char runLoopIID[16] = { 0x18, 0xC3, 0x53, 0x66, 0x97, 0x76, 0x4F, 0x1A, 0x9C, 0x5B, 0x83, 0x85, 0x7A, 0x87, 0x13, 0x89 };
        static const unsigned char runLoopIIDSwapped[16] = { 0x66, 0x53, 0xC3, 0x18, 0x1A, 0x4F, 0x76, 0x97, 0x9C, 0x5B, 0x83, 0x85, 0x7A, 0x87, 0x13, 0x89 };
        if (std::memcmp(iid, runLoopIID, 16) == 0 || std::memcmp(iid, runLoopIIDSwapped, 16) == 0) {
            if (m_node && m_node->getHostAppUnknown()) {
                return ((Steinberg::Vst::IHostApplication*)m_node->getHostAppUnknown())->queryInterface(iid, obj);
            }
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    
    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) override {
        if (m_node) {
            for (auto& port : m_node->getControlPorts()) {
                if (port.index == id) {
                    port.value = valueNormalized;
                    break;
                }
            }
        }
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        return Steinberg::kResultOk;
    }

    // IComponentHandler2
    Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString name) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API startGroupEdit() override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API finishGroupEdit() override {
        return Steinberg::kResultOk;
    }

    // IPlugFrame
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override {
        return Steinberg::kResultOk;
    }

private:
    VST3PluginNode* m_node;
    std::atomic<uint32_t> m_refCount;
};

class HostApplication : public Steinberg::Vst::IHostApplication,
                        public Steinberg::Linux::IRunLoop {
public:
    HostApplication() : m_refCount(1) {
        m_helper = new QtRunLoopHelper();
    }
    virtual ~HostApplication() {
        delete m_helper;
    }
    
    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        char buf[64];
        sprintf(buf, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            (unsigned char)iid[0], (unsigned char)iid[1], (unsigned char)iid[2], (unsigned char)iid[3],
            (unsigned char)iid[4], (unsigned char)iid[5], (unsigned char)iid[6], (unsigned char)iid[7],
            (unsigned char)iid[8], (unsigned char)iid[9], (unsigned char)iid[10], (unsigned char)iid[11],
            (unsigned char)iid[12], (unsigned char)iid[13], (unsigned char)iid[14], (unsigned char)iid[15]);
        std::cout << "VST3Host: HostApplication::queryInterface queried IID: " << buf << std::endl;

        if (iidsMatch(iid, Steinberg::Vst::IHostApplication::iid) ||
            iidsMatch(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = static_cast<Steinberg::Vst::IHostApplication*>(this);
            return Steinberg::kResultOk;
        }
        static const unsigned char runLoopIID[16] = { 0x18, 0xC3, 0x53, 0x66, 0x97, 0x76, 0x4F, 0x1A, 0x9C, 0x5B, 0x83, 0x85, 0x7A, 0x87, 0x13, 0x89 };
        static const unsigned char runLoopIIDSwapped[16] = { 0x66, 0x53, 0xC3, 0x18, 0x1A, 0x4F, 0x76, 0x97, 0x9C, 0x5B, 0x83, 0x85, 0x7A, 0x87, 0x13, 0x89 };
        if (std::memcmp(iid, runLoopIID, 16) == 0 || std::memcmp(iid, runLoopIIDSwapped, 16) == 0) {
            addRef();
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    
    // IHostApplication
    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override {
        std::string hostName = "PedalBoard";
        for (size_t i = 0; i < 127 && i < hostName.size(); ++i) {
            name[i] = hostName[i];
        }
        name[std::min((size_t)127, hostName.size())] = 0;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNotImplemented;
    }

    // IRunLoop
    Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler, Steinberg::Linux::FileDescriptor fd) override {
        if (handler && m_helper) {
            m_helper->registerEventHandler(handler, fd);
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override {
        if (handler && m_helper) {
            m_helper->unregisterEventHandler(handler);
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler, Steinberg::Linux::TimerInterval milliseconds) override {
        if (handler && m_helper) {
            m_helper->registerTimer(handler, milliseconds);
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override {
        if (handler && m_helper) {
            m_helper->unregisterTimer(handler);
            return Steinberg::kResultOk;
        }
        return Steinberg::kResultFalse;
    }

private:
    std::atomic<uint32_t> m_refCount;
    QtRunLoopHelper* m_helper = nullptr;
};

class ParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ParamValueQueue(Steinberg::Vst::ParamID id) : m_id(id), m_refCount(1) {}
    virtual ~ParamValueQueue() = default;
    
    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (iidsMatch(iid, Steinberg::Vst::IParamValueQueue::iid) ||
            iidsMatch(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    
    // IParamValueQueue
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return m_id; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return m_points.size(); }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= (int)m_points.size()) return Steinberg::kInvalidArgument;
        sampleOffset = m_points[index].offset;
        value = m_points[index].value;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override {
        Point p = { sampleOffset, value };
        m_points.push_back(p);
        index = m_points.size() - 1;
        return Steinberg::kResultOk;
    }
    
    void clear() { m_points.clear(); }

private:
    struct Point {
        Steinberg::int32 offset;
        Steinberg::Vst::ParamValue value;
    };
    Steinberg::Vst::ParamID m_id;
    std::atomic<uint32_t> m_refCount;
    std::vector<Point> m_points;
};

class ParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    ParameterChanges() : m_refCount(1) {}
    virtual ~ParameterChanges() {
        for (auto q : m_queues) q->release();
    }
    
    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (iidsMatch(iid, Steinberg::Vst::IParameterChanges::iid) ||
            iidsMatch(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        Steinberg::uint32 r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    
    // IParameterChanges
    Steinberg::int32 PLUGIN_API getParameterCount() override { return m_queues.size(); }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= (int)m_queues.size()) return nullptr;
        return m_queues[index];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override {
        for (size_t i = 0; i < m_queues.size(); ++i) {
            if (m_queues[i]->getParameterId() == id) {
                index = i;
                return m_queues[i];
            }
        }
        ParamValueQueue* q = new ParamValueQueue(id);
        m_queues.push_back(q);
        index = m_queues.size() - 1;
        return q;
    }
    
    void clear() {
        for (auto q : m_queues) q->clear();
    }

private:
    std::atomic<uint32_t> m_refCount;
    std::vector<ParamValueQueue*> m_queues;
};

static void initializePluginGtkVersion(const std::string& soPath) {
    std::ifstream file(soPath, std::ios::binary);
    if (!file.is_open()) return;
    
    enum GtkVersion {
        GTK_NONE,
        GTK_2,
        GTK_3
    } version = GTK_NONE;
    
    char buffer[8192];
    std::string content;
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        content.append(buffer, file.gcount());
        if (content.find("libgtk-3.so") != std::string::npos) {
            version = GTK_3;
            break;
        }
        if (content.find("libgtk-x11-2.0.so") != std::string::npos ||
            content.find("libglibmm-2.4.so") != std::string::npos) {
            version = GTK_2;
            break;
        }
        if (content.size() > 16384) {
            content = content.substr(content.size() - 1024);
        }
    }
    file.close();
    
    static bool gtk2Initialized = false;
    static bool gtk3Initialized = false;
    
    if (version == GTK_2) {
        if (gtk3Initialized) {
            std::cerr << "VST3Host: WARNING: GTK 3 is already initialized in-process. Loading GTK 2 might cause a crash." << std::endl;
        }
        if (!gtk2Initialized) {
            QLibrary gtk2("libgtk-x11-2.0.so.0");
            if (gtk2.load()) {
                using GtkInitCheck = int (*)(int*, char***);
                auto initCheck = reinterpret_cast<GtkInitCheck>(gtk2.resolve("gtk_init_check"));
                if (initCheck) {
                    int argc = 0;
                    char** argv = nullptr;
                    if (initCheck(&argc, &argv)) {
                        std::cout << "VST3Host: Successfully initialized GTK 2 in-process for " << soPath << std::endl;
                        gtk2Initialized = true;
                    }
                }
            }
        }
    } else if (version == GTK_3) {
        if (gtk2Initialized) {
            std::cerr << "VST3Host: WARNING: GTK 2 is already initialized in-process. Loading GTK 3 might cause a crash." << std::endl;
        }
        if (!gtk3Initialized) {
            QLibrary gtk3("libgtk-3.so.0");
            if (gtk3.load()) {
                using GtkInitCheck = int (*)(int*, char***);
                auto initCheck = reinterpret_cast<GtkInitCheck>(gtk3.resolve("gtk_init_check"));
                if (initCheck) {
                    int argc = 0;
                    char** argv = nullptr;
                    if (initCheck(&argc, &argv)) {
                        std::cout << "VST3Host: Successfully initialized GTK 3 in-process for " << soPath << std::endl;
                        gtk3Initialized = true;
                    }
                }
            }
        }
    } else {
        std::cout << "VST3Host: No GTK dependency detected for " << soPath << std::endl;
    }
}

VST3PluginNode::VST3PluginNode(const std::string& path) : m_path(path) {
    std::filesystem::path p(path);
    m_name = p.stem().string();
    std::cout << "VST3Host: Loading plugin \"" << m_name << "\" from path: " << path << std::endl;

    // Resolve the actual shared library (.so)
    std::string soPath = path;
    if (std::filesystem::is_directory(p)) {
        std::string stem = p.stem().string();
        std::filesystem::path testPath = p / "Contents" / "x86_64-linux" / (stem + ".so");
        if (std::filesystem::exists(testPath)) {
            soPath = testPath.string();
        } else {
            // fallback: find any .so
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(p)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".so") {
                        soPath = entry.path().string();
                        break;
                    }
                }
            } catch (...) {}
        }
    }

    std::cout << "VST3Host: Resolved library path: " << soPath << std::endl;
    
    // Dynamically initialize the matching GTK library version
    initializePluginGtkVersion(soPath);
    
    // Load the library
    m_libHandle = dlopen(soPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!m_libHandle) {
        std::cerr << "VST3Host: Failed to dlopen " << soPath << ": " << dlerror() << std::endl;
        return;
    }

    // Call VST3 ModuleEntry on Linux
    typedef bool (PLUGIN_API *ModuleEntryProc)(void*);
    ModuleEntryProc moduleEntry = (ModuleEntryProc)dlsym(m_libHandle, "ModuleEntry");
    if (moduleEntry) {
        std::cout << "VST3Host: Calling ModuleEntry..." << std::endl;
        if (!moduleEntry(m_libHandle)) {
            std::cerr << "VST3Host: ModuleEntry failed" << std::endl;
        }
    }

    typedef Steinberg::IPluginFactory* (PLUGIN_API *GetFactoryProc)();
    GetFactoryProc getFactory = (GetFactoryProc)dlsym(m_libHandle, "GetPluginFactory");
    if (!getFactory) {
        std::cerr << "VST3Host: GetPluginFactory not found in " << soPath << std::endl;
        return;
    }

    Steinberg::IPluginFactory* factory = getFactory();
    if (!factory) {
        std::cerr << "VST3Host: GetPluginFactory returned null" << std::endl;
        return;
    }

    // Search for audio effect class matching the plugin name
    Steinberg::PClassInfo info;
    std::cout << "VST3Host: Factory countClasses = " << factory->countClasses() << std::endl;
    for (int32_t i = 0; i < factory->countClasses(); ++i) {
        if (factory->getClassInfo(i, &info) == Steinberg::kResultOk) {
            std::cout << "  Class " << i << ": name=\"" << info.name << "\", category=\"" << info.category << "\", card=\"";
            for (int j = 0; j < 16; ++j) printf("%02X", (unsigned char)info.cid[j]);
            std::cout << "\"" << std::endl;
        }
    }

    Steinberg::TUID cid;
    bool found = false;
    
    std::string targetName = m_name;
    std::transform(targetName.begin(), targetName.end(), targetName.begin(), ::tolower);
    std::replace(targetName.begin(), targetName.end(), '_', ' ');
    std::replace(targetName.begin(), targetName.end(), '-', ' ');

    for (int32_t i = 0; i < factory->countClasses(); ++i) {
        if (factory->getClassInfo(i, &info) == Steinberg::kResultOk) {
            if (strcmp(info.category, "Audio Module Class") == 0) {
                std::string className = info.name;
                std::transform(className.begin(), className.end(), className.begin(), ::tolower);
                std::replace(className.begin(), className.end(), '_', ' ');
                std::replace(className.begin(), className.end(), '-', ' ');
                
                if (!found || className.find(targetName) != std::string::npos || targetName.find(className) != std::string::npos) {
                    memcpy(cid, info.cid, sizeof(Steinberg::TUID));
                    found = true;
                    if (className.find(targetName) != std::string::npos || targetName.find(className) != std::string::npos) {
                        // Strong match found
                        break;
                    }
                }
            }
        }
    }

    if (!found) {
        std::cerr << "VST3Host: No Audio Module Class found in factory." << std::endl;
        return;
    }

    // Generate both straight and COM-swapped string representations for CID
    char cidStraight[33] = {0};
    char cidSwapped[33] = {0};
    for (int i = 0; i < 16; ++i) sprintf(cidStraight + i * 2, "%02X", (unsigned char)cid[i]);
    
    unsigned char cidCopy[16];
    memcpy(cidCopy, cid, 16);
    std::swap(cidCopy[0], cidCopy[3]);
    std::swap(cidCopy[1], cidCopy[2]);
    std::swap(cidCopy[4], cidCopy[5]);
    std::swap(cidCopy[6], cidCopy[7]);
    for (int i = 0; i < 16; ++i) sprintf(cidSwapped + i * 2, "%02X", cidCopy[i]);

    // Generate both straight and COM-swapped string representations for IID
    char iidStraight[33] = {0};
    char iidSwapped[33] = {0};
    const unsigned char* iidBytes = (const unsigned char*)Steinberg::Vst::IComponent::iid.toTUID();
    for (int i = 0; i < 16; ++i) sprintf(iidStraight + i * 2, "%02X", iidBytes[i]);
    
    unsigned char iidCopy[16];
    memcpy(iidCopy, iidBytes, 16);
    std::swap(iidCopy[0], iidCopy[3]);
    std::swap(iidCopy[1], iidCopy[2]);
    std::swap(iidCopy[4], iidCopy[5]);
    std::swap(iidCopy[6], iidCopy[7]);
    for (int i = 0; i < 16; ++i) sprintf(iidSwapped + i * 2, "%02X", iidCopy[i]);

    struct Combo {
        const char* cidStr;
        const char* iidStr;
        std::string name;
    } combos[] = {
        { cidStraight, iidStraight, "cidStraight, iidStraight" },
        { cidStraight, iidSwapped, "cidStraight, iidSwapped" },
        { cidSwapped, iidStraight, "cidSwapped, iidStraight" },
        { cidSwapped, iidSwapped, "cidSwapped, iidSwapped" }
    };

    Steinberg::FUnknown* instance = nullptr;
    Steinberg::tresult res = Steinberg::kResultFalse;
    bool ok = false;

    // Try binary combos first (Carla/C-style ABI representation)
    Steinberg::TUID cidSwappedBytes;
    memcpy(cidSwappedBytes, cid, 16);
    std::swap(cidSwappedBytes[0], cidSwappedBytes[3]);
    std::swap(cidSwappedBytes[1], cidSwappedBytes[2]);
    std::swap(cidSwappedBytes[4], cidSwappedBytes[5]);
    std::swap(cidSwappedBytes[6], cidSwappedBytes[7]);

    Steinberg::TUID iidSwappedBytes;
    memcpy(iidSwappedBytes, iidBytes, 16);
    std::swap(iidSwappedBytes[0], iidSwappedBytes[3]);
    std::swap(iidSwappedBytes[1], iidSwappedBytes[2]);
    std::swap(iidSwappedBytes[4], iidSwappedBytes[5]);
    std::swap(iidSwappedBytes[6], iidSwappedBytes[7]);

    struct BinaryCombo {
        const char* cidData;
        const char* iidData;
        std::string name;
    } binaryCombos[] = {
        { cid, (const char*)iidBytes, "cidStraight, iidStraight" },
        { cid, (const char*)iidSwappedBytes, "cidStraight, iidSwapped" },
        { (const char*)cidSwappedBytes, (const char*)iidBytes, "cidSwapped, iidStraight" },
        { (const char*)cidSwappedBytes, (const char*)iidSwappedBytes, "cidSwapped, iidSwapped" }
    };
    
    for (const auto& combo : binaryCombos) {
        res = factory->createInstance(combo.cidData, combo.iidData, (void**)&instance);
        if (res == Steinberg::kResultOk && instance) {
            std::cout << "VST3Host: Successfully created instance using binary combo: " << combo.name << std::endl;
            ok = true;
            break;
        }
    }

    if (!ok) {
        // Fallback to string combos (C++ COM-style representation)
        std::vector<std::string> cidCandidates = { cidStraight, cidSwapped };
        std::string cidStraightLower = cidStraight;
        std::transform(cidStraightLower.begin(), cidStraightLower.end(), cidStraightLower.begin(), ::tolower);
        cidCandidates.push_back(cidStraightLower);
        std::string cidSwappedLower = cidSwapped;
        std::transform(cidSwappedLower.begin(), cidSwappedLower.end(), cidSwappedLower.begin(), ::tolower);
        cidCandidates.push_back(cidSwappedLower);

        std::vector<std::string> iidCandidates = { iidStraight, iidSwapped };
        std::string iidStraightLower = iidStraight;
        std::transform(iidStraightLower.begin(), iidStraightLower.end(), iidStraightLower.begin(), ::tolower);
        iidCandidates.push_back(iidStraightLower);
        std::string iidSwappedLower = iidSwapped;
        std::transform(iidSwappedLower.begin(), iidSwappedLower.end(), iidSwappedLower.begin(), ::tolower);
        iidCandidates.push_back(iidSwappedLower);

        for (const auto& cStr : cidCandidates) {
            for (const auto& iStr : iidCandidates) {
                res = factory->createInstance(cStr.c_str(), iStr.c_str(), (void**)&instance);
                if (res == Steinberg::kResultOk && instance) {
                    std::cout << "VST3Host: Successfully created instance using string CID=" << cStr << ", IID=" << iStr << std::endl;
                    ok = true;
                    break;
                }
            }
            if (ok) break;
        }
    }

    if (!instance) {
        std::cerr << "VST3Host: Failed to create component instance. Error: 0x" << std::hex << res << std::endl;
        return;
    }

    m_component = instance;

    m_hostApp = new HostApplication();
    m_compHandler = new ComponentHandler(this);
    Steinberg::Vst::IComponent* component = (Steinberg::Vst::IComponent*)m_component;

    // Get Edit Controller (try querying component first, then fall back to class factory)
    Steinberg::Vst::IEditController* controllerFromComp = nullptr;
    Steinberg::tresult queryCtrlRes = instance->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&controllerFromComp);
    if (queryCtrlRes != Steinberg::kResultOk || !controllerFromComp) {
        char swappedIID[16];
        std::memcpy(swappedIID, Steinberg::Vst::IEditController::iid.toTUID(), 16);
        std::swap(swappedIID[0], swappedIID[3]);
        std::swap(swappedIID[1], swappedIID[2]);
        std::swap(swappedIID[4], swappedIID[5]);
        std::swap(swappedIID[6], swappedIID[7]);
        queryCtrlRes = instance->queryInterface(swappedIID, (void**)&controllerFromComp);
    }

    bool hasController = false;
    if (queryCtrlRes == Steinberg::kResultOk && controllerFromComp) {
        m_controller = controllerFromComp;
        std::cout << "VST3Host: Successfully retrieved controller from component via queryInterface." << std::endl;
        hasController = true;
    } else {
        Steinberg::TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == Steinberg::kResultOk) {
            char ctrlStraight[33] = {0};
            char ctrlSwapped[33] = {0};
            for (int i = 0; i < 16; ++i) sprintf(ctrlStraight + i * 2, "%02X", (unsigned char)controllerCID[i]);
            
            unsigned char ctrlCopy[16];
            memcpy(ctrlCopy, controllerCID, 16);
            std::swap(ctrlCopy[0], ctrlCopy[3]);
            std::swap(ctrlCopy[1], ctrlCopy[2]);
            std::swap(ctrlCopy[4], ctrlCopy[5]);
            std::swap(ctrlCopy[6], ctrlCopy[7]);
            for (int i = 0; i < 16; ++i) sprintf(ctrlSwapped + i * 2, "%02X", ctrlCopy[i]);

            char ctrlIIDStraight[33] = {0};
            char ctrlIIDSwapped[33] = {0};
            const unsigned char* ctrlIIDBytes = (const unsigned char*)Steinberg::Vst::IEditController::iid.toTUID();
            for (int i = 0; i < 16; ++i) sprintf(ctrlIIDStraight + i * 2, "%02X", ctrlIIDBytes[i]);
            
            unsigned char ctrlIIDCopy[16];
            memcpy(ctrlIIDCopy, ctrlIIDBytes, 16);
            std::swap(ctrlIIDCopy[0], ctrlIIDCopy[3]);
            std::swap(ctrlIIDCopy[1], ctrlIIDCopy[2]);
            std::swap(ctrlIIDCopy[4], ctrlIIDCopy[5]);
            std::swap(ctrlIIDCopy[6], ctrlIIDCopy[7]);
            for (int i = 0; i < 16; ++i) sprintf(ctrlIIDSwapped + i * 2, "%02X", ctrlIIDCopy[i]);

            Steinberg::FUnknown* controllerInstance = nullptr;
            bool ctrlOk = false;

            // Try binary combos first
            Steinberg::TUID ctrlSwappedBytes;
            memcpy(ctrlSwappedBytes, controllerCID, 16);
            std::swap(ctrlSwappedBytes[0], ctrlSwappedBytes[3]);
            std::swap(ctrlSwappedBytes[1], ctrlSwappedBytes[2]);
            std::swap(ctrlSwappedBytes[4], ctrlSwappedBytes[5]);
            std::swap(ctrlSwappedBytes[6], ctrlSwappedBytes[7]);

            Steinberg::TUID ctrlIIDSwappedBytes;
            memcpy(ctrlIIDSwappedBytes, ctrlIIDBytes, 16);
            std::swap(ctrlIIDSwappedBytes[0], ctrlIIDSwappedBytes[3]);
            std::swap(ctrlIIDSwappedBytes[1], ctrlIIDSwappedBytes[2]);
            std::swap(ctrlIIDSwappedBytes[4], ctrlIIDSwappedBytes[5]);
            std::swap(ctrlIIDSwappedBytes[6], ctrlIIDSwappedBytes[7]);

            BinaryCombo ctrlBinaryCombos[] = {
                { (const char*)controllerCID, (const char*)ctrlIIDBytes, "ctrlStraight, ctrlIIDStraight" },
                { (const char*)controllerCID, (const char*)ctrlIIDSwappedBytes, "ctrlStraight, ctrlIIDSwapped" },
                { (const char*)ctrlSwappedBytes, (const char*)ctrlIIDBytes, "ctrlSwapped, ctrlIIDStraight" },
                { (const char*)ctrlSwappedBytes, (const char*)ctrlIIDSwappedBytes, "ctrlSwapped, ctrlIIDSwapped" }
            };

            for (const auto& combo : ctrlBinaryCombos) {
                if (factory->createInstance(combo.cidData, combo.iidData, (void**)&controllerInstance) == Steinberg::kResultOk && controllerInstance) {
                    m_controller = controllerInstance;
                    std::cout << "VST3Host: Successfully created controller using binary combo: " << combo.name << std::endl;
                    ctrlOk = true;
                    hasController = true;
                    break;
                }
            }

            if (!ctrlOk) {
                // Fallback to string combos
                std::vector<std::string> ctrlCidCandidates = { ctrlStraight, ctrlSwapped };
                std::string ctrlStraightLower = ctrlStraight;
                std::transform(ctrlStraightLower.begin(), ctrlStraightLower.end(), ctrlStraightLower.begin(), ::tolower);
                ctrlCidCandidates.push_back(ctrlStraightLower);
                std::string ctrlSwappedLower = ctrlSwapped;
                std::transform(ctrlSwappedLower.begin(), ctrlSwappedLower.end(), ctrlSwappedLower.begin(), ::tolower);
                ctrlCidCandidates.push_back(ctrlSwappedLower);

                std::vector<std::string> ctrlIidCandidates = { ctrlIIDStraight, ctrlIIDSwapped };
                std::string ctrlIidStraightLower = ctrlIIDStraight;
                std::transform(ctrlIidStraightLower.begin(), ctrlIidStraightLower.end(), ctrlIidStraightLower.begin(), ::tolower);
                ctrlIidCandidates.push_back(ctrlIidStraightLower);
                std::string ctrlIidSwappedLower = ctrlIIDSwapped;
                std::transform(ctrlIidSwappedLower.begin(), ctrlIidSwappedLower.end(), ctrlIidSwappedLower.begin(), ::tolower);
                ctrlIidCandidates.push_back(ctrlIidSwappedLower);

                for (const auto& cStr : ctrlCidCandidates) {
                    for (const auto& iStr : ctrlIidCandidates) {
                        if (factory->createInstance(cStr.c_str(), iStr.c_str(), (void**)&controllerInstance) == Steinberg::kResultOk && controllerInstance) {
                            m_controller = controllerInstance;
                            std::cout << "VST3Host: Successfully created controller using string CID=" << cStr << ", IID=" << iStr << std::endl;
                            ctrlOk = true;
                            hasController = true;
                            break;
                        }
                    }
                    if (ctrlOk) break;
                }
            }
        }
    }

    // Now initialize both
    Steinberg::tresult compInitRes = component->initialize(static_cast<Steinberg::Vst::IHostApplication*>(static_cast<HostApplication*>(m_hostApp)));
    std::cout << "VST3Host: component->initialize returned res=0x" << std::hex << compInitRes << std::endl;

    if (hasController && m_controller) {
        Steinberg::Vst::IEditController* controller = (Steinberg::Vst::IEditController*)m_controller;
        Steinberg::tresult ctrlInitRes = controller->initialize(static_cast<Steinberg::Vst::IHostApplication*>(static_cast<HostApplication*>(m_hostApp)));
        Steinberg::tresult ctrlHandlerRes = controller->setComponentHandler((ComponentHandler*)m_compHandler);
        std::cout << "VST3Host: controller->initialize returned res=0x" << std::hex << ctrlInitRes << std::endl;
        std::cout << "VST3Host: controller->setComponentHandler returned res=0x" << std::hex << ctrlHandlerRes << std::endl;
    }

    // Connect Component and Edit Controller connection points
    Steinberg::Vst::IConnectionPoint* compCP = nullptr;
    Steinberg::Vst::IConnectionPoint* ctrlCP = nullptr;
    if (m_component && m_controller) {
        Steinberg::tresult compCPRes = ((Steinberg::FUnknown*)m_component)->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&compCP);
        if (compCPRes != Steinberg::kResultOk || !compCP) {
            char swapped[16];
            std::memcpy(swapped, Steinberg::Vst::IConnectionPoint::iid.toTUID(), 16);
            std::swap(swapped[0], swapped[3]);
            std::swap(swapped[1], swapped[2]);
            std::swap(swapped[4], swapped[5]);
            std::swap(swapped[6], swapped[7]);
            compCPRes = ((Steinberg::FUnknown*)m_component)->queryInterface(swapped, (void**)&compCP);
        }
        
        Steinberg::tresult ctrlCPRes = ((Steinberg::FUnknown*)m_controller)->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&ctrlCP);
        if (ctrlCPRes != Steinberg::kResultOk || !ctrlCP) {
            char swapped[16];
            std::memcpy(swapped, Steinberg::Vst::IConnectionPoint::iid.toTUID(), 16);
            std::swap(swapped[0], swapped[3]);
            std::swap(swapped[1], swapped[2]);
            std::swap(swapped[4], swapped[5]);
            std::swap(swapped[6], swapped[7]);
            ctrlCPRes = ((Steinberg::FUnknown*)m_controller)->queryInterface(swapped, (void**)&ctrlCP);
        }
        
        std::cout << "VST3Host: Query IConnectionPoint on component returned res=0x" << std::hex << compCPRes << ", ptr=" << compCP << std::endl;
        std::cout << "VST3Host: Query IConnectionPoint on controller returned res=0x" << std::hex << ctrlCPRes << ", ptr=" << ctrlCP << std::endl;
        
        if (compCP && ctrlCP) {
            Steinberg::tresult compConnRes = compCP->connect(ctrlCP);
            Steinberg::tresult ctrlConnRes = ctrlCP->connect(compCP);
            std::cout << "VST3Host: Connect compCP to ctrlCP returned res=0x" << std::hex << compConnRes << std::endl;
            std::cout << "VST3Host: Connect ctrlCP to compCP returned res=0x" << std::hex << ctrlConnRes << std::endl;
        }
        if (compCP) compCP->release();
        if (ctrlCP) ctrlCP->release();
    }

    // Synchronize state between Component and Controller (required by VST3 lifecycle)
    if (m_component && m_controller) {
        Steinberg::Vst::IComponent* component = (Steinberg::Vst::IComponent*)m_component;
        Steinberg::Vst::IEditController* controller = (Steinberg::Vst::IEditController*)m_controller;
        MemoryStream* stream = new MemoryStream();
        if (component->getState(stream) == Steinberg::kResultOk) {
            stream->seek(0, Steinberg::IBStream::kIBSeekSet);
            Steinberg::tresult syncRes = controller->setComponentState(stream);
            std::cout << "VST3Host: Synchronized state between component and controller. setComponentState returned res=0x" << std::hex << syncRes << std::endl;
        }
        stream->release();
    }

    // Query IAudioProcessor and dynamic ports
    if (m_component) {
        Steinberg::Vst::IComponent* component = (Steinberg::Vst::IComponent*)m_component;
        Steinberg::Vst::IAudioProcessor* processor = nullptr;
        if (component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processor) == Steinberg::kResultOk) {
            m_processor = processor;
        }

        int numInputs = 0;
        int numOutputs = 0;
        int inputBuses = component->getBusCount(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kInput);
        for (int i = 0; i < inputBuses; ++i) {
            Steinberg::Vst::BusInfo info;
            if (component->getBusInfo(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kInput, i, info) == Steinberg::kResultOk) {
                if (info.busType == Steinberg::Vst::BusTypes::kMain) {
                    numInputs += info.channelCount;
                }
            }
        }
        int outputBuses = component->getBusCount(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kOutput);
        for (int i = 0; i < outputBuses; ++i) {
            Steinberg::Vst::BusInfo info;
            if (component->getBusInfo(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kOutput, i, info) == Steinberg::kResultOk) {
                if (info.busType == Steinberg::Vst::BusTypes::kMain) {
                    numOutputs += info.channelCount;
                }
            }
        }
        if (numInputs == 0) numInputs = 2;
        if (numOutputs == 0) numOutputs = 2;

        std::cout << "VST3Host: Detected " << numInputs << " inputs and " << numOutputs << " outputs." << std::endl;

        for (int i = 0; i < numInputs; ++i) {
            AudioPort port;
            port.name = "Input " + std::to_string(i + 1);
            port.isInput = true;
            port.isStereo = (numInputs == 2);
            port.channelIdx = i;
            m_ports.push_back(port);
        }
        for (int i = 0; i < numOutputs; ++i) {
            AudioPort port;
            port.name = "Output " + std::to_string(i + 1);
            port.isInput = false;
            port.isStereo = (numOutputs == 2);
            port.channelIdx = i;
            m_ports.push_back(port);
        }
    }

    // Query dynamic control ports
    if (m_controller) {
        Steinberg::Vst::IEditController* controller = (Steinberg::Vst::IEditController*)m_controller;
        int paramCount = controller->getParameterCount();
        std::cout << "VST3Host: Detected " << paramCount << " parameters." << std::endl;
        for (int i = 0; i < paramCount; ++i) {
            Steinberg::Vst::ParameterInfo info;
            if (controller->getParameterInfo(i, info) == Steinberg::kResultOk) {
                std::string name = "";
                for (int j = 0; j < 128 && info.title[j] != 0; ++j) {
                    name += (char)info.title[j];
                }
                
                ControlPort port;
                port.name = name;
                port.index = info.id;
                port.minVal = 0.0f;
                port.maxVal = 1.0f;
                port.defaultVal = info.defaultNormalizedValue;
                port.value = info.defaultNormalizedValue;
                port.isOutput = (info.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly) != 0;
                
                m_controlPorts.push_back(port);
                m_lastParamValues.push_back(info.defaultNormalizedValue);
            }
        }
    }

    if (m_controlPorts.empty()) {
        ControlPort ctrl1;
        ctrl1.name = "Gain";
        ctrl1.index = 0;
        ctrl1.minVal = 0.0f;
        ctrl1.maxVal = 1.0f;
        ctrl1.defaultVal = 0.5f;
        ctrl1.value = 0.5f;
        ctrl1.isOutput = false;
        m_controlPorts.push_back(ctrl1);
        m_lastParamValues.push_back(0.5f);

        ControlPort ctrl2;
        ctrl2.name = "Mix";
        ctrl2.index = 1;
        ctrl2.minVal = 0.0f;
        ctrl2.maxVal = 1.0f;
        ctrl2.defaultVal = 1.0f;
        ctrl2.value = 1.0f;
        ctrl2.isOutput = false;
        m_controlPorts.push_back(ctrl2);
        m_lastParamValues.push_back(1.0f);
    }

    m_paramChanges = new ParameterChanges();
    m_outputParamChanges = new ParameterChanges();

}

VST3PluginNode::~VST3PluginNode() {
    // Disconnect connection points
    Steinberg::Vst::IConnectionPoint* compCP = nullptr;
    Steinberg::Vst::IConnectionPoint* ctrlCP = nullptr;
    if (m_component && m_controller) {
        Steinberg::tresult compCPRes = ((Steinberg::FUnknown*)m_component)->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&compCP);
        if (compCPRes != Steinberg::kResultOk || !compCP) {
            char swapped[16];
            std::memcpy(swapped, Steinberg::Vst::IConnectionPoint::iid.toTUID(), 16);
            std::swap(swapped[0], swapped[3]);
            std::swap(swapped[1], swapped[2]);
            std::swap(swapped[4], swapped[5]);
            std::swap(swapped[6], swapped[7]);
            compCPRes = ((Steinberg::FUnknown*)m_component)->queryInterface(swapped, (void**)&compCP);
        }
        
        Steinberg::tresult ctrlCPRes = ((Steinberg::FUnknown*)m_controller)->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&ctrlCP);
        if (ctrlCPRes != Steinberg::kResultOk || !ctrlCP) {
            char swapped[16];
            std::memcpy(swapped, Steinberg::Vst::IConnectionPoint::iid.toTUID(), 16);
            std::swap(swapped[0], swapped[3]);
            std::swap(swapped[1], swapped[2]);
            std::swap(swapped[4], swapped[5]);
            std::swap(swapped[6], swapped[7]);
            ctrlCPRes = ((Steinberg::FUnknown*)m_controller)->queryInterface(swapped, (void**)&ctrlCP);
        }
        
        if (compCP && ctrlCP) {
            compCP->disconnect(ctrlCP);
            ctrlCP->disconnect(compCP);
        }
        if (compCP) compCP->release();
        if (ctrlCP) ctrlCP->release();
    }

    if (m_plugView) {
        m_plugView->release();
    }
    if (m_controller) {
        Steinberg::Vst::IEditController* controller = (Steinberg::Vst::IEditController*)m_controller;
        controller->terminate();
        controller->release();
    }
    if (m_processor) {
        Steinberg::Vst::IAudioProcessor* processor = (Steinberg::Vst::IAudioProcessor*)m_processor;
        processor->setProcessing(false);
        processor->release();
    }
    if (m_component) {
        Steinberg::Vst::IComponent* component = (Steinberg::Vst::IComponent*)m_component;
        component->setActive(false);
        component->terminate();
        component->release();
    }
    if (m_paramChanges) {
        delete (ParameterChanges*)m_paramChanges;
    }
    if (m_outputParamChanges) {
        delete (ParameterChanges*)m_outputParamChanges;
    }
    if (m_hostApp) {
        HostApplication* hostApp = (HostApplication*)m_hostApp;
        hostApp->release();
    }
    if (m_compHandler) {
        ComponentHandler* handler = (ComponentHandler*)m_compHandler;
        handler->release();
    }
    if (m_libHandle) {
        typedef bool (PLUGIN_API *ModuleExitProc)();
        ModuleExitProc moduleExit = (ModuleExitProc)dlsym(m_libHandle, "ModuleExit");
        if (moduleExit) {
            std::cout << "VST3Host: Calling ModuleExit..." << std::endl;
            moduleExit();
        }
        dlclose(m_libHandle);
    }
}

void VST3PluginNode::prepare(double sampleRate, int maxBlockSize) {
    m_audioBuffers.resize(m_ports.size());
    for (size_t i = 0; i < m_ports.size(); ++i) {
        m_audioBuffers[i].assign(maxBlockSize, 0.0f);
        m_ports[i].buffer = m_audioBuffers[i].data();
    }

    if (m_processor) {
        Steinberg::Vst::IAudioProcessor* processor = (Steinberg::Vst::IAudioProcessor*)m_processor;
        Steinberg::Vst::ProcessSetup setup;
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = maxBlockSize;
        setup.sampleRate = sampleRate;
        processor->setupProcessing(setup);
    }
    if (m_component) {
        Steinberg::Vst::IComponent* component = (Steinberg::Vst::IComponent*)m_component;
        int inputBuses = component->getBusCount(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kInput);
        for (int i = 0; i < inputBuses; ++i) {
            component->activateBus(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kInput, i, true);
        }
        int outputBuses = component->getBusCount(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kOutput);
        for (int i = 0; i < outputBuses; ++i) {
            component->activateBus(Steinberg::Vst::MediaTypes::kAudio, Steinberg::Vst::BusDirections::kOutput, i, true);
        }
        component->setActive(true);
    }
    if (m_processor) {
        Steinberg::Vst::IAudioProcessor* processor = (Steinberg::Vst::IAudioProcessor*)m_processor;
        processor->setProcessing(true);
    }
}

void VST3PluginNode::process(int numFrames) {
    if (isBypassed()) {
        int numInputs = getAudioInputCount();
        int numOutputs = getAudioOutputCount();
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

    if (!m_processor) return;

    Steinberg::Vst::IAudioProcessor* processor = (Steinberg::Vst::IAudioProcessor*)m_processor;

    ParameterChanges* paramChanges = (ParameterChanges*)m_paramChanges;
    paramChanges->clear();
    
    ParameterChanges* outParamChanges = (ParameterChanges*)m_outputParamChanges;
    if (outParamChanges) {
        outParamChanges->clear();
    }
    
    for (size_t i = 0; i < m_controlPorts.size(); ++i) {
        float val = m_controlPorts[i].value;
        if (val != m_lastParamValues[i]) {
            int index = 0;
            Steinberg::Vst::IParamValueQueue* queue = paramChanges->addParameterData(m_controlPorts[i].index, index);
            if (queue) {
                int dummy;
                queue->addPoint(0, val, dummy);
            }
            m_lastParamValues[i] = val;
        }
    }

    int numInputs = getAudioInputCount();
    int numOutputs = getAudioOutputCount();

    std::vector<float*> inputChannels(numInputs);
    for (int i = 0; i < numInputs; ++i) {
        inputChannels[i] = m_ports[i].buffer;
    }

    std::vector<float*> outputChannels(numOutputs);
    for (int i = 0; i < numOutputs; ++i) {
        outputChannels[i] = m_ports[numInputs + i].buffer;
        if (outputChannels[i]) {
            std::memset(outputChannels[i], 0, numFrames * sizeof(float));
        }
    }

    Steinberg::Vst::AudioBusBuffers inputBus;
    inputBus.numChannels = numInputs;
    inputBus.silenceFlags = 0;
    inputBus.channelBuffers32 = inputChannels.data();

    Steinberg::Vst::AudioBusBuffers outputBus;
    outputBus.numChannels = numOutputs;
    outputBus.silenceFlags = 0;
    outputBus.channelBuffers32 = outputChannels.data();

    Steinberg::Vst::ProcessData processData;
    processData.processMode = Steinberg::Vst::kRealtime;
    processData.symbolicSampleSize = Steinberg::Vst::kSample32;
    processData.numSamples = numFrames;
    processData.inputs = &inputBus;
    processData.numInputs = (numInputs > 0) ? 1 : 0;
    processData.outputs = &outputBus;
    processData.numOutputs = (numOutputs > 0) ? 1 : 0;
    processData.inputEvents = nullptr;
    processData.outputEvents = nullptr;
    processData.inputParameterChanges = paramChanges;
    processData.outputParameterChanges = outParamChanges;
    processData.processContext = nullptr;

    processor->process(processData);
}

Steinberg::IPlugView* VST3PluginNode::getPlugView() {
    if (!m_plugView && m_controller) {
        Steinberg::Vst::IEditController* controller = (Steinberg::Vst::IEditController*)m_controller;
        m_plugView = controller->createView(Steinberg::Vst::ViewType::kEditor);
        if (m_plugView) {
            m_plugView->setFrame((Steinberg::IPlugFrame*)m_compHandler);
            std::cout << "VST3Host: Dynamically created view " << m_plugView << " and registered IPlugFrame handler." << std::endl;
        } else {
            std::cout << "VST3Host: Failed to create view dynamically (createView returned nullptr)." << std::endl;
        }
    }
    return (Steinberg::IPlugView*)m_plugView;
}

void VST3PluginNode::releasePlugView() {
    if (m_plugView) {
        m_plugView->release();
        m_plugView = nullptr;
        std::cout << "VST3Host: Released cached plugView pointer." << std::endl;
    }
}
