#pragma once
#include "CaptureLibrary.h"
#include "Tone3000Types.h"
#include "../audio/AudioEngine.h"
#include "../audio/AudioNode.h"
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <functional>
#include <vector>

class QComboBox;
class QScrollArea;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolButton;
class QUrl;
class Tone3000ResultModel;
class Tone3000RowDelegate;

// Browser for TONE3000: NAM captures for a Neural Amp Modeler block, or
// impulse responses for the file slot of an IR/cab loader block. Everything
// that talks to the network runs in the background; the list stays usable
// while results, capture variants and files load.
class Tone3000Dialog : public QDialog {
    Q_OBJECT
public:
    // Nam: captures for a Neural Amp Modeler block. Ir: impulse responses for
    // the file slot `irPropertyUri` of an IR/cab loader block.
    enum class Mode { Nam, Ir };
    explicit Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent = nullptr,
                            Mode mode = Mode::Nam, const std::string& irPropertyUri = {});
    ~Tone3000Dialog() override;

    std::string getDownloadedModelPath() const { return m_downloadedModelPath; }
    QString getDownloadedToneName() const { return m_downloadedToneName; }
    QString getDownloadedToneUrl() const { return m_downloadedToneUrl; }
    AudioNode::ModelMetadata getDownloadedMetadata() const { return m_downloadedMetadata; }
    std::vector<AudioNode::ModelVariant> getDownloadedVariants() const { return m_downloadedVariants; }
    void setInitialSearchQuery(const QString& query);
    void setInitialTone(const QString& sourceUrl, const QString& captureName);
    // Opens straight into one creator's tones.
    void showCreator(const QString& username);
    void reject() override;
    void done(int result) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Library: what is on this computer. Online: TONE3000.
    enum class Tab { Library, Online };
    enum class View {
        LibraryAll, LibraryRecent, LibraryTopRated, LibraryMissing, LibraryFolder,
        Trending, Newest, MostDownloaded, Favorites, Creators, Saved, Catalog
    };
    struct Choice {
        QString label;
        QString value;
    };
    struct FilterChip {
        QToolButton* button = nullptr;
        QString name;
        std::vector<Choice> choices;
        QString Tone3000::Query::*field = nullptr;
        bool namOnly = true;
        bool multi = false; // several values, comma-separated
    };
    struct BackEntry {
        View view = View::Trending;
        int savedIndex = -1;
        Tone3000::Query query;
        int pages = 1;
        int scroll = 0;
        int toneId = 0;
    };
    struct PendingRestore {
        int pages = 1;
        int scroll = 0;
        int toneId = 0;
        bool active = false;
    };
    // One file transfer: a preview or the file to load.
    struct Transfer {
        QPointer<QNetworkReply> reply;
        QString path;
        QJsonObject tone;
        QJsonArray models;
        int modelIndex = -1;
        int redirects = 0;
    };

    void buildUi();
    QWidget* buildTabs();
    QWidget* buildApiKeyBanner();
    QWidget* buildSidebar();
    QWidget* buildCenter();
    QWidget* buildInfoPanel();
    void addChoiceChip(QLayout* layout, const QString& name, std::vector<Choice> choices,
                       QString Tone3000::Query::*field, bool namOnly, bool multi = false);
    void fillChipMenu(FilterChip& chip);
    void toggleChipValue(QString Tone3000::Query::*field, const QString& value);
    void addTagFilter(const QString& tag);
    void updateChips();
    void rebuildSidebar();
    void selectSidebarView();

    // Library tab (Tone3000DialogLibrary.cpp)
    void setTab(Tab tab);
    bool isLibraryView() const {
        return m_view == View::LibraryAll || m_view == View::LibraryRecent
            || m_view == View::LibraryTopRated || m_view == View::LibraryMissing || m_view == View::LibraryFolder;
    }
    QWidget* buildLibraryChips();
    QWidget* buildLibraryInfo();
    QWidget* buildImportBanner();
    void rebuildLibrarySidebar();
    void onLibrarySidebarClicked(QListWidgetItem* item);
    void runLibraryView();
    void updateLibraryChips();
    void fillLibraryFilterMenu(int category);
    void showLibraryDetails();
    void saveGearEdits();
    QString selectedKey() const;
    QStringList selectedKeys() const;
    // The file the panel is editing: the variant picked on the right, else the row.
    QString activeLibraryPath() const;
    // How many library rows are selected (a grouped tone counts as one).
    int selectedLibraryRows() const;
    void tagSelected();
    void removeFromLibrary(const QStringList& paths);
    void openInLibrary(int toneId);
    void openToneOnline();
    void addFilesToLibrary();
    // Describes the files (off the GUI thread), then asks what to add and how.
    void askToAdd(const QStringList& paths, const QString& title);
    void showAddDialog(const QList<CaptureLibrary::File>& files, const QString& title);
    QString askNewFolder();
    void fillFolderMenu(QMenu* menu, const std::function<void(const QString&)>& pick);
    void addFolderToLibrary();
    void importEarlierDownloads();
    // Adds TONE3000 variants to the library, downloading them first if needed.
    void addVariantsToLibrary(const QList<int>& indices, const CaptureLibrary::AddOptions& options);
    void addNextQueued();
    void updateAddButtons();
    // Filing library rows by dragging them onto a sidebar folder.
    void libraryDragMoved(const QPoint& global);
    void libraryDragEnded(const QPoint& global, bool drop);
    void highlightDropTarget(QListWidgetItem* item);
    // Packs and editing several files at once.
    void editPack(const QString& id);
    QString choosePack(const QString& suggestedTitle);
    QWidget* buildBatchInfo();
    void showBatchDetails();
    // Asks to save or discard unsaved edits (one file or several) before the
    // panel shows something else. Returns after the user decided.
    void resolvePendingEdits();
    void editChanged();

    // Views and searching
    void setView(View view, int savedIndex = -1);
    void runView(int restorePages = 1);
    void onQueryEdited();
    void updateCount();
    void updateActivity();
    void loadSettings();
    void saveSettings();

    // Selection and details
    void onCurrentChanged();
    void selectTone(const Tone3000::ToneItem& tone);
    void clearDetails();
    void showDetails();
    void fillVariants();
    void requestModels();
    void setModels(int toneId, const QJsonArray& models);
    int currentVariant() const;
    // The NAM architecture the variant list is filtered to; empty for all.
    QString variantArchitecture() const;
    void showAllVariants();
    QString localPathFor(const QJsonObject& model) const;
    QString modeCacheDir() const;

    // Actions
    void previewVariant(int index);
    void loadVariant(int index);
    void finishLoad(const Transfer& transfer, const QString& path);
    enum class TransferKind { Preview, Load, Add };
    Transfer& transferFor(TransferKind kind);
    void startTransfer(Transfer& transfer, const QUrl& url, TransferKind kind);
    void cancelTransfer(Transfer& transfer);
    void toggleFavorite(const Tone3000::ToneItem& tone);
    void applyFileToNode(const std::string& path);
    void saveCurrentSearch();

    // Creators
    void updateCreatorHeader();
    void fetchCreatorInfo(const QString& username);
    void goBack();

    void setStatus(const QString& text, bool error = false);

    AudioNode* m_node;
    AudioEngine* m_engine;
    Mode m_mode;
    Tone3000::Format m_format;
    std::string m_irPropertyUri;
    std::string m_originalModelPath;
    bool m_isPreviewing = false;

    // Results for the caller
    std::string m_downloadedModelPath;
    QString m_downloadedToneName;
    QString m_downloadedToneUrl;
    AudioNode::ModelMetadata m_downloadedMetadata;
    std::vector<AudioNode::ModelVariant> m_downloadedVariants;

    // State
    Tab m_tab = Tab::Library;
    View m_view = View::LibraryAll;
    QHash<int, QStringList> m_libraryFilters; // tag category -> picked tags (any of them)
    QString m_libraryGroup = "tone"; // none, tone, creator, folder
    QString m_librarySort = "name";  // name, rating, used, added
    QString m_libraryFolderName;     // the library folder shown by View::LibraryFolder
    QSet<QString> m_collapsedGroups;
    QString m_preferredVariantPath;  // the library file whose tone is shown
    View m_libraryView = View::LibraryAll; // last view of each tab
    View m_onlineView = View::Trending;
    QString m_otherTabSearch;    // each tab keeps its own search text
    bool m_tabFromSettings = false;
    int m_savedIndex = -1;
    Tone3000::Query m_query;
    Tone3000::ToneItem m_selected;
    QJsonArray m_models;
    int m_modelsToneId = 0;
    bool m_modelsLoading = false;
    bool m_allVariants = false; // the user asked to see every architecture
    bool m_otherVariants = false; // a library row also shows its tone's files not in the library
    int m_previewingIndex = -1;
    QPointer<QObject> m_modelsRequest;
    QPointer<QObject> m_creatorRequest;
    QJsonObject m_creatorInfo;
    std::vector<BackEntry> m_backStack;
    PendingRestore m_restore;
    QString m_initialToneUrl;
    Transfer m_previewTransfer;
    Transfer m_loadTransfer;
    Transfer m_addTransfer;
    QList<QPair<QJsonObject, int>> m_addQueue; // (tone, variant index) waiting to be added
    QJsonArray m_addQueueModels;
    CaptureLibrary::AddOptions m_addOptions;
    qreal m_phase = 0;
    bool m_viewportUpdateQueued = false;

    // Widgets
    QPushButton* m_libraryTab = nullptr;
    QPushButton* m_onlineTab = nullptr;
    QWidget* m_libraryChipRow = nullptr;
    QList<QToolButton*> m_libraryFilterChips; // by TagCategory
    QToolButton* m_libraryGroupChip = nullptr;
    QToolButton* m_librarySortChip = nullptr;
    QToolButton* m_selectionButton = nullptr;
    QToolButton* m_libraryAddButton = nullptr;
    QLabel* m_libraryCount = nullptr;
    QPushButton* m_importBanner = nullptr;
    QWidget* m_libraryInfo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QList<QToolButton*> m_ratingButtons;
    QLabel* m_libraryTagsLabel = nullptr;
    QLineEdit* m_addTagEdit = nullptr;
    QComboBox* m_gearType = nullptr;
    QLineEdit* m_gearMake = nullptr;
    QLineEdit* m_gearModel = nullptr;
    QComboBox* m_gearTone = nullptr;
    class QPlainTextEdit* m_notesEdit = nullptr;
    QLabel* m_factsLabel = nullptr;
    QPushButton* m_showFolderButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_addToLibraryButton = nullptr;
    QComboBox* m_folderCombo = nullptr;
    QComboBox* m_packCombo = nullptr;
    QWidget* m_batchInfo = nullptr;
    QLabel* m_batchTitle = nullptr;
    QComboBox* m_batchFolder = nullptr;
    QComboBox* m_batchPack = nullptr;
    QComboBox* m_batchRating = nullptr;
    QComboBox* m_batchType = nullptr;
    QComboBox* m_batchTone = nullptr;
    QLineEdit* m_batchMake = nullptr;
    QLineEdit* m_batchModel = nullptr;
    QLabel* m_batchTags = nullptr;
    QLineEdit* m_batchAddTag = nullptr;
    QPushButton* m_batchApply = nullptr;
    QPushButton* m_batchReset = nullptr;
    CaptureLibrary::Changes m_batchChanges;
    QStringList m_batchKeys;          // the files the batch editor shows
    CaptureLibrary::Changes m_edit;   // unsaved changes to one file
    QString m_editPath;               // the file they belong to
    QWidget* m_editBar = nullptr;     // "Unsaved changes  Cancel  Save"
    QWidget* m_librarySummary = nullptr;
    QWidget* m_libraryForm = nullptr;
    QLabel* m_summaryTags = nullptr;
    QLabel* m_summaryNotes = nullptr;
    QScrollArea* m_infoScroll = nullptr;
    bool m_editing = false;           // the library form is open
    QString m_editingPath;
    QPushButton* m_addAllButton = nullptr;
    QWidget* m_apiKeyBanner = nullptr;
    QListWidget* m_sidebar = nullptr;
    QListWidgetItem* m_dropTarget = nullptr; // folder lit up during a drag
    QLabel* m_dragBadge = nullptr;           // follows the pointer during a drag
    int m_sidebarRowBeforeDrag = -1;
    QLineEdit* m_search = nullptr;
    QPushButton* m_saveSearchButton = nullptr;
    QWidget* m_chipRow = nullptr;
    std::vector<FilterChip> m_chips;
    QToolButton* m_calibratedChip = nullptr;
    QToolButton* m_resetChip = nullptr;
    QLabel* m_count = nullptr;
    QWidget* m_creatorHeader = nullptr;
    QLabel* m_creatorAvatar = nullptr;
    QLabel* m_creatorName = nullptr;
    QLabel* m_creatorStats = nullptr;
    QPushButton* m_creatorBack = nullptr;
    QWidget* m_activityBar = nullptr;
    QListView* m_list = nullptr;
    Tone3000ResultModel* m_model = nullptr;
    Tone3000RowDelegate* m_delegate = nullptr;
    QLabel* m_status = nullptr;

    QWidget* m_infoEmpty = nullptr;
    QWidget* m_infoContent = nullptr;
    QLabel* m_image = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_byline = nullptr;
    QLabel* m_tags = nullptr;
    QLabel* m_description = nullptr;
    QToolButton* m_moreButton = nullptr;
    QLabel* m_variantsLabel = nullptr;
    QListWidget* m_variants = nullptr;
    QPushButton* m_loadButton = nullptr;
    QPushButton* m_favoriteButton = nullptr;
    QPushButton* m_webButton = nullptr;
    QProgressBar* m_progress = nullptr;

    QTimer* m_searchTimer = nullptr;
    QTimer* m_modelsTimer = nullptr;
    QTimer* m_animation = nullptr;
};
