#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>

// What a .nam file says about itself, read from the head of the file without
// parsing the weights, and the tags that can be guessed from it.
namespace NamMetadata {

struct Info {
    bool valid = false;
    QString name;
    QString modeledBy;
    QString gearType;   // as written: amp, amp_cab, full-rig, pedal…
    QString gearMake;
    QString gearModel;
    QString toneType;   // free text: clean, metal, hi_gain…
    QString architecture; // WaveNet, LSTM, SlimmableContainer…
    QString size;       // standard / lite / feather / nano, when it can tell
    double loudness = 0;
    bool calibrated = false;
};

// Reads the metadata of a .nam file: only its first and last few kilobytes.
Info read(const QString& path);
// Parses the start and the end of a .nam file (the whole file if it is small
// enough to be in `head`). The metadata may be at either end.
Info parseParts(const QByteArray& head, const QByteArray& tail = {});
Info parse(const QJsonObject& root);

// "A2" for the slimmable models TONE3000 trains now, "A1" for the older ones.
QString architectureLabel(const Info& info);
// "amp", "amp-cab", "pedal" or "outboard", from the file's gear type.
QString normalizedGear(const QString& gearType);

// The fixed vocabulary auto tags are drawn from, in groups the library
// filters by. Tags the user types freely are Other.
enum class TagCategory { Type, Tone, Brand, Tech, Other };
TagCategory tagCategory(const QString& tag);
QStringList vocabulary(TagCategory category);
// The vocabulary tag a word or phrase stands for ("Hi Gain" -> "high-gain",
// "Full Rig" -> "amp-cab"); empty when it is not in the vocabulary.
QString vocabularyTag(const QString& text);
// Short label for showing a tag ("amp-cab" -> "Amp + Cab", "a2" -> "A2").
QString tagLabel(const QString& tag);

// Lower-case, hyphenated tag with common spellings merged
// ("Hi Gain" -> "high-gain"). Empty for placeholders such as "T3K-Null".
QString normalizeTag(const QString& tag);
// Vocabulary tags guessed from the file's metadata and its name.
QStringList autoTags(const Info& info, const QString& fileName);
// Tags guessed from words in any text (file names, titles).
QStringList tagsFromText(const QString& text);

} // namespace NamMetadata
