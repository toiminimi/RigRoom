#pragma once
#include "Tone3000Types.h"
#include <QAbstractListModel>
#include <QPointer>
#include <QStyledItemDelegate>
#include <vector>

// The TONE3000 browser's result list: tones or creators, fetched a page at a
// time as the list scrolls (canFetchMore/fetchMore), or a local list from the
// library. Pages are appended; the list is never rebuilt. While a new search
// runs, the previous results stay visible (dimmed) until the answer arrives.
class Tone3000ResultModel : public QAbstractListModel {
    Q_OBJECT
public:
    // Group rows are headers in library lists, made from items whose raw JSON
    // has "rigroom_group".
    enum class RowKind { Tone, Creator, Skeleton, Status, Group };
    enum class Status { None, Loading, Waiting, Error, Empty };
    enum Role { KindRole = Qt::UserRole + 1, StaleRole, StatusRole };

    explicit Tone3000ResultModel(QObject* parent = nullptr);

    // Remote list. `restorePages` loads that many pages straight away (they
    // usually come from the session cache), so a scroll position can be restored.
    void setQuery(const Tone3000::Query& query, int restorePages = 1);
    // A list that is already here (favorites, downloads, recent).
    void setLocal(std::vector<Tone3000::ToneItem> tones, const QString& emptyText);
    void retry();

    bool isRemote() const { return m_remote; }
    bool isCreators() const { return m_remote && m_query.source == Tone3000::Query::Source::Creators; }
    const Tone3000::Query& query() const { return m_query; }
    // The NAM architecture the capture lists are filtered to; empty for all.
    QString architectureFilter() const {
        return m_remote && !isCreators() && m_query.format == Tone3000::Format::Nam ? m_query.architecture : QString();
    }
    bool isLoading() const { return m_loading; }
    bool isStale() const { return m_stale; }
    int loadedPages() const { return m_page; }
    int total() const;
    int itemCount() const;
    Status status() const { return m_status; }
    QString statusText() const { return m_statusText; }

    const Tone3000::ToneItem* tone(int row) const;
    const Tone3000::CreatorItem* creator(int row) const;
    int rowForTone(int toneId) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    // Library rows can be dragged onto a library folder.
    static constexpr const char* kLibraryMimeType = "application/x-rigroom-library-paths";
    QStringList mimeTypes() const override { return {kLibraryMimeType}; }
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    void fetchMore(const QModelIndex& parent) override;

signals:
    void loadingChanged(bool loading);
    // A page arrived (page 1 replaces the list).
    void pageLoaded(int page);
    void statusChanged();

private:
    void requestPage(int page);
    void setLoading(bool loading);
    void setStatus(Status status, const QString& text = {});
    int extraRows() const;

    Tone3000::Query m_query;
    bool m_remote = false;
    std::vector<Tone3000::ToneItem> m_tones;
    std::vector<Tone3000::CreatorItem> m_creators;
    bool m_showCreators = false;
    int m_page = 0;
    int m_total = 0;
    bool m_hasNext = false;
    bool m_loading = false;
    bool m_stale = false;
    int m_restorePages = 1;
    Status m_status = Status::None;
    QString m_statusText;
    QString m_emptyText;
    QPointer<QObject> m_request;
    quint64 m_generation = 0;
};

// Paints result rows the way the plugin browser does: thumbnail, title,
// creator and gear, statistics, and a star to click.
class Tone3000RowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    Tone3000RowDelegate(Tone3000::Format format, QObject* parent = nullptr);

    static constexpr int kRowHeight = 72;
    static constexpr int kGroupHeight = 36;
    // Animation phase 0..1 for skeleton rows and the loading spinner.
    void setPhase(qreal phase) { m_phase = phase; }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
    static QRect starRect(const QRect& row);
    // Where the "on disk" badge of a TONE3000 row is drawn.
    static QRect diskBadgeRect(const QRect& row, const QFont& font);

signals:
    void starClicked(int row);
    void diskBadgeClicked(int row);

private:
    void paintTone(QPainter* p, const QStyleOptionViewItem& option, const Tone3000::ToneItem& tone,
                   int modelCount) const;
    void paintCreator(QPainter* p, const QStyleOptionViewItem& option, const Tone3000::CreatorItem& creator) const;
    void paintSkeleton(QPainter* p, const QRect& r, int row) const;
    void paintGroup(QPainter* p, const QStyleOptionViewItem& option, const Tone3000::ToneItem& group) const;
    void paintStatus(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    Tone3000::Format m_format;
    qreal m_phase = 0;
};

// Short counts for statistics: 950, 12.3k, 1.2M.
QString tone3000ShortCount(int value);
