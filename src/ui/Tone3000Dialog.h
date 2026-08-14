#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QHash>
#include "../audio/AudioNode.h"
#include "../audio/AudioEngine.h"

class QKeyEvent;
class QEvent;
class QUrl;
class QListWidget;
class QSplitter;
class Tone3000ImageLoader;

class Tone3000Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent = nullptr);
    ~Tone3000Dialog() override = default;

    std::string getDownloadedModelPath() const { return m_downloadedModelPath; }
    QString getDownloadedToneName() const { return m_downloadedToneName; }
    QString getDownloadedToneUrl() const { return m_downloadedToneUrl; }
    AudioNode::ModelMetadata getDownloadedMetadata() const { return m_downloadedMetadata; }
    std::vector<AudioNode::ModelVariant> getDownloadedVariants() const { return m_downloadedVariants; }
    void setInitialSearchQuery(const QString& query);
    void setInitialTone(const QString& sourceUrl, const QString& captureName);
    void reject() override;

private slots:
    void performSearch();
    void onSearchFinished(QNetworkReply* reply);
    void onDownloadClicked();
    void onPreviewClicked();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished(QNetworkReply* reply);
    void onFavoritesToggled(bool checked);
    void onFavoriteButtonClicked();
    void onScrollChanged(int value);

private:
    void setupUI();
    void requestSearch();
    void requestPage(int page, bool append);
    void fetchFavoriteIds(int page = 1);
    void onFavoriteIdsFinished(QNetworkReply* reply, int page);
    void fetchModelsForTone(int toneId);
    void onModelsFinished(QNetworkReply* reply, int toneId);
    void populateModels(const QJsonArray& models);
    void downloadModelFile(const QString& url, const QString& filename, bool isPreview, bool isRedirect = false);
    void rebuildCards();
    void appendToneCard(const QJsonObject& tone, int index);
    void selectTone(int index);
    void showToneInfo(const QJsonObject& tone);
    void updateActiveFilterChips();
    QString requestCacheKey(int page) const;
    void onInfoLinkClicked(const QUrl& url);
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Audio node and engine for preview
    AudioNode* m_node;
    AudioEngine* m_engine;
    std::string m_originalModelPath;
    bool m_isPreviewing = false;
    bool m_previewDownload = false;

    // UI elements
    QLineEdit* m_searchEdit;
    QPushButton* m_searchBtn;
    
    // Filters
    QComboBox* m_gearFilterCombo;
    QComboBox* m_archFilterCombo;
    QComboBox* m_sizeFilterCombo;
    QComboBox* m_characterFilterCombo;
    QComboBox* m_sortCombo;
    QCheckBox* m_calibratedCheckbox;

    QScrollArea* m_resultsArea;
    QWidget* m_resultsContent;
    QVBoxLayout* m_resultsLayout;
    QScrollArea* m_infoArea;
    QSplitter* m_contentSplitter;
    QLabel* m_infoTitleLabel;
    QLabel* m_infoCreatorLabel;
    QLabel* m_infoImageLabel;
    class QTextBrowser* m_infoText;
    QLabel* m_variantsLabel;
    QListWidget* m_variantsList;
    QPushButton* m_infoToggleBtn;
    QWidget* m_filterChipsWidget;
    QHBoxLayout* m_filterChipsLayout;
    QComboBox* m_modelsCombo;
    QPushButton* m_loadBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_favoriteBtn;
    QLabel* m_pageLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QCheckBox* m_favoritesCheckbox;

    void loadFavoritesLocal();
    void saveFavoriteLocal(const QJsonObject& toneObj, const QJsonArray& modelsArray);
    void removeFavoriteLocal(int toneId);
    bool isFavoriteLocal(int toneId) const;
    void loadFilterSettings();
    void saveFilterSettings();

    // Network manager
    QNetworkAccessManager* m_networkManager;
    Tone3000ImageLoader* m_imageLoader;
    QPointer<QNetworkReply> m_currentReply;
    QPointer<QNetworkReply> m_modelsReply;
    QPointer<QNetworkReply> m_downloadReply;
    QPointer<QNetworkReply> m_favoritesReply;

    // Data cache
    QJsonArray m_currentTones;
    QJsonArray m_currentModels;
    QHash<int, QJsonArray> m_modelsByToneId;
    QHash<QString, QJsonArray> m_pageCache;
    QHash<QString, QJsonObject> m_pageMetadata;
    int m_currentPage = 1;
    static constexpr int PAGE_SIZE = 25;
    bool m_hasNextPage = false;
    bool m_isLoadingPage = false;
    int m_totalPages = 0;
    int m_totalResults = 0;
    int m_selectedToneIndex = -1;
    int m_selectedToneId = -1;
    enum class PendingAction { None, Preview, Load };
    PendingAction m_pendingAction = PendingAction::None;
    QSet<int> m_favoriteToneIds;
    bool m_isPopulatingModels = false;
    quint64 m_searchGeneration = 0;
    QString m_initialToneUrl;
    QTimer* m_searchDebounceTimer;
    std::string m_downloadedModelPath;
    QString m_downloadedToneName;
    QString m_downloadedToneUrl;
    AudioNode::ModelMetadata m_downloadedMetadata;
    std::vector<AudioNode::ModelVariant> m_downloadedVariants;

    QWidget* m_apiKeyBanner = nullptr;
};
