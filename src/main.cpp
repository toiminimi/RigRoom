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
#include <csignal>
#include <fstream>
#include <iostream>
#include <execinfo.h>

static void crash_handler(int sig) {
    std::ofstream log("crash_debug.txt");
    log << "RigRoom crashed with signal: " << sig << std::endl;
    
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
