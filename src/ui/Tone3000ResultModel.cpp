#include "Tone3000ResultModel.h"
#include "PluginBrowser.h"
#include "Tone3000Api.h"
#include "Tone3000ImageLoader.h"
#include "Tone3000Library.h"
#include "CaptureLibrary.h"
#include "NamMetadata.h"

#include <QFontMetrics>
#include <QJsonArray>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>
#include <cmath>

using Tone3000::CreatorItem;
using Tone3000::ToneItem;

namespace {
constexpr int kSkeletonRows = 6;
}

QString tone3000ShortCount(int value) {
    if (value >= 1000000) return QString::number(value / 1000000.0, 'f', 1) + "M";
    if (value >= 10000) return QString::number(value / 1000) + "k";
    if (value >= 1000) return QString::number(value / 1000.0, 'f', 1) + "k";
    return QString::number(value);
}

// ─── Model ───────────────────────────────────────────────────────────────────

Tone3000ResultModel::Tone3000ResultModel(QObject* parent) : QAbstractListModel(parent) {}

void Tone3000ResultModel::setQuery(const Tone3000::Query& query, int restorePages) {
    ++m_generation;
    delete m_request;
    m_remote = true;
    m_query = query;
    m_restorePages = std::max(1, restorePages);
    m_page = 0;
    m_hasNext = false;
    m_emptyText = query.source == Tone3000::Query::Source::Creators
        ? QStringLiteral("No creators match your search.")
        : QStringLiteral("Nothing matches. Try other words or fewer filters.");
    // Keep showing the old rows only if they are the same kind as the new ones.
    const bool hadItems = itemCount() > 0 && m_showCreators == isCreators();
    if (hadItems) {
        // Keep the old rows, dimmed, until the new ones arrive.
        m_stale = true;
        emit dataChanged(index(0), index(rowCount() - 1));
    } else {
        beginResetModel();
        m_tones.clear();
        m_creators.clear();
        m_showCreators = isCreators();
        m_status = Status::None;
        endResetModel();
    }
    requestPage(1);
}

void Tone3000ResultModel::setLocal(std::vector<ToneItem> tones, const QString& emptyText) {
    ++m_generation;
    delete m_request;
    beginResetModel();
    m_remote = false;
    m_tones = std::move(tones);
    m_creators.clear();
    m_showCreators = false;
    m_page = 0;
    m_hasNext = false;
    m_stale = false;
    m_total = static_cast<int>(m_tones.size());
    m_emptyText = emptyText;
    m_status = m_tones.empty() ? Status::Empty : Status::None;
    m_statusText = m_tones.empty() ? emptyText : QString();
    endResetModel();
    setLoading(false);
    emit statusChanged();
    emit pageLoaded(1);
}

void Tone3000ResultModel::retry() {
    if (!m_remote) return;
    if (m_page == 0) setQuery(m_query, m_restorePages);
    else requestPage(m_page + 1);
}

int Tone3000ResultModel::total() const {
    return m_total > 0 ? m_total : itemCount();
}

int Tone3000ResultModel::itemCount() const {
    return m_showCreators ? static_cast<int>(m_creators.size()) : static_cast<int>(m_tones.size());
}

int Tone3000ResultModel::extraRows() const {
    if (itemCount() == 0 && m_loading && m_status != Status::Waiting) return kSkeletonRows;
    return m_status == Status::None ? 0 : 1;
}

const ToneItem* Tone3000ResultModel::tone(int row) const {
    if (m_showCreators || row < 0 || row >= static_cast<int>(m_tones.size())) return nullptr;
    return &m_tones[row];
}

const CreatorItem* Tone3000ResultModel::creator(int row) const {
    if (!m_showCreators || row < 0 || row >= static_cast<int>(m_creators.size())) return nullptr;
    return &m_creators[row];
}

int Tone3000ResultModel::rowForTone(int toneId) const {
    if (m_showCreators) return -1;
    for (int row = 0; row < static_cast<int>(m_tones.size()); ++row) {
        if (m_tones[row].id == toneId) return row;
    }
    return -1;
}

int Tone3000ResultModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : itemCount() + extraRows();
}

QVariant Tone3000ResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    const bool item = row < itemCount();
    switch (role) {
    case KindRole:
        if (item && !m_showCreators && m_tones[row].raw.contains("rigroom_group")) return static_cast<int>(RowKind::Group);
        if (item) return static_cast<int>(m_showCreators ? RowKind::Creator : RowKind::Tone);
        return static_cast<int>(itemCount() == 0 && m_loading && m_status != Status::Waiting ? RowKind::Skeleton
                                                                                             : RowKind::Status);
    case StaleRole:
        return m_stale;
    case StatusRole:
        return static_cast<int>(m_status);
    case Qt::DisplayRole:
        if (!item) return m_statusText;
        return m_showCreators ? m_creators[row].displayName : m_tones[row].title;
    case Qt::ToolTipRole:
        if (!item || m_showCreators) return {};
        return m_tones[row].title;
    default:
        return {};
    }
}

Qt::ItemFlags Tone3000ResultModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.row() < itemCount()) {
        // Group headers can be clicked (to fold them) but not selected.
        if (!m_showCreators && m_tones[index.row()].raw.contains("rigroom_group")) return Qt::ItemIsEnabled;
        if (!m_showCreators && m_tones[index.row()].raw.contains("rigroom_key")) {
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
        }
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    // The error row is clickable (retry); the others are decoration.
    return m_status == Status::Error ? Qt::ItemIsEnabled : Qt::NoItemFlags;
}

QMimeData* Tone3000ResultModel::mimeData(const QModelIndexList& indexes) const {
    QStringList paths;
    for (const QModelIndex& index : indexes) {
        if (const ToneItem* item = tone(index.row())) {
            const QJsonArray grouped = item->raw.value("rigroom_keys").toArray();
            if (!grouped.isEmpty()) {
                for (const QJsonValue& value : grouped) {
                    const QString path = value.toString();
                    if (!path.isEmpty() && !paths.contains(path)) paths << path;
                }
                continue;
            }
            const QString path = item->raw.value("rigroom_key").toString();
            if (!path.isEmpty()) paths << path;
        }
    }
    auto* mime = new QMimeData();
    mime->setData(kLibraryMimeType, paths.join('\n').toUtf8());
    return mime;
}

bool Tone3000ResultModel::canFetchMore(const QModelIndex& parent) const {
    return !parent.isValid() && m_remote && m_hasNext && !m_loading && !m_stale && m_status != Status::Error;
}

void Tone3000ResultModel::fetchMore(const QModelIndex& parent) {
    if (canFetchMore(parent)) requestPage(m_page + 1);
}

void Tone3000ResultModel::setLoading(bool loading) {
    if (m_loading == loading) return;
    const int before = extraRows();
    m_loading = loading;
    const int after = extraRows();
    // Skeleton rows appear and disappear with the first page.
    if (after > before) {
        beginInsertRows({}, itemCount() + before, itemCount() + after - 1);
        endInsertRows();
    } else if (after < before) {
        beginRemoveRows({}, itemCount() + after, itemCount() + before - 1);
        endRemoveRows();
    }
    emit loadingChanged(loading);
}

void Tone3000ResultModel::setStatus(Status status, const QString& text) {
    const int before = extraRows();
    m_status = status;
    m_statusText = text;
    const int after = extraRows();
    const int first = itemCount();
    if (after > before) {
        if (before > 0) emit dataChanged(index(first), index(first + before - 1));
        beginInsertRows({}, first + before, first + after - 1);
        endInsertRows();
    } else if (after < before) {
        beginRemoveRows({}, first + after, first + before - 1);
        endRemoveRows();
        if (after > 0) emit dataChanged(index(first), index(first + after - 1));
    } else if (after > 0) {
        emit dataChanged(index(first), index(first + after - 1));
    }
    emit statusChanged();
}

void Tone3000ResultModel::requestPage(int page) {
    delete m_request;
    const quint64 generation = ++m_generation;
    const Tone3000::Query query = m_query;
    if (itemCount() > 0 && !m_stale) setStatus(Status::Loading, QStringLiteral("Loading more…"));
    else if (m_status != Status::None) setStatus(Status::None);
    setLoading(true);

    QUrl url(query.endpoint());
    url.setQuery(query.toUrlQuery(page));
    auto waiting = [this, generation](int seconds) {
        if (generation != m_generation) return;
        setStatus(Status::Waiting, QString("TONE3000 is busy. Trying again in %1 s…").arg(seconds));
    };
    auto done = [this, generation, page, query](const Tone3000Api::Response& response) {
        if (generation != m_generation) return;
        m_request = nullptr;
        if (!response.ok) {
            if (response.error == QNetworkReply::OperationCanceledError) return;
            if (m_stale) {
                beginResetModel();
                m_tones.clear();
                m_creators.clear();
                m_stale = false;
                m_showCreators = isCreators();
                endResetModel();
            }
            setLoading(false);
            const QString reason = response.httpStatus == 401 && !Tone3000Api::hasKey()
                ? QStringLiteral("Enter your TONE3000 secret key above to browse.")
                : QString("Couldn't reach TONE3000 (%1). Click to try again.").arg(response.errorString);
            setStatus(Status::Error, reason);
            return;
        }
        const QJsonObject& object = response.object;
        const QJsonArray data = object.value("data").toArray();
        const bool creators = query.source == Tone3000::Query::Source::Creators;
        const int totalPages = object.value("total_pages").toInt();
        m_total = object.value("total").toInt(object.value("total_count").toInt(0));
        m_hasNext = totalPages > 0 ? page < totalPages : data.size() == query.pageSize();

        if (page == 1) {
            beginResetModel();
            m_tones.clear();
            m_creators.clear();
            m_showCreators = creators;
            m_stale = false;
            m_loading = false;
            m_status = Status::None;
            m_statusText.clear();
            for (const QJsonValue& value : data) {
                if (creators) m_creators.push_back(CreatorItem::fromJson(value.toObject()));
                else m_tones.push_back(ToneItem::fromJson(value.toObject()));
            }
            endResetModel();
            emit loadingChanged(false);
        } else {
            setLoading(false);
            setStatus(Status::None);
            const int first = itemCount();
            if (!data.isEmpty()) {
                beginInsertRows({}, first, first + static_cast<int>(data.size()) - 1);
                for (const QJsonValue& value : data) {
                    if (creators) m_creators.push_back(CreatorItem::fromJson(value.toObject()));
                    else m_tones.push_back(ToneItem::fromJson(value.toObject()));
                }
                endInsertRows();
            }
        }
        m_page = page;
        if (itemCount() == 0) setStatus(Status::Empty, m_emptyText);
        else emit statusChanged();
        emit pageLoaded(page);
        if (m_hasNext && page < m_restorePages) requestPage(page + 1);
    };
    m_request = Tone3000Api::instance()->getJson(url, this, done, waiting, query.cacheKey(page));
}

// ─── Delegate ────────────────────────────────────────────────────────────────

namespace {
QColor gearColor(const QString& gear, Tone3000::Format format) {
    if (format == Tone3000::Format::Ir) return QColor("#4DB6AC");
    const QString g = gear.toLower();
    if (g.contains("pedal")) return PluginArt::categoryColor("Distortions");
    if (g.contains("outboard")) return PluginArt::categoryColor("Dynamics");
    if (g.contains("amp")) return PluginArt::categoryColor("Amplifiers");
    return QColor("#4DB6AC");
}

QColor skeletonColor(qreal phase, int row) {
    const qreal wave = 0.5 + 0.5 * std::sin((phase + row * 0.08) * 2 * M_PI);
    return QColor::fromRgbF(0.15 + 0.03 * wave, 0.15 + 0.03 * wave, 0.17 + 0.03 * wave);
}
} // namespace

Tone3000RowDelegate::Tone3000RowDelegate(Tone3000::Format format, QObject* parent)
    : QStyledItemDelegate(parent), m_format(format) {}

QSize Tone3000RowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const {
    const auto kind = static_cast<Tone3000ResultModel::RowKind>(index.data(Tone3000ResultModel::KindRole).toInt());
    return {300, kind == Tone3000ResultModel::RowKind::Group ? kGroupHeight : kRowHeight};
}

QRect Tone3000RowDelegate::starRect(const QRect& row) {
    return QRect(row.right() - 34, row.top() + 10, 24, 24);
}

namespace {
const QString kDiskBadgeText = QStringLiteral("IN LIBRARY");

QFont badgeFont(const QFont& base) {
    QFont font = base;
    font.setPixelSize(9);
    font.setBold(true);
    return font;
}
} // namespace

QRect Tone3000RowDelegate::diskBadgeRect(const QRect& row, const QFont& font) {
    const QRect star = starRect(row);
    const int width = QFontMetrics(badgeFont(font)).horizontalAdvance(kDiskBadgeText) + 10;
    return QRect(star.left() - 6 - width, star.center().y() - 8, width, 16);
}

void Tone3000RowDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const auto* model = qobject_cast<const Tone3000ResultModel*>(index.model());
    if (!model) return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const auto kind = static_cast<Tone3000ResultModel::RowKind>(index.data(Tone3000ResultModel::KindRole).toInt());
    if (index.data(Tone3000ResultModel::StaleRole).toBool()) p->setOpacity(0.4);
    switch (kind) {
    case Tone3000ResultModel::RowKind::Tone:
        if (const ToneItem* tone = model->tone(index.row())) {
            paintTone(p, option, *tone, tone->modelCountFor(model->architectureFilter()));
        }
        break;
    case Tone3000ResultModel::RowKind::Creator:
        if (const CreatorItem* creator = model->creator(index.row())) paintCreator(p, option, *creator);
        break;
    case Tone3000ResultModel::RowKind::Skeleton:
        paintSkeleton(p, option.rect, index.row());
        break;
    case Tone3000ResultModel::RowKind::Group:
        if (const ToneItem* group = model->tone(index.row())) paintGroup(p, option, *group);
        break;
    case Tone3000ResultModel::RowKind::Status:
        paintStatus(p, option, index);
        break;
    }
    p->restore();
}

void Tone3000RowDelegate::paintTone(QPainter* p, const QStyleOptionViewItem& option, const ToneItem& tone,
                                    int modelCount) const {
    const QRect r = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    if (selected) p->fillRect(r, QColor("#0B4F6C"));
    else if (hovered) p->fillRect(r, QColor("#202026"));

    // Thumbnail, or generated art until (or unless) the image arrives.
    const int groupedCount = tone.raw.value("rigroom_keys").toArray().size();
    QRect thumb(r.left() + 8, r.top() + 8, 84, r.height() - 16);
    if (groupedCount > 1) {
        // A small stack behind the picture, so a tone with several captures
        // reads as one thing with more behind it.
        p->setPen(QPen(QColor("#2A2A32"), 1));
        p->setBrush(QColor("#1A1A20"));
        p->drawRoundedRect(thumb.adjusted(6, 5, 6, 5), 6, 6);
        p->drawRoundedRect(thumb.adjusted(3, 2, 3, 2), 6, 6);
        thumb.adjust(0, 0, -4, -4);
    }
    QPainterPath clip;
    clip.addRoundedRect(thumb, 6, 6);
    const qreal dpr = p->device()->devicePixelRatioF();
    const QPixmap image = tone.imageUrl.isEmpty()
        ? QPixmap() : Tone3000ImageLoader::instance()->thumbnail(tone.imageUrl, thumb.size() * dpr);
    if (!image.isNull()) {
        p->save();
        p->setClipPath(clip);
        p->drawPixmap(thumb, image);
        p->restore();
    } else {
        const QColor accent = gearColor(tone.gear, m_format);
        QLinearGradient bg(thumb.topLeft(), thumb.bottomRight());
        bg.setColorAt(0.0, accent.darker(260));
        bg.setColorAt(1.0, QColor("#141418"));
        p->fillPath(clip, bg);
        QFont glyph = option.font;
        glyph.setPixelSize(13);
        glyph.setBold(true);
        p->setFont(glyph);
        p->setPen(accent.lighter(115));
        p->drawText(thumb, Qt::AlignCenter, m_format == Tone3000::Format::Ir ? "IR" : "NAM");
    }

    // Star and "on disk" badge on the right. Library rows carry their key.
    const QString key = tone.raw.value("rigroom_key").toString();
    // The star saves a tone on the TONE3000 account; library rows have ratings instead.
    const CaptureLibrary& captures = CaptureLibrary::instance();
    const QRect star = starRect(r);
    if (key.isEmpty()) {
        const bool favorite = Tone3000Library::instance().isFavorite(tone.id);
        QFont starFont = option.font;
        starFont.setPixelSize(16);
        p->setFont(starFont);
        p->setPen(favorite ? QColor("#FFD54F") : QColor(hovered || selected ? "#6E6E7A" : "#3A3A42"));
        p->drawText(star, Qt::AlignCenter, favorite ? QString::fromUtf8("★") : QString::fromUtf8("☆"));
    }
    int rightEdge = key.isEmpty() ? star.left() - 6 : r.right() - 12;
    if (tone.raw.value("rigroom_missing").toBool()) {
        QFont font = badgeFont(option.font);
        p->setFont(font);
        const QString text = QStringLiteral("MISSING");
        const int w = QFontMetrics(font).horizontalAdvance(text) + 10;
        const QRect chip(rightEdge - w, star.center().y() - 8, w, 16);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#3A1E20"));
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(QColor("#F08080"));
        p->drawText(chip, Qt::AlignCenter, text);
        rightEdge = chip.left() - 6;
    } else if (key.isEmpty() && captures.hasTone(tone.id, m_format)) {
        p->setFont(badgeFont(option.font));
        const QRect chip = diskBadgeRect(r, option.font);
        p->setPen(hovered ? QPen(QColor("#2A6B60")) : Qt::NoPen);
        p->setBrush(QColor("#123F38"));
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(QColor("#4DD0B8"));
        p->drawText(chip, Qt::AlignCenter, kDiskBadgeText);
        rightEdge = chip.left() - 6;
    }

    // Text: title, creator · gear, statistics.
    const int textLeft = thumb.right() + 12;
    QFont nameFont = option.font;
    nameFont.setPixelSize(13);
    nameFont.setBold(true);
    p->setFont(nameFont);
    p->setPen(QColor("#EDEDF2"));
    const int titleWidth = rightEdge - textLeft;
    p->drawText(QRect(textLeft, r.top() + 8, titleWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(nameFont).elidedText(tone.title, Qt::ElideRight, titleWidth));

    const int textWidth = r.right() - 12 - textLeft;
    QFont subFont = option.font;
    subFont.setPixelSize(11);
    p->setFont(subFont);
    p->setPen(QColor("#9FA8B8"));
    QString subText = tone.raw.value("rigroom_sub").toString();
    if (subText.isEmpty()) {
        QStringList sub;
        if (!tone.creator.isEmpty()) sub << tone.creator;
        if (!tone.gear.isEmpty()) sub << tone.gear;
        if (!tone.makes.isEmpty()) sub << tone.makes.join(", ");
        subText = sub.join(QString::fromUtf8("  ·  "));
    }
    p->drawText(QRect(textLeft, r.top() + 28, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(subFont).elidedText(subText, Qt::ElideRight, textWidth));

    QStringList stats;
    if (tone.downloads > 0 || tone.favorites > 0) {
        stats << QString::fromUtf8("↓ %1").arg(tone3000ShortCount(tone.downloads));
        stats << QString::fromUtf8("★ %1").arg(tone3000ShortCount(tone.favorites));
    }
    if (modelCount > 0) {
        const QString noun = m_format == Tone3000::Format::Ir ? "file" : "capture";
        stats << QString("%1 %2%3").arg(modelCount).arg(noun, modelCount == 1 ? "" : "s");
    }
    const QJsonArray tags = tone.raw.value("rigroom_tags").toArray();
    if (!tone.raw.contains("rigroom_key")) {
        p->setPen(QColor("#6E6E7A"));
        p->drawText(QRect(textLeft, r.top() + 46, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter,
                    stats.join(QString::fromUtf8("   ")));
        return;
    }
    // Library rows show their tags as small chips, and a capture count when
    // several files of one tone or pack share the row.
    QFont tagFont = option.font;
    tagFont.setPixelSize(10);
    p->setFont(tagFont);
    const QFontMetrics metrics(tagFont);
    int x = textLeft;
    const int limit = textLeft + textWidth;
    Q_UNUSED(modelCount);
    if (groupedCount > 1) {
        const QString noun = m_format == Tone3000::Format::Ir ? "file" : "capture";
        const QString text = QString("%1 %2%3").arg(groupedCount).arg(noun, groupedCount == 1 ? "" : "s");
        const int w = metrics.horizontalAdvance(text) + 10;
        const QRect chip(x, r.top() + 47, w, 15);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(selected ? "#145A78" : "#1A3038"));
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(QColor(selected ? "#E0F4FF" : "#80D8FF"));
        p->drawText(chip, Qt::AlignCenter, text);
        x += w + 6;
    }
    if (const int rating = tone.raw.value("rigroom_rating").toInt(); rating > 0) {
        const QString stars = QString(rating, QChar(0x2605));
        p->setPen(QColor("#FFD54F"));
        p->drawText(QRect(x, r.top() + 46, metrics.horizontalAdvance(stars) + 2, 16), Qt::AlignLeft | Qt::AlignVCenter, stars);
        x += metrics.horizontalAdvance(stars) + 10;
    }
    for (const QJsonValue& value : tags) {
        const QString tag = NamMetadata::tagCategory(value.toString()) == NamMetadata::TagCategory::Other
            ? value.toString() : NamMetadata::tagLabel(value.toString());
        const int w = metrics.horizontalAdvance(tag) + 10;
        if (x + w > limit) break;
        const QRect chip(x, r.top() + 47, w, 15);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(selected ? "#1B6A8C" : "#24242A"));
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(QColor(selected ? "#E0F4FF" : "#9FA8B8"));
        p->drawText(chip, Qt::AlignCenter, tag);
        x += w + 4;
    }
}

void Tone3000RowDelegate::paintCreator(QPainter* p, const QStyleOptionViewItem& option,
                                       const CreatorItem& creator) const {
    const QRect r = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    if (selected) p->fillRect(r, QColor("#0B4F6C"));
    else if (hovered) p->fillRect(r, QColor("#202026"));

    const QRect avatar(r.left() + 16, r.top() + (r.height() - 44) / 2, 44, 44);
    QPainterPath circle;
    circle.addEllipse(avatar);
    const qreal dpr = p->device()->devicePixelRatioF();
    const QPixmap image = creator.avatarUrl.isEmpty()
        ? QPixmap() : Tone3000ImageLoader::instance()->thumbnail(creator.avatarUrl, avatar.size() * dpr);
    if (!image.isNull()) {
        p->save();
        p->setClipPath(circle);
        p->drawPixmap(avatar, image);
        p->restore();
    } else {
        p->fillPath(circle, QColor("#22303A"));
        QFont initial = option.font;
        initial.setPixelSize(17);
        initial.setBold(true);
        p->setFont(initial);
        p->setPen(QColor("#80D8FF"));
        p->drawText(avatar, Qt::AlignCenter, creator.username.left(1).toUpper());
    }

    const int textLeft = avatar.right() + 14;
    const int textWidth = r.right() - 12 - textLeft;
    QFont nameFont = option.font;
    nameFont.setPixelSize(13);
    nameFont.setBold(true);
    p->setFont(nameFont);
    p->setPen(QColor("#EDEDF2"));
    QString name = creator.displayName;
    if (creator.verified) name += QString::fromUtf8("  ✓");
    p->drawText(QRect(textLeft, r.top() + 14, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, textWidth));
    QFont subFont = option.font;
    subFont.setPixelSize(11);
    p->setFont(subFont);
    p->setPen(QColor("#9FA8B8"));
    const QString stats = QString::fromUtf8("@%1  ·  %2 uploads  ·  ↓ %3  ·  ★ %4")
                              .arg(creator.username)
                              .arg(creator.tones)
                              .arg(tone3000ShortCount(creator.downloads), tone3000ShortCount(creator.favorites));
    p->drawText(QRect(textLeft, r.top() + 36, textWidth, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(subFont).elidedText(stats, Qt::ElideRight, textWidth));
}

void Tone3000RowDelegate::paintGroup(QPainter* p, const QStyleOptionViewItem& option, const ToneItem& group) const {
    const QRect r = option.rect;
    const bool hovered = option.state & QStyle::State_MouseOver;
    p->fillRect(r, QColor(hovered ? "#1D1D22" : "#141417"));
    p->setPen(QColor("#26262C"));
    p->drawLine(r.bottomLeft(), r.bottomRight());
    const bool collapsed = group.raw.value("rigroom_collapsed").toBool();
    QFont font = option.font;
    font.setPixelSize(12);
    font.setBold(true);
    p->setFont(font);
    p->setPen(QColor("#8A8A96"));
    const QRect chevron(r.left() + 10, r.top(), 14, r.height());
    p->drawText(chevron, Qt::AlignCenter, collapsed ? QString::fromUtf8("▸") : QString::fromUtf8("▾"));
    int x = chevron.right() + 6;
    // A small picture of the tone, when there is one.
    if (!group.imageUrl.isEmpty()) {
        const QRect thumb(x, r.top() + 6, 36, r.height() - 12);
        const QPixmap image = Tone3000ImageLoader::instance()->thumbnail(group.imageUrl,
                                                                           thumb.size() * p->device()->devicePixelRatioF());
        if (!image.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(thumb, 4, 4);
            p->save();
            p->setClipPath(clip);
            p->drawPixmap(thumb, image);
            p->restore();
        }
        x = thumb.right() + 8;
    }
    const QString count = QString("  (%1)").arg(group.modelCount);
    const QString subtitle = group.creator.isEmpty() ? QString() : QString::fromUtf8("  ·  ") + group.creator;
    const QFontMetrics titleMetrics(font);
    const int available = r.right() - 12 - x - titleMetrics.horizontalAdvance(count) - 120;
    const QString title = titleMetrics.elidedText(group.title, Qt::ElideRight, std::max(80, available));
    p->setPen(QColor("#D8D8DE"));
    p->drawText(QRect(x, r.top(), r.right() - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    x += titleMetrics.horizontalAdvance(title);
    QFont small = option.font;
    small.setPixelSize(11);
    p->setFont(small);
    p->setPen(QColor("#6E6E7A"));
    p->drawText(QRect(x, r.top(), r.right() - 12 - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(small).elidedText(subtitle + count, Qt::ElideRight, r.right() - 12 - x));
}

void Tone3000RowDelegate::paintSkeleton(QPainter* p, const QRect& r, int row) const {
    p->setPen(Qt::NoPen);
    p->setBrush(skeletonColor(m_phase, row));
    p->drawRoundedRect(QRect(r.left() + 8, r.top() + 8, 84, r.height() - 16), 6, 6);
    const int left = r.left() + 104;
    const int width = r.width() - 140;
    p->drawRoundedRect(QRect(left, r.top() + 12, width * (55 + (row * 17) % 30) / 100, 12), 4, 4);
    p->drawRoundedRect(QRect(left, r.top() + 32, width * (30 + (row * 13) % 25) / 100, 9), 4, 4);
    p->drawRoundedRect(QRect(left, r.top() + 50, width / 4, 9), 4, 4);
}

void Tone3000RowDelegate::paintStatus(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const QRect r = option.rect.adjusted(12, 6, -12, -6);
    const auto status = static_cast<Tone3000ResultModel::Status>(index.data(Tone3000ResultModel::StatusRole).toInt());
    const QString text = index.data(Qt::DisplayRole).toString();
    QFont font = option.font;
    font.setPixelSize(12);
    p->setFont(font);

    if (status == Tone3000ResultModel::Status::Error) {
        const bool hovered = option.state & QStyle::State_MouseOver;
        p->setPen(QColor(hovered ? "#E05252" : "#5A2A2A"));
        p->setBrush(QColor("#1E1416"));
        p->drawRoundedRect(r, 8, 8);
        p->setPen(QColor("#F0B4B4"));
        p->drawText(r.adjusted(14, 0, -14, 0), Qt::AlignCenter | Qt::TextWordWrap, text);
        return;
    }
    if (status == Tone3000ResultModel::Status::Loading || status == Tone3000ResultModel::Status::Waiting) {
        // A small spinning arc next to the text.
        const QFontMetrics metrics(font);
        const int textWidth = metrics.horizontalAdvance(text);
        const int total = textWidth + 26;
        const QRect arc(r.center().x() - total / 2, r.center().y() - 8, 16, 16);
        p->setPen(QPen(QColor("#00B0FF"), 2, Qt::SolidLine, Qt::RoundCap));
        p->setBrush(Qt::NoBrush);
        p->drawArc(arc, static_cast<int>(-m_phase * 360 * 16), 270 * 16);
        p->setPen(QColor("#8A8A96"));
        p->drawText(QRect(arc.right() + 10, r.top(), textWidth + 4, r.height()), Qt::AlignLeft | Qt::AlignVCenter, text);
        return;
    }
    p->setPen(QColor("#6E6E7A"));
    p->drawText(r, Qt::AlignCenter | Qt::TextWordWrap, text);
}

bool Tone3000RowDelegate::editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) {
    if (event->type() != QEvent::MouseButtonRelease) return false;
    if (static_cast<Tone3000ResultModel::RowKind>(index.data(Tone3000ResultModel::KindRole).toInt())
        != Tone3000ResultModel::RowKind::Tone) {
        return false;
    }
    auto* mouse = static_cast<QMouseEvent*>(event);
    const QPoint pos = mouse->position().toPoint();
    if (starRect(option.rect).contains(pos) && !index.data(Qt::DisplayRole).isNull()
        && !qobject_cast<const Tone3000ResultModel*>(index.model())->tone(index.row())->raw.contains("rigroom_key")) {
        emit starClicked(index.row());
        return true;
    }
    const auto* model = qobject_cast<const Tone3000ResultModel*>(index.model());
    const ToneItem* tone = model ? model->tone(index.row()) : nullptr;
    if (tone && !tone->raw.contains("rigroom_key") && CaptureLibrary::instance().hasTone(tone->id, m_format)
        && diskBadgeRect(option.rect, option.font).contains(pos)) {
        emit diskBadgeClicked(index.row());
        return true;
    }
    return false;
}
