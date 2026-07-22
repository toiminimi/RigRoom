#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
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

class Tone3000Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent = nullptr);
    ~Tone3000Dialog() override = default;

    std::string getDownloadedModelPath() const { return m_downloadedModelPath; }
    QString getDownloadedToneName() const { return m_downloadedToneName; }
    QString getDownloadedToneUrl() const { return m_downloadedToneUrl; }
    AudioNode::ModelMetadata getDownloadedMetadata() const { return m_downloadedMetadata; }
    void setInitialSearchQuery(const QString& query);
    void reject() override;

private slots:
    void performSearch();
    void onSearchFinished();
    void onRowSelectionChanged();
    void onDownloadClicked();
    void onPreviewClicked();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onFavoritesToggled(bool checked);
    void onFavoriteButtonClicked();
    void onPreviousPageClicked();
    void onNextPageClicked();

private:
    void setupUI();
    void requestSearch();
    void fetchModelsForTone(int toneId);
    void onModelsFinished(QNetworkReply* reply, int toneId);
    void populateModels(const QJsonArray& models);
    void downloadModelFile(const QString& url, const QString& filename, bool isPreview, bool isRedirect = false);
    void onHeaderClicked(int logicalIndex);
    void refreshTableRows();

    // Audio node and engine for preview
    AudioNode* m_node;
    AudioEngine* m_engine;
    std::string m_originalModelPath;
    bool m_isPreviewing = false;
    bool m_previewDownload = false;
    int m_lastSortedColumn = -1;
    bool m_sortAscending = true;

    // UI elements
    QLineEdit* m_searchEdit;
    QPushButton* m_searchBtn;
    
    // Filters
    QComboBox* m_gearFilterCombo;
    QComboBox* m_archFilterCombo;
    QComboBox* m_tagFilterCombo;
    QComboBox* m_sortCombo;

    QTableWidget* m_resultsTable;
    QComboBox* m_modelsCombo;
    QPushButton* m_loadBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_favoriteBtn;
    QPushButton* m_previousPageBtn;
    QPushButton* m_nextPageBtn;
    QLabel* m_pageLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    class QCheckBox* m_favoritesCheckbox;
    QPushButton* m_infoToggleBtn;
    class QScrollArea* m_detailsArea;
    QLabel* m_detailsTitleLabel;
    QLabel* m_detailsAuthorLabel;
    QLabel* m_detailsStatsLabel;
    QLabel* m_detailsDescText;
    QLabel* m_detailsGearLabel;
    QLabel* m_detailsTagsLabel;

    void loadFavoritesLocal();
    void saveFavoriteLocal(const QJsonObject& toneObj, const QJsonArray& modelsArray);
    void removeFavoriteLocal(int toneId);
    bool isFavoriteLocal(int toneId) const;
    void loadFilterSettings();
    void saveFilterSettings();

    // Network manager
    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply = nullptr;
    QNetworkReply* m_modelsReply = nullptr;

    // Data cache
    QJsonArray m_currentTones;
    QJsonArray m_currentModels;
    QHash<int, QJsonArray> m_modelsByToneId;
    int m_currentPage = 1;
    static constexpr int PAGE_SIZE = 25;
    bool m_hasNextPage = false;
    std::string m_downloadedModelPath;
    QString m_downloadedToneName;
    QString m_downloadedToneUrl;
    AudioNode::ModelMetadata m_downloadedMetadata;

    // Supabase config
    QString m_supabaseUrl = "https://api.tone3000.com";
    QString m_supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Imd6eWJpdW9weGtkeGJ5dG5vamRzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzgwODIxNjUsImV4cCI6MjA1MzY1ODE2NX0.Gq66BJXjtLsqP2nAGXm9Xb9PAjoeZalWUj66K4nmVSU";
    
    QWidget* m_apiKeyBanner = nullptr;
    QString getEffectiveApiKey() const;
};
