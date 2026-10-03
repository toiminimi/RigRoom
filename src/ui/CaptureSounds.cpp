#include "CaptureSounds.h"
#include "CaptureLibrary.h"
#include "NamMetadata.h"
#include "Tone3000ImageLoader.h"
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace CaptureSounds {
namespace {

Type namType(const QString& gear) {
    const QString g = NamMetadata::normalizedGear(gear);
    if (g == "amp") return Type::Amp;
    if (g == "amp-cab") return Type::FullRig;
    if (g == "pedal") return Type::Pedal;
    if (g == "outboard") return Type::Outboard;
    return Type::Other;
}

// IR uploads use their own words: cab, space, pedal, outboard.
Type irTypeOf(const QString& gear) {
    const QString g = gear.toLower();
    if (g.contains("space") || g.contains("room") || g.contains("reverb") || g.contains("hall")
        || g.contains("pedal") || g.contains("outboard")) {
        return Type::Room;
    }
    return Type::Cab;
}

// The type tag of a library file: the user's, else one of its tags.
QString gearTag(const CaptureLibrary& library, const CaptureLibrary::File& file) {
    const QString user = library.userData(file.path).gear.type;
    if (!user.isEmpty()) return user;
    static const QStringList kTypes = {"amp-cab", "amp", "pedal", "outboard", "cab", "space"};
    const QStringList tags = library.tags(file);
    for (const QString& type : kTypes) {
        if (tags.contains(type, Qt::CaseInsensitive)) return type;
    }
    return file.nam.gearType;
}

std::string gearString(Type type) {
    switch (type) {
    case Type::Amp: return "amp";
    case Type::FullRig: return "amp-cab";
    case Type::Pedal: return "pedal";
    case Type::Outboard: return "outboard";
    case Type::Cab: return "cab";
    case Type::Room: return "space";
    default: return "";
    }
}

void dots(QPainter& p, const QRectF& area, qreal step, const QColor& color) {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    int row = 0;
    for (qreal y = area.top() + step / 2; y < area.bottom(); y += step, ++row)
        for (qreal x = area.left() + (row % 2 ? step : step / 2); x < area.right(); x += step)
            p.drawEllipse(QPointF(x, y), step * 0.2, step * 0.2);
}
} // namespace

QList<Item> items() {
    CaptureLibrary& library = CaptureLibrary::instance();
    QList<Item> result;
    for (const Tone3000::Format format : {Tone3000::Format::Nam, Tone3000::Format::Ir}) {
        for (const CaptureLibrary::File& file : library.files(format)) {
            if (file.missing) continue;
            const CaptureLibrary::UserData user = library.userData(file.path);
            Item item;
            item.path = file.path;
            item.format = format;
            const QString gear = gearTag(library, file);
            item.type = format == Tone3000::Format::Nam ? namType(gear) : irTypeOf(gear);
            item.name = library.displayName(file);
            item.variant = file.variantName;
            if (item.variant.compare(item.name, Qt::CaseInsensitive) == 0) item.variant.clear();
            item.creator = library.creator(file);
            item.imageUrl = Tone3000ImageLoader::firstImageUrl(file.tone);
            item.make = !user.gear.make.isEmpty() ? user.gear.make : file.nam.gearMake;
            item.model = !user.gear.model.isEmpty() ? user.gear.model : file.nam.gearModel;
            item.description = file.tone.value("description").toString();
            item.architecture = file.nam.architecture;
            item.tags = library.tags(file);
            item.toneId = file.toneId;
            if (!file.tone.isEmpty()) item.sourceUrl = Tone3000::ToneItem::fromJson(file.tone).url;
            item.rating = user.rating;
            item.lastUsed = user.lastUsed;
            item.hasLoudness = file.nam.valid && file.nam.loudness != 0.0;
            item.loudness = file.nam.loudness;
            result << item;
        }
    }
    // Several files with one name (a tone's captures imported without their
    // variant names): tell them apart by file name.
    QHash<QString, int> names;
    for (const Item& item : result) ++names[item.name.toLower()];
    for (Item& item : result) {
        if (!item.variant.isEmpty() || names.value(item.name.toLower()) < 2) continue;
        const QString stem = QFileInfo(item.path).completeBaseName();
        if (stem.compare(item.name, Qt::CaseInsensitive) != 0) item.variant = stem;
    }
    std::stable_sort(result.begin(), result.end(), [](const Item& a, const Item& b) {
        if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool inCategory(const Item& item, Category category) {
    const bool nam = item.format == Tone3000::Format::Nam;
    switch (category) {
    case Category::AmpCab: return nam && (item.type == Type::Amp || item.type == Type::FullRig || item.type == Type::Other);
    case Category::Amp: return nam && (item.type == Type::Amp || item.type == Type::Other);
    case Category::Pedal: return nam && (item.type == Type::Pedal || item.type == Type::Outboard);
    case Category::Cab: return !nam && item.type == Type::Cab;
    case Category::Room: return !nam && item.type == Type::Room;
    }
    return false;
}

QList<Item> itemsIn(Category category) {
    QList<Item> result;
    for (const Item& item : items())
        if (inCategory(item, category)) result << item;
    return result;
}

QString lastUsedCab() {
    for (const Item& item : items()) {
        if (item.type == Type::Cab && item.lastUsed > 0) return item.path;
    }
    return {};
}

QString gearText(const QString& make, const QString& model) {
    const QString m = model.trimmed(), k = make.trimmed();
    if (k.isEmpty() || m.startsWith(k, Qt::CaseInsensitive)) return m;
    if (m.isEmpty() || k.startsWith(m, Qt::CaseInsensitive)) return k;
    return k + " " + m;
}

QString typeLabel(Type type) {
    switch (type) {
    case Type::Amp: return "Amp";
    case Type::FullRig: return "Full rig";
    case Type::Pedal: return "Pedal";
    case Type::Outboard: return "Outboard";
    case Type::Cab: return "Cab";
    case Type::Room: return "Room";
    default: return "Capture";
    }
}

QColor typeColor(Type type) {
    switch (type) {
    case Type::Amp: return QColor("#E0913A");
    case Type::FullRig: return QColor("#E06A3A");
    case Type::Pedal: return QColor("#4CC38A");
    case Type::Outboard: return QColor("#9C7BE0");
    case Type::Cab: return QColor("#8FA3B8");
    case Type::Room: return QColor("#7E6BD9");
    default: return QColor("#6FA8DC");
    }
}

QString categoryTitle(Category category) {
    switch (category) {
    case Category::AmpCab: return "Amp + Cab";
    case Category::Amp: return "Amp";
    case Category::Cab: return "Cab";
    case Category::Pedal: return "Pedal";
    case Category::Room: return "Room / Reverb";
    }
    return {};
}

QString categoryHint(Category category) {
    switch (category) {
    case Category::AmpCab: return "An amp capture with its cab, ready to play";
    case Category::Amp: return "Amp capture only, the cab comes after it";
    case Category::Cab: return "A cabinet impulse response";
    case Category::Pedal: return "Drive, boost or outboard captures";
    case Category::Room: return "Room, reverb and effect IRs";
    }
    return {};
}

Type categoryType(Category category) {
    switch (category) {
    case Category::AmpCab: return Type::FullRig;
    case Category::Amp: return Type::Amp;
    case Category::Cab: return Type::Cab;
    case Category::Pedal: return Type::Pedal;
    case Category::Room: return Type::Room;
    }
    return Type::Other;
}

Type modelType(const CaptureNode& node) {
    return node.modelPath().empty() ? Type::Other : namType(QString::fromStdString(node.getModelMetadata().gearType));
}

Type irType(const CaptureNode& node) {
    return irTypeOf(QString::fromStdString(node.irInfo().gearType));
}

QPixmap artwork(Type type, const QSize& size, qreal dpr) {
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal w = size.width(), h = size.height(), u = std::min(w, h) / 132.0;
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, w, h), 8 * u, 8 * u);
    p.setClipPath(clip);
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0, QColor("#1A1C20"));
    bg.setColorAt(1, QColor("#111215"));
    p.fillRect(QRectF(0, 0, w, h), bg);
    const QColor accent = typeColor(type);
    const QPointF c(w / 2, h / 2);

    switch (type) {
    case Type::Pedal: {
        const QRectF box(c.x() - 36 * u, c.y() - 54 * u, 72 * u, 108 * u);
        p.setPen(QPen(accent.darker(140), 2 * u));
        p.setBrush(QColor("#22252A"));
        p.drawRoundedRect(box, 8 * u, 8 * u);
        for (int i = 0; i < 2; ++i) {
            const QPointF k(box.left() + (20 + i * 32) * u, box.top() + 24 * u);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#0E0F11"));
            p.drawEllipse(k, 10 * u, 10 * u);
            p.setPen(QPen(accent.lighter(130), 2 * u));
            p.drawLine(k, k + QPointF((i ? 5 : -5) * u, -6 * u));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QPointF(c.x(), box.top() + 52 * u), 3.5 * u, 3.5 * u);
        p.setBrush(QColor("#9AA0A8"));
        p.drawEllipse(QPointF(c.x(), box.bottom() - 24 * u), 11 * u, 11 * u);
        p.setBrush(QColor("#6C727A"));
        p.drawEllipse(QPointF(c.x(), box.bottom() - 24 * u), 7 * u, 7 * u);
        break;
    }
    case Type::Outboard: {
        // A rack unit: faceplate, ears, meters and knobs.
        const QRectF unit(8 * u, c.y() - 24 * u, w - 16 * u, 48 * u);
        p.setPen(QPen(accent.darker(150), 1.5 * u));
        p.setBrush(QColor("#22252A"));
        p.drawRoundedRect(unit, 4 * u, 4 * u);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#0E0F11"));
        p.drawRect(QRectF(unit.left() + 12 * u, unit.top() + 10 * u, 30 * u, 28 * u));
        p.setBrush(accent);
        p.drawRect(QRectF(unit.left() + 16 * u, unit.top() + 26 * u, 22 * u, 3 * u));
        for (int i = 0; i < 3; ++i) {
            p.setBrush(QColor("#0E0F11"));
            p.drawEllipse(QPointF(unit.left() + (60 + i * 20) * u, unit.center().y()), 7 * u, 7 * u);
        }
        break;
    }
    case Type::Cab: {
        dots(p, QRectF(0, 0, w, h), 7 * u, QColor("#22252A"));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent.darker(160), 3 * u));
        p.drawEllipse(c, 46 * u, 46 * u);
        p.setPen(QPen(accent.darker(115), 2 * u));
        p.drawEllipse(c, 30 * u, 30 * u);
        p.setPen(Qt::NoPen);
        p.setBrush(accent.darker(125));
        p.drawEllipse(c, 12 * u, 12 * u);
        break;
    }
    case Type::Room: {
        const QPointF src(w * 0.24, c.y());
        p.setBrush(Qt::NoBrush);
        for (int i = 1; i <= 5; ++i) {
            QColor ring = accent;
            ring.setAlphaF(1.0 - i * 0.16);
            p.setPen(QPen(ring, 2.5 * u));
            const qreal r = i * 19 * u;
            p.drawArc(QRectF(src.x() - r, src.y() - r, 2 * r, 2 * r), -60 * 16, 120 * 16);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(src, 6 * u, 6 * u);
        break;
    }
    default: {
        // Amp head over a grille; a full rig shows a cab under it.
        const bool rig = type == Type::FullRig;
        const qreal panel = (rig ? 38 : 46) * u;
        p.fillRect(QRectF(0, 0, w, panel), QColor("#1F2126"));
        p.fillRect(QRectF(0, panel - 2 * u, w, 3 * u), accent);
        for (int i = 0; i < 5; ++i) {
            const QPointF k(w / 2 + (i - 2) * 24 * u, panel / 2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#0E0F11"));
            p.drawEllipse(k, 8 * u, 8 * u);
            p.setPen(QPen(accent.lighter(130), 2 * u));
            p.drawLine(k, k + QPointF(std::cos(-2.2 + i * 0.9) * 6 * u, std::sin(-2.2 + i * 0.9) * 6 * u));
        }
        dots(p, QRectF(0, panel + 4 * u, w, h - panel - 4 * u), 7 * u, QColor("#24272C"));
        if (rig) {
            p.setBrush(Qt::NoBrush);
            const QPointF sp(c.x(), panel + (h - panel) / 2);
            p.setPen(QPen(accent.darker(150), 2.5 * u));
            p.drawEllipse(sp, 30 * u, 30 * u);
            p.setPen(QPen(accent.darker(120), 2 * u));
            p.drawEllipse(sp, 12 * u, 12 * u);
        }
        break;
    }
    }
    return pm;
}

bool apply(CaptureNode& node, const Item& item, QString* error) {
    std::string message;
    if (item.format == Tone3000::Format::Nam) {
        if (!node.loadModel(item.path.toStdString(), &message)) {
            if (error) *error = QString::fromStdString(message);
            return false;
        }
        AudioNode::ModelMetadata meta;
        meta.toneId = item.toneId > 0 ? std::to_string(item.toneId) : "";
        meta.toneTitle = item.name.toStdString();
        meta.author = item.creator.toStdString();
        meta.gearType = gearString(item.type);
        meta.gearMake = item.make.toStdString();
        meta.gearModel = item.model.toStdString();
        meta.imageUrl = item.imageUrl.toStdString();
        meta.description = item.description.toStdString();
        meta.architecture = item.architecture.toStdString();
        meta.tags = item.tags.join(", ").toStdString();
        meta.loudness = item.loudness;
        node.setModelMetadata(meta);
        node.setModelDisplayName((item.variant.isEmpty() ? item.name : item.name + " - " + item.variant).toStdString());
        node.setModelSourceUrl(item.sourceUrl.toStdString());
        node.setModelVariants({});
    } else {
        const bool hadIr = !node.irPath().empty();
        if (!node.loadIr(item.path.toStdString(), &message)) {
            if (error) *error = QString::fromStdString(message);
            return false;
        }
        CaptureNode::IrInfo info;
        info.name = item.name.toStdString();
        info.imageUrl = item.imageUrl.toStdString();
        info.gearType = gearString(item.type);
        info.sourceUrl = item.sourceUrl.toStdString();
        node.setIrInfo(info);
        node.setParameter(CaptureNode::CabEnabled, 1.0f);
        // A cab is the sound; a room starts as a blend (kept when only swapping rooms).
        if (item.type == Type::Cab) node.setParameter(CaptureNode::IrMix, 1.0f);
        else if (!hadIr || irType(node) != Type::Room) node.setParameter(CaptureNode::IrMix, 0.3f);
    }
    return true;
}

void markUsed(const Item& item) {
    CaptureLibrary::instance().markUsed(item.path);
}

Snapshot snapshot(CaptureNode& node) {
    Snapshot s;
    s.modelPath = node.modelPath();
    s.irPath = node.irPath();
    s.metadata = node.getModelMetadata();
    s.displayName = node.getModelDisplayName();
    s.sourceUrl = node.getModelSourceUrl();
    s.variants = node.getModelVariants();
    s.irInfo = node.irInfo();
    for (const auto& p : node.getControlPorts()) s.params.emplace_back(p.index, p.value);
    return s;
}

void restore(CaptureNode& node, const Snapshot& s) {
    if (node.modelPath() != s.modelPath) node.loadModel(s.modelPath);
    if (node.irPath() != s.irPath) node.loadIr(s.irPath);
    node.setModelMetadata(s.metadata);
    node.setModelDisplayName(s.displayName);
    node.setModelSourceUrl(s.sourceUrl);
    node.setModelVariants(s.variants);
    node.setIrInfo(s.irInfo);
    for (const auto& [index, value] : s.params) node.setParameter(index, value);
}

} // namespace CaptureSounds
