// Filling in TONE3000 details for captures on disk. Kept apart from
// CaptureLibrary.cpp so the library itself builds without networking.
#include "CaptureLibrary.h"
#include "Tone3000Api.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr int kParallel = 2;
}

void CaptureLibrary::enrich() {
    if (!Tone3000Api::hasKey()) return;
    // Candidates too: the import dialog shows their tones.
    for (const QList<File>* list : {&m_files, &m_candidates}) {
        for (const File& f : *list) {
            if (f.toneId == 0 || !f.tone.isEmpty() || m_enrichAttempted.contains(f.toneId)) continue;
            m_enrichAttempted.insert(f.toneId);
            m_enrichQueue << f.toneId;
        }
    }
    while (m_enriching < kParallel && !m_enrichQueue.isEmpty()) enrichNext();
}

void CaptureLibrary::enrichNext() {
    if (m_enrichQueue.isEmpty()) return;
    const int id = m_enrichQueue.takeFirst();
    ++m_enriching;
    Tone3000Api* api = Tone3000Api::instance();
    auto finish = [this]() {
        --m_enriching;
        if (!m_enrichQueue.isEmpty()) {
            enrichNext();
        } else if (m_enriching == 0) {
            rescan(); // pick up the new tone.json files
        }
    };
    api->getJson(QUrl(QString("https://www.tone3000.com/api/v1/tones/%1").arg(id)), this,
                 [this, id, api, finish](const Tone3000Api::Response& toneResponse) {
        if (!toneResponse.ok || toneResponse.object.value("id").toInt() != id) {
            finish();
            return;
        }
        const QJsonObject tone = toneResponse.object;
        api->fetchAllModels(id, this, [this, tone, finish](bool ok, const QJsonArray& models, const QString&) {
            Tone3000::Format format = Tone3000::Format::Nam;
            Tone3000::formatOf(tone, &format);
            const QString folder = m_cacheDir + (format == Tone3000::Format::Ir ? "/ir/" : "/")
                + Tone3000::toneFolderName(tone);
            QDir().mkpath(folder);
            QSaveFile file(folder + "/tone.json");
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(QJsonObject{{"tone", tone}, {"models", ok ? models : QJsonArray()}}).toJson());
                file.commit();
            }
            finish();
        });
    }, {}, QString("tone|%1").arg(id));
}
