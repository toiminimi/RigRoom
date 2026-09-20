#pragma once
#include <QObject>
#include <QStringList>

class QProcess;

// Makes the browser's preview images by opening each plugin's own GUI in a
// throwaway process on a hidden display, capturing it and caching the PNG.
//
// This is optional and off by default: it needs a headless display server
// (Xvfb, or kwin_wayland's virtual backend) and some plugin GUIs misbehave when
// opened without a host, so a run is never started unless asked for.
class PluginPreviewService : public QObject {
    Q_OBJECT
public:
    explicit PluginPreviewService(QObject* parent = nullptr);
    ~PluginPreviewService() override;

    // Which hidden display we can use here, and a sentence for the Settings page.
    struct Environment {
        bool available = false;
        QString tool;         // "Xvfb" or "kwin_wayland"
    };
    static Environment environment();
    // What this needs, wherever it runs. Not a report about this computer.
    static QString requirementsText();

    static QString cacheDir();                       // ~/.cache/RigRoom/plugin-previews
    static QString pathFor(const QString& uri);      // <cacheDir>/<sha1>.png
    static bool hasPreview(const QString& uri);
    // Plugins tried before and found to have no GUI, or whose GUI would not
    // open. They are never retried unless the user asks for a fresh run.
    static bool hasFailed(const QString& uri);
    // Clears the plugins whose GUI would not open. Plugins with no window of
    // their own keep their mark: retrying those can never help.
    static void forgetFailures();
    static int failedCount();   // errors only, not the ones without a window
    // Records plugins that cannot have a picture (no window of their own), so
    // that asking for the missing ones does not walk through them every time.
    static void markImpossible(const QStringList& uris, const QString& reason);

    // What a run covers.
    enum class Mode {
        Missing,   // plugins never tried
        Failed,    // those, plus the ones whose GUI would not open
        All,       // every plugin with a window, replacing the pictures it has
    };
    // How many plugins a run would work through.
    static int countTodo(const QStringList& uris, Mode mode);

    bool running() const { return m_process != nullptr; }
    void generate(const QStringList& uris, Mode mode);
    // Asks the run to stop and returns at once; `finished` arrives when it has.
    // The plugin being worked on is given up, so it can take a few seconds.
    void cancel();
    bool cancelling() const { return m_cancelling; }
    // Used when the application is closing: stops everything right away.
    void shutdown();

signals:
    void started(const QString& uri);   // this plugin is being opened now
    void progress(int done, int total, const QString& uri);
    // `note` is empty on a normal run, otherwise it says why nothing happened.
    void finished(int captured, int skipped, const QString& note);

private:
    void readOutput();
    void cleanUp();

    QProcess* m_process = nullptr;
    QString m_jobFile;
    int m_total = 0;
    int m_done = 0;
    int m_captured = 0;
    QString m_log;
    bool m_cancelling = false;
};
