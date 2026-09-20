#include "PluginPreviewService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QPointer>
#include <QTextStream>
#include <QTimer>
#include <csignal>
#include <unistd.h>

namespace {

// The mark for a plugin that has no window at all. Retrying cannot help, so it
// survives "try the failed ones again".
const QString kNoWindow = QStringLiteral("no window of its own");

QString failedFilePath() {
    return PluginPreviewService::cacheDir() + "/failed.json";
}

QJsonObject readFailures() {
    QFile file(failedFilePath());
    if (!file.open(QFile::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeFailure(const QString& uri, const QString& reason) {
    QJsonObject failures = readFailures();
    failures[uri] = reason;
    QFile file(failedFilePath());
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) return;
    file.write(QJsonDocument(failures).toJson(QJsonDocument::Compact));
}

// A display number no X server is using.
int freeDisplayNumber() {
    for (int display = 90; display < 120; ++display) {
        if (!QFile::exists(QString("/tmp/.X%1-lock").arg(display))) return display;
    }
    return -1;
}

}  // namespace

PluginPreviewService::PluginPreviewService(QObject* parent) : QObject(parent) {}

PluginPreviewService::~PluginPreviewService() {
    shutdown();
}

QString PluginPreviewService::cacheDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base + "/plugin-previews";
}

QString PluginPreviewService::pathFor(const QString& uri) {
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Sha1).toHex());
    return cacheDir() + "/" + hash + ".png";
}

bool PluginPreviewService::hasPreview(const QString& uri) {
    return QFile::exists(pathFor(uri));
}

bool PluginPreviewService::hasFailed(const QString& uri) {
    return readFailures().contains(uri);
}

void PluginPreviewService::forgetFailures() {
    const QJsonObject failures = readFailures();
    QJsonObject kept;
    for (auto it = failures.constBegin(); it != failures.constEnd(); ++it) {
        if (it.value().toString() == kNoWindow) kept[it.key()] = it.value();
    }
    QFile file(failedFilePath());
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) return;
    file.write(QJsonDocument(kept).toJson(QJsonDocument::Compact));
}

int PluginPreviewService::failedCount() {
    const QJsonObject failures = readFailures();
    int count = 0;
    for (auto it = failures.constBegin(); it != failures.constEnd(); ++it) {
        if (it.value().toString() != kNoWindow) ++count;
    }
    return count;
}

PluginPreviewService::Environment PluginPreviewService::environment() {
    Environment result;
    // Xvfb is the lightest: a display no one can see, and no compositor.
    if (!QStandardPaths::findExecutable("Xvfb").isEmpty()) {
        result.available = true;
        result.tool = "Xvfb";
        return result;
    }
    // A compositor that can open a virtual session does the same job, so some
    // desktops need nothing installed.
    if (!QStandardPaths::findExecutable("kwin_wayland").isEmpty()) {
        result.available = true;
        result.tool = "kwin_wayland";
    }
    return result;
}

QString PluginPreviewService::requirementsText() {
    return "RigRoom opens each plugin's own window out of sight, takes a picture and keeps it. "
           "Pictures appear in the browser's info panel; the list keeps its category cards.\n"
           "Needs a hidden display: Xvfb (package xorg-x11-server-Xvfb on most systems), or a "
           "compositor that can open a virtual session (kwin_wayland). Nothing appears on screen.\n"
           "Plugins with a window of their own are covered (X11 and Gtk). A plugin with no window, "
           "or one that will not open, keeps its category card and is not tried again.";
}

void PluginPreviewService::markImpossible(const QStringList& uris, const QString& reason) {
    if (uris.isEmpty()) return;
    QDir().mkpath(cacheDir());
    QJsonObject failures = readFailures();
    for (const QString& uri : uris) {
        if (!failures.contains(uri)) failures[uri] = reason;
    }
    QFile file(failedFilePath());
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) return;
    file.write(QJsonDocument(failures).toJson(QJsonDocument::Compact));
}

int PluginPreviewService::countTodo(const QStringList& uris, Mode mode) {
    const QJsonObject failures = readFailures();
    int count = 0;
    for (const QString& uri : uris) {
        if (failures.value(uri).toString() == kNoWindow) continue;
        if (mode == Mode::All) { ++count; continue; }
        if (hasPreview(uri)) continue;
        if (mode == Mode::Missing && failures.contains(uri)) continue;
        ++count;
    }
    return count;
}

void PluginPreviewService::generate(const QStringList& uris, Mode mode) {
    if (m_process) return;
    const Environment env = environment();
    if (!env.available) {
        emit finished(0, uris.size(), "No hidden display is available on this computer.");
        return;
    }
    QDir().mkpath(cacheDir());
    // Clearing the error marks first is what makes a retry actually retry; the
    // "no window of its own" marks survive, since retrying those is pointless.
    if (mode != Mode::Missing) forgetFailures();

    const QJsonObject failures = readFailures();
    QStringList todo;
    for (const QString& uri : uris) {
        if (failures.value(uri).toString() == kNoWindow) continue;
        if (mode != Mode::All && hasPreview(uri)) continue;
        if (mode == Mode::Missing && failures.contains(uri)) continue;
        todo << uri;
    }
    if (todo.isEmpty()) {
        const int failed = failedCount();
        emit finished(0, uris.size(),
                      failed > 0
                          ? QString("Nothing to do. %1 plugins failed before: choose \"Also the ones that "
                                    "failed before\" to try them again.").arg(failed)
                          : QString("Nothing to do: every plugin that can have a picture has one."));
        return;
    }

    m_jobFile = cacheDir() + "/queue.txt";
    QFile job(m_jobFile);
    if (!job.open(QFile::WriteOnly | QFile::Truncate)) {
        emit finished(0, uris.size(), "The cache folder could not be written to.");
        return;
    }
    QTextStream stream(&job);
    for (const QString& uri : todo) stream << uri << "\n";
    job.close();

    m_total = todo.size();
    m_done = 0;
    m_captured = 0;

    // One hidden display for the whole run; inside it, each plugin still gets
    // its own process, so a GUI that crashes only loses its own preview.
    const QString self = QCoreApplication::applicationFilePath();
    const QString batch = QString("\"%1\" --plugin-snapshot-batch \"%2\" \"%3\"")
                              .arg(self, m_jobFile, cacheDir());
    QString command;
    if (env.tool == "Xvfb") {
        const int display = freeDisplayNumber();
        if (display < 0) {
            emit finished(0, uris.size(), "No free display number for the hidden display.");
            return;
        }
        command = QString("Xvfb :%1 -screen 0 3200x2000x24 -nolisten tcp >/dev/null 2>&1 & "
                          "xvfb_pid=$!; sleep 1; DISPLAY=:%1 %2; kill $xvfb_pid 2>/dev/null")
                      .arg(display)
                      .arg(batch);
    } else {
        // Only the compositor's own chatter is dropped: the batch runner shares
        // this stdout and its progress lines have to reach us.
        command = QString("kwin_wayland --virtual --width 3200 --height 2000 --xwayland "
                          "--exit-with-session='%1' 2>/dev/null")
                      .arg(batch);
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    // Its own process group: cancelling then takes the hidden display and every
    // helper with it, instead of leaving them behind.
    m_process->setChildProcessModifier([]() { setsid(); });
    connect(m_process, &QProcess::readyRead, this, &PluginPreviewService::readOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                readOutput();
                const int captured = m_captured;
                const int skipped = m_total - m_captured;
                const bool silent = m_done == 0 && !m_cancelling;
                const bool cancelled = m_cancelling;
                const QString log = m_log;
                cleanUp();
                if (cancelled) {
                    emit finished(captured, skipped,
                                  QString("Cancelled. %1 pictures were made before stopping.").arg(captured));
                    return;
                }
                // Nothing came back at all: the hidden display never started.
                emit finished(captured, skipped,
                              silent ? QString("The hidden display did not start (exit %1). Last output: %2")
                                           .arg(exitCode)
                                           .arg(log.right(200).trimmed())
                                     : QString());
            });
    // Previews must never compete with audio: the run stays at the lowest priority.
    m_process->start("/bin/sh", {"-c", "nice -n 19 sh -c " + QString("'%1'").arg(QString(command).replace("'", "'\\''"))});
}

void PluginPreviewService::readOutput() {
    if (!m_process) return;
    while (m_process->canReadLine()) {
        const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (m_done == 0 && !line.isEmpty()) m_log = line;  // kept in case nothing else arrives
        // The batch runner reports one line per plugin: PREVIEW OK|FAIL <uri> [reason]
        if (!line.startsWith("PREVIEW ")) continue;
        const QStringList parts = line.mid(8).split(' ');
        if (parts.size() < 2) continue;
        const QString status = parts[0];
        const QString uri = parts[1];
        const QString reason = parts.mid(2).join(' ');
        if (status == "START") {
            // Shown while the plugin is being opened, which can take a while.
            emit started(uri);
            continue;
        }
        ++m_done;
        if (status == "OK") ++m_captured;
        else writeFailure(uri, reason.isEmpty() ? QString("no preview") : reason);
        emit progress(m_done, m_total, uri);
    }
}

void PluginPreviewService::cancel() {
    if (!m_process || m_cancelling) return;
    m_cancelling = true;
    // Asking the run to stop lets the hidden display shut itself down. The
    // plugin it is on is given up on, so this takes a few seconds; the GUI is
    // not made to wait for it.
    QFile stop(cacheDir() + "/cancel");
    if (stop.open(QFile::WriteOnly)) stop.close();

    // If it has not stopped by itself, take it down the hard way.
    QPointer<PluginPreviewService> self(this);
    QTimer::singleShot(30000, this, [self]() {
        if (!self || !self->m_process) return;
        const qint64 pid = self->m_process->processId();
        if (pid > 0) ::kill(static_cast<pid_t>(-pid), SIGKILL);
        self->m_process->kill();
    });
}

void PluginPreviewService::shutdown() {
    if (!m_process) return;
    m_process->disconnect(this);
    const qint64 pid = m_process->processId();
    if (pid > 0) ::kill(static_cast<pid_t>(-pid), SIGKILL);
    m_process->kill();
    m_process->waitForFinished(2000);
    cleanUp();
}

void PluginPreviewService::cleanUp() {
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (!m_jobFile.isEmpty()) {
        QFile::remove(m_jobFile);
        m_jobFile.clear();
    }
    QFile::remove(cacheDir() + "/cancel");
    m_cancelling = false;
}
