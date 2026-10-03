#pragma once
#include "../audio/CaptureNode.h"
#include "Tone3000Types.h"
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

// The capture library seen as sounds for the built-in capture block: NAM
// captures and IRs together, sorted into what a guitarist adds to a chain.
namespace CaptureSounds {

// What a file is.
enum class Type { Amp, FullRig, Pedal, Outboard, Cab, Room, Other };

// What the Add menu offers; each makes one block.
enum class Category { AmpCab, Amp, Cab, Pedal, Room };

struct Item {
    QString path;
    Tone3000::Format format = Tone3000::Format::Nam;
    Type type = Type::Other;
    QString name;
    QString variant; // which of a tone's captures this is, if it has several
    QString creator;
    QString imageUrl;
    QString make;
    QString model;
    QString description;
    QString architecture;
    QString sourceUrl;
    QStringList tags;
    int toneId = 0;
    int rating = 0;
    qint64 lastUsed = 0;
    double loudness = 0.0;
    bool hasLoudness = false;
};

// Every usable file in the library, most recently used first.
QList<Item> items();
bool inCategory(const Item& item, Category category);
QList<Item> itemsIn(Category category);
// The IR used most recently, if any.
QString lastUsedCab();

QString typeLabel(Type type);     // "Amp", "Full rig", "Cab", "Room"...
// "Make Model", without repeating a make the model already starts with.
QString gearText(const QString& make, const QString& model);
QColor typeColor(Type type);
QString categoryTitle(Category category);
QString categoryHint(Category category);
// What kind of item the category starts from, for its artwork.
Type categoryType(Category category);

// The block's types, from what it holds.
Type modelType(const CaptureNode& node);
Type irType(const CaptureNode& node);

// Drawn artwork for items without a picture: amp, stompbox, rack, speaker, room.
QPixmap artwork(Type type, const QSize& size, qreal dpr);

// Loads an item into the block (model or IR stage) with its details.
bool apply(CaptureNode& node, const Item& item, QString* error = nullptr);
// Records the choice as used, so it comes first next time.
void markUsed(const Item& item);

// What a block holds, so a preview can be undone.
struct Snapshot {
    std::string modelPath, irPath;
    AudioNode::ModelMetadata metadata;
    std::string displayName, sourceUrl;
    std::vector<AudioNode::ModelVariant> variants;
    CaptureNode::IrInfo irInfo;
    std::vector<std::pair<uint32_t, float>> params;
};
Snapshot snapshot(CaptureNode& node);
void restore(CaptureNode& node, const Snapshot& snapshot);

} // namespace CaptureSounds
