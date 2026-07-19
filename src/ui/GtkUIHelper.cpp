#include "GtkUIHelper.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QLocalSocket>
#include <QTimer>
#include <QDataStream>
#include <QFile>
#include <lilv/lilv.h>
#include <suil/suil.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace {
std::unordered_map<std::string, LV2_URID> urids;
LV2_URID nextUrid = 1;

LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri) {
    const auto [it, inserted] = urids.emplace(uri, nextUrid);
    if (inserted) ++nextUrid;
    return it->second;
}

const char* unmapUri(LV2_URID_Unmap_Handle, LV2_URID urid) {
    for (const auto& [uri, id] : urids) if (id == urid) return uri.c_str();
    return nullptr;
}

struct Helper {
    using GtkInitCheck = int (*)(int*, char***);
    using GtkWindowNew = void* (*)(int);
    using GtkWindowSetTitle = void (*)(void*, const char*);
    using GtkWindowSetKeepAbove = void (*)(void*, int);
    using GtkContainerAdd = void (*)(void*, void*);
    using GtkWidgetAction = void (*)(void*);
    using GtkWidgetGetWindow = void* (*)(void*);
    using SignalConnectData = unsigned long (*)(void*, const char*, void (*)(), void*, void (*)(void*, void*), int);

    Helper(const char* gtkLibrary, const char* containerType, const char* uiType, QString titlePrefix)
        : gtk(gtkLibrary), containerType(containerType), uiType(uiType), titlePrefix(std::move(titlePrefix)) {}

    QLibrary gtk;
    const char* containerType;
    const char* uiType;
    QString titlePrefix;
    QLocalSocket socket;
    LilvWorld* world = nullptr;
    SuilHost* host = nullptr;
    SuilInstance* instance = nullptr;
    void* window = nullptr;
    GtkWidgetAction destroyWidget = nullptr;
    const LV2UI_Idle_Interface* idle = nullptr;
    QTimer idleTimer;
    QByteArray incoming;

    LV2_URID_Map map{nullptr, mapUri};
    LV2_URID_Unmap unmap{nullptr, unmapUri};
    LV2_Feature mapFeature{LV2_URID__map, &map};
    LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};
    LV2_Feature residentFeature{"http://lv2plug.in/ns/extensions/ui#makeResident", nullptr};
    const LV2_Feature* features[4]{&mapFeature, &unmapFeature, &residentFeature, nullptr};

    static void portWrite(SuilController controller, uint32_t index, uint32_t size,
                          uint32_t protocol, const void* buffer) {
        auto* self = static_cast<Helper*>(controller);
        if (!self || protocol != 0 || size != sizeof(float)) return;
        QByteArray message;
        QDataStream stream(&message, QIODevice::WriteOnly);
        stream << quint8('P') << quint32(index) << *static_cast<const float*>(buffer);
        self->socket.write(message);
        self->socket.flush();
    }

    static int closeWindow(void*, void*, void*) {
        QCoreApplication::quit();
        return 1;
    }

    bool start(const QString& serverName, const QString& pluginUri, const QString& uiUri, unsigned long parentWindow) {
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(5000)) return false;

        // Avoid optional desktop integration modules which are not required by LV2 UIs.
        qputenv("GTK_MODULES", "");
        if (!gtk.load()) return false;
        auto initCheck = reinterpret_cast<GtkInitCheck>(gtk.resolve("gtk_init_check"));
        auto windowNew = reinterpret_cast<GtkWindowNew>(gtk.resolve("gtk_window_new"));
        auto setTitle = reinterpret_cast<GtkWindowSetTitle>(gtk.resolve("gtk_window_set_title"));
        auto setKeepAbove = reinterpret_cast<GtkWindowSetKeepAbove>(gtk.resolve("gtk_window_set_keep_above"));
        auto containerAdd = reinterpret_cast<GtkContainerAdd>(gtk.resolve("gtk_container_add"));
        auto showAll = reinterpret_cast<GtkWidgetAction>(gtk.resolve("gtk_widget_show_all"));
        auto getWindow = reinterpret_cast<GtkWidgetGetWindow>(gtk.resolve("gtk_widget_get_window"));
        destroyWidget = reinterpret_cast<GtkWidgetAction>(gtk.resolve("gtk_widget_destroy"));
        auto connectSignal = reinterpret_cast<SignalConnectData>(gtk.resolve("g_signal_connect_data"));
        if (!initCheck || !windowNew || !setTitle || !setKeepAbove || !containerAdd || !showAll ||
            !getWindow || !destroyWidget || !connectSignal) return false;
        int gtkArgc = 0;
        char** gtkArgv = nullptr;
        if (!initCheck(&gtkArgc, &gtkArgv)) return false;

        world = lilv_world_new();
        lilv_world_load_all(world);
        LilvNode* pluginNode = lilv_new_uri(world, pluginUri.toUtf8().constData());
        const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginNode);
        lilv_node_free(pluginNode);
        if (!plugin) return false;

        const LilvUI* selectedUi = nullptr;
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        LILV_FOREACH(uis, i, uis) {
            const LilvUI* candidate = lilv_uis_get(uis, i);
            if (uiUri == QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(candidate)))) {
                selectedUi = candidate;
                break;
            }
        }
        if (!selectedUi) return false;

        host = suil_host_new(portWrite, nullptr, nullptr, nullptr);
        char* bundle = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_bundle_uri(selectedUi)), nullptr);
        const LilvNode* binaryNode = lilv_ui_get_binary_uri(selectedUi);
        char* binary = binaryNode ? lilv_file_uri_parse(lilv_node_as_uri(binaryNode), nullptr) : nullptr;
        QByteArray binaryOverride;
        if ((!binary || !QFile::exists(QString::fromLocal8Bit(binary))) &&
            pluginUri.startsWith("http://calf.sourceforge.net/plugins/")) {
            const QString fedoraCalfUi = "/usr/lib64/calf/libcalflv2gui.so";
            if (QFile::exists(fedoraCalfUi)) {
                binaryOverride = QFile::encodeName(fedoraCalfUi);
                if (binary) lilv_free(binary);
                binary = nullptr;
            }
        }
        const QByteArray pluginBytes = pluginUri.toUtf8();
        const QByteArray uiBytes = uiUri.toUtf8();
        instance = suil_instance_new(host, this, containerType, pluginBytes.constData(),
                                     uiBytes.constData(), uiType, bundle,
                                     binaryOverride.isEmpty() ? binary : binaryOverride.constData(), features);
        lilv_free(bundle);
        if (binary) lilv_free(binary);
        if (!instance) return false;

        void* pluginWidget = suil_instance_get_widget(instance);
        if (!pluginWidget) return false;
        window = windowNew(0);
        const QByteArray title = (titlePrefix + " - " + pluginUri).toUtf8();
        setTitle(window, title.constData());
        setKeepAbove(window, 1);
        containerAdd(window, pluginWidget);
        connectSignal(window, "delete-event", reinterpret_cast<void (*)()>(&Helper::closeWindow),
                       this, nullptr, 0);
        showAll(window);
        if (parentWindow) {
            QLibrary gdk{"libgdk-x11-2.0.so.0"};
            using GdkWindowGetXid = unsigned long (*)(void*);
            auto getXid = gdk.load()
                ? reinterpret_cast<GdkWindowGetXid>(gdk.resolve("gdk_x11_window_get_xid"))
                : nullptr;
            if (getXid) {
                if (Display* display = XOpenDisplay(nullptr)) {
                    XSetTransientForHint(display, getXid(getWindow(window)), parentWindow);
                    XFlush(display);
                    XCloseDisplay(display);
                }
            }
        }

        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        if (idle) {
            QObject::connect(&idleTimer, &QTimer::timeout, [&]() {
                idle->idle(suil_instance_get_handle(instance));
            });
            idleTimer.start(16);
        }

        QObject::connect(&socket, &QLocalSocket::readyRead, [&]() {
            incoming += socket.readAll();
            QDataStream stream(&incoming, QIODevice::ReadOnly);
            while (true) {
                stream.startTransaction();
                quint8 command = 0;
                quint32 index = 0;
                float value = 0.0f;
                stream >> command >> index >> value;
                if (!stream.commitTransaction()) break;
                if (command == 'P') {
                    suil_instance_port_event(instance, index, sizeof(float), 0, &value);
                }
            }
            incoming.remove(0, static_cast<int>(stream.device()->pos()));
        });
        QObject::connect(&socket, &QLocalSocket::disconnected, &QCoreApplication::quit);
        return true;
    }

    ~Helper() {
        idleTimer.stop();
        if (instance) suil_instance_free(instance);
        if (window && destroyWidget) destroyWidget(window);
        if (host) suil_host_free(host);
        if (world) lilv_world_free(world);
    }
};

struct X11Helper {
    QLocalSocket socket;
    LilvWorld* world = nullptr;
    SuilHost* host = nullptr;
    SuilInstance* instance = nullptr;
    Display* display = nullptr;
    Window window = 0;
    Window child = 0;
    Atom closeMessage = None;
    const LV2UI_Idle_Interface* idle = nullptr;
    const LV2UI_Resize* resize = nullptr;
    QTimer idleTimer;
    QTimer eventTimer;
    QByteArray incoming;
    int requestedWidth = 0;
    int requestedHeight = 0;
    int lastResizeWidth = 0;
    int lastResizeHeight = 0;
    bool uiReady = false;

    LV2_URID_Map map{nullptr, mapUri};
    LV2_URID_Unmap unmap{nullptr, unmapUri};
    LV2_Feature mapFeature{LV2_URID__map, &map};
    LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};
    LV2_Feature parentFeature{LV2_UI__parent, nullptr};
    LV2UI_Resize hostResize{this, resizeRequest};
    LV2_Feature resizeFeature{LV2_UI__resize, &hostResize};
    LV2_Feature residentFeature{"http://lv2plug.in/ns/extensions/ui#makeResident", nullptr};
    const LV2_Feature* features[6]{&mapFeature, &unmapFeature, &parentFeature, &resizeFeature, &residentFeature, nullptr};

    static int resizeRequest(LV2UI_Feature_Handle handle, int width, int height) {
        auto* self = static_cast<X11Helper*>(handle);
        if (!self || width <= 0 || height <= 0) return 1;
        self->requestedWidth = width;
        self->requestedHeight = height;
        if (self->uiReady && self->display && self->window) {
            XResizeWindow(self->display, self->window, width, height);
            XFlush(self->display);
        }
        return 0;
    }

    static void portWrite(SuilController controller, uint32_t index, uint32_t size,
                          uint32_t protocol, const void* buffer) {
        auto* self = static_cast<X11Helper*>(controller);
        if (!self || protocol != 0 || size != sizeof(float)) return;
        QByteArray message;
        QDataStream stream(&message, QIODevice::WriteOnly);
        stream << quint8('P') << quint32(index) << *static_cast<const float*>(buffer);
        self->socket.write(message);
        self->socket.flush();
    }

    bool start(const QString& serverName, const QString& pluginUri, const QString& uiUri, unsigned long parentWindow) {
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(5000)) return false;

        display = XOpenDisplay(nullptr);
        if (!display) return false;
        const int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 1, 1, 0,
                                     BlackPixel(display, screen), BlackPixel(display, screen));
        if (!window) return false;
        const QByteArray title = (QString("Plugin GUI - ") + pluginUri).toUtf8();
        XStoreName(display, window, title.constData());
        closeMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &closeMessage, 1);
        XSelectInput(display, window, StructureNotifyMask);
        if (parentWindow) XSetTransientForHint(display, window, parentWindow);
        parentFeature.data = reinterpret_cast<void*>(static_cast<uintptr_t>(window));

        world = lilv_world_new();
        lilv_world_load_all(world);
        LilvNode* pluginNode = lilv_new_uri(world, pluginUri.toUtf8().constData());
        const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginNode);
        lilv_node_free(pluginNode);
        if (!plugin) return false;

        const LilvUI* selectedUi = nullptr;
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        LILV_FOREACH(uis, i, uis) {
            const LilvUI* candidate = lilv_uis_get(uis, i);
            if (uiUri == QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(candidate)))) {
                selectedUi = candidate;
                break;
            }
        }
        if (!selectedUi) return false;

        host = suil_host_new(portWrite, nullptr, nullptr, nullptr);
        char* bundle = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_bundle_uri(selectedUi)), nullptr);
        const LilvNode* binaryNode = lilv_ui_get_binary_uri(selectedUi);
        char* binary = binaryNode ? lilv_file_uri_parse(lilv_node_as_uri(binaryNode), nullptr) : nullptr;
        const QByteArray pluginBytes = pluginUri.toUtf8();
        const QByteArray uiBytes = uiUri.toUtf8();
        instance = suil_instance_new(host, this, LV2_UI__X11UI, pluginBytes.constData(),
                                     uiBytes.constData(), LV2_UI__X11UI, bundle, binary, features);
        lilv_free(bundle);
        if (binary) lilv_free(binary);
        if (!instance) return false;

        child = reinterpret_cast<uintptr_t>(suil_instance_get_widget(instance));
        XWindowAttributes attributes{};
        if (!child || !XGetWindowAttributes(display, child, &attributes)) return false;
        XReparentWindow(display, child, window, 0, 0);
        const int width = requestedWidth > 0 ? requestedWidth : attributes.width;
        const int height = requestedHeight > 0 ? requestedHeight : attributes.height;
        XSizeHints sizeHints{};
        sizeHints.flags = PMinSize | PAspect;
        sizeHints.min_width = 1;
        sizeHints.min_height = 1;
        sizeHints.min_aspect.x = width;
        sizeHints.min_aspect.y = height;
        sizeHints.max_aspect.x = width;
        sizeHints.max_aspect.y = height;
        XSetWMNormalHints(display, window, &sizeHints);
        XResizeWindow(display, window, width, height);
        XMoveResizeWindow(display, child, 0, 0, width, height);
        XMapWindow(display, child);
        XMapRaised(display, window);
        XFlush(display);
        uiReady = true;

        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        resize = static_cast<const LV2UI_Resize*>(
            suil_instance_extension_data(instance, LV2_UI__resize));
        if (idle) {
            QObject::connect(&idleTimer, &QTimer::timeout, [&]() {
                idle->idle(suil_instance_get_handle(instance));
            });
            idleTimer.start(16);
        }

        QObject::connect(&socket, &QLocalSocket::readyRead, [&]() {
            incoming += socket.readAll();
            QDataStream stream(&incoming, QIODevice::ReadOnly);
            while (true) {
                stream.startTransaction();
                quint8 command = 0;
                quint32 index = 0;
                float value = 0.0f;
                stream >> command >> index >> value;
                if (!stream.commitTransaction()) break;
                if (command == 'P') suil_instance_port_event(instance, index, sizeof(float), 0, &value);
            }
            incoming.remove(0, static_cast<int>(stream.device()->pos()));
        });
        QObject::connect(&socket, &QLocalSocket::disconnected, &QCoreApplication::quit);
        QObject::connect(&eventTimer, &QTimer::timeout, [&]() {
            while (XPending(display)) {
                XEvent event{};
                XNextEvent(display, &event);
                if (event.type == ClientMessage && event.xclient.data.l[0] == static_cast<long>(closeMessage)) {
                    QCoreApplication::quit();
                } else if (event.type == ConfigureNotify && event.xconfigure.window == window && resize &&
                           (event.xconfigure.width != lastResizeWidth || event.xconfigure.height != lastResizeHeight)) {
                    lastResizeWidth = event.xconfigure.width;
                    lastResizeHeight = event.xconfigure.height;
                    resize->ui_resize(suil_instance_get_handle(instance), lastResizeWidth, lastResizeHeight);
                    XMoveResizeWindow(display, child, 0, 0, lastResizeWidth, lastResizeHeight);
                    XFlush(display);
                }
            }
        });
        eventTimer.start(16);
        return true;
    }

    ~X11Helper() {
        eventTimer.stop();
        idleTimer.stop();
        if (instance) suil_instance_free(instance);
        if (display && window) XDestroyWindow(display, window);
        if (display) XCloseDisplay(display);
        if (host) suil_host_free(host);
        if (world) lilv_world_free(world);
    }
};
}

int runGtkUIHelper(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) return 2;
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    Helper helper("libgtk-x11-2.0.so.0", LV2_UI__GtkUI, LV2_UI__GtkUI, "Calf");
    const unsigned long parentWindow = argc > 5 ? QString::fromUtf8(argv[5]).toULongLong() : 0;
    if (!helper.start(QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4]), parentWindow)) {
        return 3;
    }
    return app.exec();
}

int runX11UIHelper(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) return 2;
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    X11Helper helper;
    const unsigned long parentWindow = argc > 5 ? QString::fromUtf8(argv[5]).toULongLong() : 0;
    if (!helper.start(QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4]), parentWindow)) {
        return 3;
    }
    return app.exec();
}
