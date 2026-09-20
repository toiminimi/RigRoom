#include <QApplication>
#include <QDir>
#include <QLibrary>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
extern "C" int XInitThreads(void);
#include "ui/MainWindow.h"
#include "ui/GtkUIHelper.h"
#include <suil/suil.h>
#include "ui/PluginSnapshot.h"
#include "Version.h"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <execinfo.h>
#include <link.h>

// Backtrace lines carry only module offsets, so a report is only useful
// together with the exact binary it came from.  The build ID names that binary.
static char g_buildId[41] = "unknown";

static int capture_build_id(struct dl_phdr_info* info, size_t, void*) {
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& header = info->dlpi_phdr[i];
        if (header.p_type != PT_NOTE) continue;
        const char* cursor = reinterpret_cast<const char*>(info->dlpi_addr + header.p_vaddr);
        const char* end = cursor + header.p_memsz;
        while (cursor + sizeof(ElfW(Nhdr)) <= end) {
            const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(cursor);
            const char* name = cursor + sizeof(ElfW(Nhdr));
            const auto* desc = reinterpret_cast<const unsigned char*>(name + ((note->n_namesz + 3) & ~3u));
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                const size_t bytes = note->n_descsz < 20 ? note->n_descsz : 20;
                for (size_t b = 0; b < bytes; ++b) std::snprintf(g_buildId + b * 2, 3, "%02x", desc[b]);
                return 1;
            }
            cursor = reinterpret_cast<const char*>(desc) + ((note->n_descsz + 3) & ~3u);
        }
    }
    return 1;  // The first object is the executable itself; no need to go further.
}

static void crash_handler(int sig) {
    std::ofstream log("crash_debug.txt");
    log << "RigRoom crashed with signal: " << sig << std::endl;
    log << "Version: " << RIGROOM_VERSION_STRING << std::endl;
    log << "Build ID: " << g_buildId << std::endl;

    void* array[50];
    size_t size = backtrace(array, 50);
    char** symbols = backtrace_symbols(array, size);
    
    log << "Backtrace:" << std::endl;
    for (size_t i = 0; i < size; ++i) {
        log << symbols[i] << std::endl;
    }
    free(symbols);
    log.close();
    
    std::cerr << "RigRoom crashed with signal " << sig << ". Backtrace saved to crash_debug.txt." << std::endl;
    
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

int main(int argc, char* argv[]) {
    XInitThreads();
    if (argc > 1 && std::string(argv[1]) == "--gtk-ui-helper") {
        return runGtkUIHelper(argc, argv);
    }
    if (argc > 1 && std::string(argv[1]) == "--x11-ui-helper") {
        return runX11UIHelper(argc, argv);
    }
    if (argc > 1 && std::string(argv[1]) == "--plugin-snapshot") {
        return runPluginSnapshot(argc, argv);
    }
    if (argc > 1 && std::string(argv[1]) == "--plugin-snapshot-batch") {
        return runPluginSnapshotBatch(argc, argv);
    }
    dl_iterate_phdr(capture_build_id, nullptr);
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGABRT, crash_handler);
    std::signal(SIGFPE, crash_handler);
    std::signal(SIGILL, crash_handler);

    // Suil's available Qt6 adapter embeds X11 LV2 UIs.  On a Wayland session,
    // use XWayland so QWidget::winId() is a real X11 window ID for the plugin.
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("GDK_BACKEND", "x11");
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        qputenv("QT_SCALE_FACTOR", "1");
    }

    // Suil module directory auto-detection across distros & AppImage
    if (qEnvironmentVariableIsEmpty("SUIL_MODULE_DIR")) {
        const QString appDirSuil = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteDir().filePath("../lib/suil-0");
        if (QDir(appDirSuil).exists()) {
            qputenv("SUIL_MODULE_DIR", appDirSuil.toUtf8());
        } else if (QDir("/usr/lib/x86_64-linux-gnu/suil-0").exists()) {
            qputenv("SUIL_MODULE_DIR", "/usr/lib/x86_64-linux-gnu/suil-0");
        } else if (QDir("/usr/lib64/suil-0").exists()) {
            qputenv("SUIL_MODULE_DIR", "/usr/lib64/suil-0");
        }
    }
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/branding/rigroom-icon.png"));
    
    MainWindow window;
    window.setWindowIcon(app.windowIcon());
    window.show();
    
    return app.exec();
}
