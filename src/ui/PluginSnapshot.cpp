#include "PluginSnapshot.h"

#include "../audio/CLAPHost.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QTimer>
#include <lilv/lilv.h>
#include "../audio/LilvUtil.h"
#include <suil/suil.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/data-access/data-access.h>
#include <lv2/options/options.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/worker/worker.h>
#include <lv2/parameters/parameters.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

// Opens a plugin's own GUI off to one side and writes a PNG of it. This runs as
// its own process (RigRoom --plugin-snapshot), so a plugin that crashes or
// hangs while drawing takes only the helper with it.
namespace {

constexpr uint32_t kBlockSize = 1024;
constexpr uint32_t kAtomBufferSize = 65536;

std::unordered_map<std::string, LV2_URID> snapshotUrids;
LV2_URID snapshotNextUrid = 1;

LV2_URID snapshotMapUri(LV2_URID_Map_Handle, const char* uri) {
    const auto [it, inserted] = snapshotUrids.emplace(uri, snapshotNextUrid);
    if (inserted) ++snapshotNextUrid;
    return it->second;
}

const char* snapshotUnmapUri(LV2_URID_Unmap_Handle, LV2_URID urid) {
    for (const auto& [uri, id] : snapshotUrids) if (id == urid) return uri.c_str();
    return nullptr;
}

bool captureWindow(Display* display, Window window, const QString& outPath);

struct Snapshotter {
    Display* display = nullptr;
    Window window = 0;
    Window child = 0;
    LilvWorld* world = nullptr;
    const LilvPlugin* plugin = nullptr;
    LilvInstance* dspInstance = nullptr;
    std::vector<std::vector<float>> audioBuffers;
    std::vector<float> controlValues;
    std::vector<std::vector<uint8_t>> atomBuffers;
    SuilHost* host = nullptr;
    SuilInstance* instance = nullptr;
    const LV2UI_Idle_Interface* idle = nullptr;
    // Set when the GUI needs a Gtk window instead of a plain X11 one.
    QLibrary gtkLibrary;
    void* gtkWindow = nullptr;
    const char* gdkLibraryName = nullptr;
    void (*gtkMainIteration)() = nullptr;
    int (*gtkEventsPending)() = nullptr;
    int requestedWidth = 0;
    int requestedHeight = 0;
    bool uiReady = false;

    LV2_URID_Map map{nullptr, snapshotMapUri};
    LV2_URID_Unmap unmap{nullptr, snapshotUnmapUri};
    LV2_Feature mapFeature{LV2_URID__map, &map};
    LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};
    LV2_Feature parentFeature{LV2_UI__parent, nullptr};
    LV2UI_Resize hostResize{this, resizeRequest};
    LV2_Feature resizeFeature{LV2_UI__resize, &hostResize};
    LV2_Feature residentFeature{"http://lv2plug.in/ns/extensions/ui#makeResident", nullptr};
    // JUCE and other wrapper UIs talk to their own DSP instance directly, so
    // they only load when instance-access and data-access are offered.
    LV2_Feature instanceFeature{LV2_INSTANCE_ACCESS_URI, nullptr};
    LV2_Extension_Data_Feature dataAccess{nullptr};
    LV2_Feature dataFeature{LV2_DATA_ACCESS_URI, &dataAccess};
    const LV2_Feature* features[8]{&mapFeature, &unmapFeature, &parentFeature, &resizeFeature,
                                   &residentFeature, &instanceFeature, &dataFeature, nullptr};

    static int resizeRequest(LV2UI_Feature_Handle handle, int width, int height) {
        auto* self = static_cast<Snapshotter*>(handle);
        if (!self || width <= 0 || height <= 0) return 1;
        self->requestedWidth = width;
        self->requestedHeight = height;
        if (self->uiReady && self->display && self->window) {
            XResizeWindow(self->display, self->window, width, height);
            XFlush(self->display);
        }
        return 0;
    }

    static void portWrite(SuilController, uint32_t, uint32_t, uint32_t, const void*) {}

    // No audio is processed here, so scheduled work is simply accepted.
    static LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle, uint32_t, const void*) {
        return LV2_WORKER_SUCCESS;
    }

    // Which kind of window this GUI needs. An X11 UI goes straight into a plain
    // X window; a Gtk UI only draws inside a Gtk window of the same version, so
    // it gets one (suil has no wrapper that puts Gtk inside X11).
    struct UIChoice {
        const LilvUI* ui = nullptr;
        std::string uiType;
        std::string container;      // LV2_UI__X11UI, GtkUI or Gtk3UI
        const char* gtkLibrary = nullptr;
        const char* gdkLibrary = nullptr;
    };


    UIChoice chooseUI() const {
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        UIChoice choice;
        if (!uis) return choice;
        // Containers in order of preference: the plainest window first.
        struct Container { const char* type; const char* gtk; const char* gdk; };
        static const Container containers[] = {
            {LV2_UI__X11UI, nullptr, nullptr},
            {LV2_UI__Gtk3UI, "libgtk-3.so.0", "libgdk-3.so.0"},
            {LV2_UI__GtkUI, "libgtk-x11-2.0.so.0", "libgdk-x11-2.0.so.0"},
        };
        unsigned bestQuality = 0;
        for (const Container& container : containers) {
            if (container.gtk && !QLibrary(container.gtk).load()) continue;
            LILV_FOREACH(uis, i, uis) {
                const LilvUI* candidate = lilv_uis_get(uis, i);
                const LilvNodes* classes = lilv_ui_get_classes(candidate);
                LILV_FOREACH(nodes, c, classes) {
                    const char* classUri = lilvUriText(lilv_nodes_get(classes, c));
                    const unsigned quality = suil_ui_supported(container.type, classUri);
                    if (quality == 0) continue;
                    if (choice.ui && quality >= bestQuality) continue;
                    choice.ui = candidate;
                    choice.uiType = classUri;
                    choice.container = container.type;
                    choice.gtkLibrary = container.gtk;
                    choice.gdkLibrary = container.gdk;
                    bestQuality = quality;
                }
            }
            if (choice.ui) break;  // a plainer container already works
        }
        return choice;
    }

    // A running DSP instance for the UI to attach to. Ports get real buffers so
    // a UI that reads them finds memory, but run() is never called: this only
    // draws a GUI, it never processes audio.
    void instantiateDsp() {
        // Plugins refuse to instantiate when a required feature is missing, and
        // a wrapper UI without its DSP instance then refuses to load too.
        const float sampleRate = 48000.0f;
        const int blockLength = static_cast<int>(kBlockSize);
        LV2_Options_Option options[] = {
            {LV2_OPTIONS_INSTANCE, 0, snapshotMapUri(nullptr, LV2_BUF_SIZE__maxBlockLength),
             sizeof(int), snapshotMapUri(nullptr, LV2_ATOM__Int), &blockLength},
            {LV2_OPTIONS_INSTANCE, 0, snapshotMapUri(nullptr, LV2_BUF_SIZE__minBlockLength),
             sizeof(int), snapshotMapUri(nullptr, LV2_ATOM__Int), &blockLength},
            {LV2_OPTIONS_INSTANCE, 0, snapshotMapUri(nullptr, LV2_PARAMETERS__sampleRate),
             sizeof(float), snapshotMapUri(nullptr, LV2_ATOM__Float), &sampleRate},
            {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr},
        };
        LV2_Feature optionsFeature{LV2_OPTIONS__options, options};
        LV2_Feature boundedFeature{LV2_BUF_SIZE__boundedBlockLength, nullptr};
        LV2_Feature powerOfTwoFeature{LV2_BUF_SIZE__powerOf2BlockLength, nullptr};
        LV2_Worker_Schedule schedule{this, scheduleWork};
        LV2_Feature workerFeature{LV2_WORKER__schedule, &schedule};
        const LV2_Feature* dspFeatures[8]{&mapFeature,      &unmapFeature,    &optionsFeature,
                                          &boundedFeature,  &powerOfTwoFeature, &workerFeature,
                                          nullptr};
        dspInstance = lilv_plugin_instantiate(plugin, 48000.0, dspFeatures);
        if (!dspInstance) { fprintf(stderr, "snapshot: running without instance access\n"); return; }

        const uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
        audioBuffers.resize(numPorts);
        atomBuffers.resize(numPorts);
        controlValues.assign(numPorts, 0.0f);
        std::vector<float> defaults(numPorts, 0.0f);
        lilv_plugin_get_port_ranges_float(plugin, nullptr, nullptr, defaults.data());

        LilvNode* audioClass = lilv_new_uri(world, LV2_CORE__AudioPort);
        LilvNode* controlClass = lilv_new_uri(world, LV2_CORE__ControlPort);
        LilvNode* cvClass = lilv_new_uri(world, LV2_CORE__CVPort);
        for (uint32_t i = 0; i < numPorts; ++i) {
            const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
            if (lilv_port_is_a(plugin, port, controlClass)) {
                controlValues[i] = std::isnan(defaults[i]) ? 0.0f : defaults[i];
                lilv_instance_connect_port(dspInstance, i, &controlValues[i]);
            } else if (lilv_port_is_a(plugin, port, audioClass) || lilv_port_is_a(plugin, port, cvClass)) {
                audioBuffers[i].assign(kBlockSize, 0.0f);
                lilv_instance_connect_port(dspInstance, i, audioBuffers[i].data());
            } else {
                atomBuffers[i].assign(kAtomBufferSize, 0);
                auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(atomBuffers[i].data());
                sequence->atom.size = sizeof(LV2_Atom_Sequence_Body);
                sequence->atom.type = snapshotMapUri(nullptr, LV2_ATOM__Sequence);
                lilv_instance_connect_port(dspInstance, i, atomBuffers[i].data());
            }
        }
        lilv_node_free(audioClass);
        lilv_node_free(controlClass);
        lilv_node_free(cvClass);
        lilv_instance_activate(dspInstance);
        instanceFeature.data = lilv_instance_get_handle(dspInstance);
        dataAccess.data_access = lilv_instance_get_descriptor(dspInstance)->extension_data;
    }

    // Loads Gtk and makes a top-level window for a Gtk plugin GUI.
    bool startGtk(const UIChoice& choice) {
        qputenv("GTK_MODULES", "");
        gtkLibrary.setFileName(choice.gtkLibrary);
        if (!gtkLibrary.load()) { fprintf(stderr, "snapshot: Gtk is not installed\n"); return false; }
        auto initCheck = reinterpret_cast<int (*)(int*, char***)>(gtkLibrary.resolve("gtk_init_check"));
        auto windowNew = reinterpret_cast<void* (*)(int)>(gtkLibrary.resolve("gtk_window_new"));
        gtkMainIteration = reinterpret_cast<void (*)()>(gtkLibrary.resolve("gtk_main_iteration"));
        gtkEventsPending = reinterpret_cast<int (*)()>(gtkLibrary.resolve("gtk_events_pending"));
        if (!initCheck || !windowNew || !gtkMainIteration || !gtkEventsPending) return false;
        int gtkArgc = 0;
        char** gtkArgv = nullptr;
        if (!initCheck(&gtkArgc, &gtkArgv)) { fprintf(stderr, "snapshot: Gtk would not start\n"); return false; }
        gtkWindow = windowNew(0);  // GTK_WINDOW_TOPLEVEL
        if (!gtkWindow) return false;
        gdkLibraryName = choice.gdkLibrary;
        return true;
    }

    // Shows the Gtk GUI and finds the X window behind it, which is what gets read.
    bool showInGtkWindow() {
        void* widget = suil_instance_get_widget(instance);
        if (!widget) return false;
        auto containerAdd = reinterpret_cast<void (*)(void*, void*)>(gtkLibrary.resolve("gtk_container_add"));
        auto showAll = reinterpret_cast<void (*)(void*)>(gtkLibrary.resolve("gtk_widget_show_all"));
        auto getWindow = reinterpret_cast<void* (*)(void*)>(gtkLibrary.resolve("gtk_widget_get_window"));
        if (!containerAdd || !showAll || !getWindow) return false;
        containerAdd(gtkWindow, widget);
        showAll(gtkWindow);
        for (int i = 0; i < 200 && gtkEventsPending(); ++i) gtkMainIteration();

        QLibrary gdk(gdkLibraryName);
        if (!gdk.load()) return false;
        auto getXid = reinterpret_cast<unsigned long (*)(void*)>(gdk.resolve("gdk_x11_window_get_xid"));
        if (!getXid) getXid = reinterpret_cast<unsigned long (*)(void*)>(gdk.resolve("gdk_x11_drawable_get_xid"));
        void* gdkWindow = getWindow(gtkWindow);
        if (!getXid || !gdkWindow) return false;
        window = static_cast<Window>(getXid(gdkWindow));
        if (!window) return false;
        XSync(display, False);
        uiReady = true;
        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        return true;
    }

    bool start(const QString& pluginUri) {
        display = XOpenDisplay(nullptr);
        if (!display) { fprintf(stderr, "snapshot: no X display\n"); return false; }
        const int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 640, 480, 0,
                                     BlackPixel(display, screen), BlackPixel(display, screen));
        if (!window) return false;
        XStoreName(display, window, "RigRoom plugin snapshot");
        XSelectInput(display, window, StructureNotifyMask);
        // The plugin draws through its own connection to the server, so the
        // window has to exist there before the GUI is handed its id.
        XSync(display, False);
        parentFeature.data = reinterpret_cast<void*>(static_cast<uintptr_t>(window));

        world = lilv_world_new();
        lilv_world_load_all(world);
        LilvNode* pluginNode = lilv_new_uri(world, pluginUri.toUtf8().constData());
        plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginNode);
        lilv_node_free(pluginNode);
        if (!plugin) { fprintf(stderr, "snapshot: no such plugin\n"); return false; }

        instantiateDsp();

        const UIChoice choice = chooseUI();
        if (!choice.ui) { fprintf(stderr, "snapshot: plugin has no embeddable GUI\n"); return false; }
        const char* uiUri = lilvUriText(lilv_ui_get_uri(choice.ui));

        host = suil_host_new(portWrite, nullptr, nullptr, nullptr);
        const std::string bundle = lilvFilePath(lilv_ui_get_bundle_uri(choice.ui));
        std::string binary = lilvFilePath(lilv_ui_get_binary_uri(choice.ui));
        // Calf points at a GUI binary that distributions put elsewhere.
        QByteArray binaryOverride;
        if ((binary.empty() || !QFile::exists(QString::fromLocal8Bit(binary.c_str())))
            && pluginUri.startsWith("http://calf.sourceforge.net/plugins/")) {
            for (const char* candidate : {"/usr/lib64/calf/libcalflv2gui.so", "/usr/lib/calf/libcalflv2gui.so",
                                          "/usr/lib/x86_64-linux-gnu/calf/libcalflv2gui.so"}) {
                if (QFile::exists(QString::fromLatin1(candidate))) { binaryOverride = candidate; break; }
            }
            if (!binaryOverride.isEmpty()) {
                binary.clear();
            }
        }
        // A Gtk UI is handed a Gtk window instead of our X11 one, so the parent
        // feature must not point at an X window it cannot use.
        const bool gtkPath = choice.gtkLibrary != nullptr;
        if (gtkPath && !startGtk(choice)) return false;
        instance = suil_instance_new(host, this, choice.container.c_str(), pluginUri.toUtf8().constData(),
                                     uiUri, choice.uiType.c_str(), bundle.c_str(),
                                     binaryOverride.isEmpty()
                                         ? (binary.empty() ? nullptr : binary.c_str())
                                         : binaryOverride.constData(), features);
        if (!instance) { fprintf(stderr, "snapshot: the GUI would not load\n"); return false; }

        if (gtkPath) return showInGtkWindow();

        child = reinterpret_cast<uintptr_t>(suil_instance_get_widget(instance));
        XWindowAttributes attributes{};
        if (!child || !XGetWindowAttributes(display, child, &attributes)) return false;
        XReparentWindow(display, child, window, 0, 0);
        const int width = requestedWidth > 0 ? requestedWidth : attributes.width;
        const int height = requestedHeight > 0 ? requestedHeight : attributes.height;
        XResizeWindow(display, window, width, height);
        XMoveResizeWindow(display, child, 0, 0, width, height);
        XMapWindow(display, child);
        XMapRaised(display, window);
        XFlush(display);
        uiReady = true;

        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        return true;
    }

    // Lets the GUI draw itself: X events and the plugin's own idle callback.
    void pump(int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            while (XPending(display)) {
                XEvent event;
                XNextEvent(display, &event);
            }
            if (gtkMainIteration && gtkEventsPending) {
                for (int i = 0; i < 50 && gtkEventsPending(); ++i) gtkMainIteration();
            }
            if (idle) idle->idle(suil_instance_get_handle(instance));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            usleep(5000);
        }
    }

    bool capture(const QString& outPath) { return captureWindow(display, window, outPath); }
};

bool captureWindow(Display* display, Window window, const QString& outPath) {
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display, window, &attributes)) return false;
    XImage* image = XGetImage(display, window, 0, 0, attributes.width, attributes.height,
                              AllPlanes, ZPixmap);
    if (!image) { fprintf(stderr, "snapshot: the window could not be read\n"); return false; }
    QImage shot(attributes.width, attributes.height, QImage::Format_RGB32);
    for (int y = 0; y < attributes.height; ++y) {
        for (int x = 0; x < attributes.width; ++x) {
            const unsigned long pixel = XGetPixel(image, x, y);
            shot.setPixel(x, y, static_cast<QRgb>(pixel & 0xFFFFFFu) | 0xFF000000u);
        }
    }
    XDestroyImage(image);

    // A GUI that never painted leaves one flat colour behind. That is not
    // worth caching, and it would look like a bug in the browser.
    const QRgb first = shot.pixel(0, 0);
    bool flat = true;
    for (int y = 0; y < shot.height() && flat; y += 3) {
        for (int x = 0; x < shot.width(); x += 3) {
            if (shot.pixel(x, y) != first) { flat = false; break; }
        }
    }
    if (flat) { fprintf(stderr, "snapshot: the GUI never drew anything\n"); return false; }

    // Some GUIs are enormous (LSP reaches 2700 px). The browser never shows
    // them bigger than this, so the cache keeps a sensible size.
    QImage saved = shot;
    if (saved.width() > 1600 || saved.height() > 1200) {
        saved = saved.scaled(1600, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    printf("snapshot: %dx%d\n", saved.width(), saved.height());
    return saved.save(outPath, "PNG");
}

// Reads a mapped window and writes it as a PNG.

}  // namespace

namespace {

// CLAP plugins host their GUI themselves: the plugin is asked to draw into an
// X window we own, which is then read the same way as an LV2 one.
bool snapshotClap(const QString& uri, const QString& outPath, int settleMs) {
    std::string path = uri.toStdString();
    uint32_t index = 0;
    const auto colon = path.rfind(':');
    if (colon != std::string::npos && colon > path.find(".clap")) {
        index = static_cast<uint32_t>(std::stoul(path.substr(colon + 1)));
        path = path.substr(0, colon);
    }

    auto node = std::make_shared<CLAPPluginNode>(path, index);
    if (node->isMissing()) { fprintf(stderr, "snapshot: the plugin would not load\n"); return false; }
    const clap_plugin_gui_t* gui = node->getClapGuiExtension();
    const clap_plugin_t* plugin = node->getClapPlugin();
    if (!gui || !plugin || !node->hasGUI()) { fprintf(stderr, "snapshot: plugin has no embeddable GUI\n"); return false; }
    if (gui->is_api_supported && !gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false)) {
        fprintf(stderr, "snapshot: the GUI cannot use a plain window\n");
        return false;
    }

    Display* display = XOpenDisplay(nullptr);
    if (!display) { fprintf(stderr, "snapshot: no X display\n"); return false; }
    const int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 640, 480, 0,
                                        BlackPixel(display, screen), BlackPixel(display, screen));
    XStoreName(display, window, "RigRoom plugin snapshot");
    XSelectInput(display, window, StructureNotifyMask);
    XSync(display, False);

    if (!gui->create(plugin, CLAP_WINDOW_API_X11, false)) {
        fprintf(stderr, "snapshot: the GUI would not load\n");
        return false;
    }
    if (gui->set_scale) gui->set_scale(plugin, 1.0);
    uint32_t width = 640, height = 480;
    if (gui->get_size) gui->get_size(plugin, &width, &height);
    if (width == 0 || height == 0) { width = 640; height = 480; }
    XResizeWindow(display, window, width, height);
    XMapRaised(display, window);
    XSync(display, False);

    clap_window_t parent{};
    parent.api = CLAP_WINDOW_API_X11;
    parent.x11 = static_cast<clap_xwnd>(window);
    if (!gui->set_parent(plugin, &parent)) {
        fprintf(stderr, "snapshot: the GUI would not attach to a window\n");
        gui->destroy(plugin);
        return false;
    }
    if (gui->show) gui->show(plugin);
    if (gui->get_size) {
        gui->get_size(plugin, &width, &height);
        if (width > 0 && height > 0) XResizeWindow(display, window, width, height);
    }
    XSync(display, False);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < settleMs) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        usleep(5000);
    }

    const bool saved = captureWindow(display, window, outPath);
    gui->destroy(plugin);
    return saved;
}

}  // namespace

int runPluginSnapshot(int argc, char* argv[]) {
    // --plugin-snapshot <plugin-uri> <out.png> [settle-ms]
    if (argc < 4) return 2;
    QCoreApplication app(argc, argv);
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    const QString pluginUri = QString::fromUtf8(argv[2]);
    const QString outPath = QString::fromUtf8(argv[3]);
    const int settleMs = argc > 4 ? QString::fromUtf8(argv[4]).toInt() : 1500;

    // CLAP plugins are named by their file; everything else is an LV2 URI.
    if (pluginUri.contains(".clap")) return snapshotClap(pluginUri, outPath, settleMs) ? 0 : 3;

    Snapshotter snapshotter;
    if (!snapshotter.start(pluginUri)) return 3;
    snapshotter.pump(settleMs);
    if (!snapshotter.capture(outPath)) return 4;
    return 0;
}

int runPluginSnapshotBatch(int argc, char* argv[]) {
    if (argc < 4) return 2;
    QCoreApplication app(argc, argv);
    QFile queue(QString::fromUtf8(argv[2]));
    if (!queue.open(QFile::ReadOnly)) return 2;
    const QString outDir = QString::fromUtf8(argv[3]);
    QDir().mkpath(outDir);

    // Cancelling drops this file in place: the run stops after the plugin it is
    // on, which lets the hidden display close itself down properly.
    const QString stopFile = outDir + "/cancel";
    QFile::remove(stopFile);

    QTextStream out(stdout);
    QTextStream in(&queue);
    while (!in.atEnd()) {
        const QString uri = in.readLine().trimmed();
        if (uri.isEmpty()) continue;
        if (QFile::exists(stopFile)) {
            out << "PREVIEW CANCELLED\n";
            out.flush();
            QFile::remove(stopFile);
            break;
        }
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Sha1).toHex());
        const QString target = outDir + "/" + hash + ".png";

        out << "PREVIEW START " << uri << "\n";
        out.flush();
        // Each plugin in its own process: a GUI that crashes or hangs costs
        // only its own preview, and the run carries on.
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {"--plugin-snapshot", uri, target, "1200"});
        QString reason;
        bool ok = false;
        if (!child.waitForFinished(25000)) {
            child.kill();
            child.waitForFinished(2000);
            reason = "timed out";
        } else {
            ok = child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0
                 && QFile::exists(target);
            if (!ok) {
                const QString output = QString::fromUtf8(child.readAll());
                for (const QString& line : output.split('\n')) {
                    if (line.startsWith("snapshot: ")) reason = line.mid(10).trimmed();
                }
                if (reason.isEmpty()) reason = child.exitStatus() == QProcess::CrashExit
                                                  ? QString("the GUI crashed")
                                                  : QString("no preview");
            }
        }
        out << "PREVIEW " << (ok ? "OK " : "FAIL ") << uri;
        if (!ok) out << " " << reason;
        out << "\n";
        out.flush();
    }
    return 0;
}
