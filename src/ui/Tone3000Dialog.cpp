#include "Tone3000Dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QTextBrowser>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QRegularExpression>
#include <QCheckBox>
#include <iostream>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>

static QString obfuscateKey(const QString& input) {
    QByteArray data = input.toUtf8();
    const char key[] = "PedalBoardSecureKey123";
    int keyLen = sizeof(key) - 1;
    for (int i = 0; i < data.size(); ++i) {
        data[i] = data[i] ^ key[i % keyLen];
    }
    return QString::fromLatin1(data.toBase64());
}

static QString deobfuscateKey(const QString& input) {
    QByteArray data = QByteArray::fromBase64(input.toLatin1());
    const char key[] = "PedalBoardSecureKey123";
    int keyLen = sizeof(key) - 1;
    for (int i = 0; i < data.size(); ++i) {
        data[i] = data[i] ^ key[i % keyLen];
    }
    return QString::fromUtf8(data);
}

Tone3000Dialog::Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent) 
    : QDialog(parent), m_node(node), m_engine(engine) {
    
    m_originalModelPath = node ? node->getModelFilePath() : "";
    
    setupUI();
    m_networkManager = new QNetworkAccessManager(this);
    
    // Load filter settings
    loadFilterSettings();
    
    // Connect search and sorting
    connect(m_searchBtn, &QPushButton::clicked, this, &Tone3000Dialog::performSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &Tone3000Dialog::performSearch);
    
    // Connect filters
    connect(m_gearFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_archFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_tagFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_favoritesCheckbox, &QCheckBox::toggled, this, &Tone3000Dialog::onFavoritesToggled);
    
    // Table selection change
    connect(m_resultsTable, &QTableWidget::itemSelectionChanged, this, &Tone3000Dialog::onRowSelectionChanged);
    
    // Download load and preview buttons
    connect(m_loadBtn, &QPushButton::clicked, this, &Tone3000Dialog::onDownloadClicked);
    connect(m_previewBtn, &QPushButton::clicked, this, &Tone3000Dialog::onPreviewClicked);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &Tone3000Dialog::onFavoriteButtonClicked);
    connect(m_previousPageBtn, &QPushButton::clicked, this, &Tone3000Dialog::onPreviousPageClicked);
    connect(m_nextPageBtn, &QPushButton::clicked, this, &Tone3000Dialog::onNextPageClicked);
    
    // Load trending tones on startup
    QMetaObject::invokeMethod(this, "performSearch", Qt::QueuedConnection);
}

void Tone3000Dialog::setupUI() {
    setWindowTitle("TONE3000 Profiles Browser");
    resize(800, 550);
    
    // Styling the dialog
    setStyleSheet(
        "QDialog {"
        "  background-color: #1a1c1e;"
        "  color: #e2e2e6;"
        "  font-family: 'Inter', sans-serif;"
        "}"
        "QLineEdit {"
        "  background-color: #2d3135;"
        "  color: #e2e2e6;"
        "  border: 1px solid #43474a;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #00a3e0;"
        "}"
        "QPushButton {"
        "  background-color: #00a3e0;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-weight: bold;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #0082b3;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #2d3135;"
        "  color: #8a8d90;"
        "}"
        "QTableWidget {"
        "  background-color: #1a1c1e;"
        "  color: #e2e2e6;"
        "  gridline-color: #2d3135;"
        "  border: 1px solid #2d3135;"
        "  border-radius: 4px;"
        "  selection-background-color: #2d3135;"
        "  selection-color: #00a3e0;"
        "}"
        "QHeaderView::section {"
        "  background-color: #2d3135;"
        "  color: #a8aab0;"
        "  padding: 6px;"
        "  border: none;"
        "  font-weight: bold;"
        "}"
        "QComboBox {"
        "  background-color: #2d3135;"
        "  color: #e2e2e6;"
        "  border: 1px solid #43474a;"
        "  border-radius: 4px;"
        "  padding: 6px;"
        "  min-width: 150px;"
        "}"
        "QProgressBar {"
        "  border: 1px solid #2d3135;"
        "  border-radius: 4px;"
        "  text-align: center;"
        "  background-color: #1a1c1e;"
        "  color: #e2e2e6;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #00a3e0;"
        "}"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // API Key Banner
    m_apiKeyBanner = new QWidget(this);
    m_apiKeyBanner->setStyleSheet(
        "QWidget { background-color: #2b2111; border: 1px solid #d4a373; border-radius: 4px; }"
        "QLabel { color: #fefae0; font-size: 12px; border: none; background: transparent; }"
        "QLineEdit { background-color: #1a1c1e; color: #e2e2e6; border: 1px solid #43474a; padding: 4px; font-size: 11px; }"
        "QPushButton { background-color: #d4a373; color: #1a1c1e; font-weight: bold; border-radius: 3px; padding: 4px 10px; font-size: 11px; }"
        "QPushButton:hover { background-color: #e9c46a; }"
    );
    QHBoxLayout* bannerLayout = new QHBoxLayout(m_apiKeyBanner);
    bannerLayout->setContentsMargins(10, 6, 10, 6);
    bannerLayout->setSpacing(10);
    
    QLabel* warningIcon = new QLabel("⚠️", m_apiKeyBanner);
    QLabel* bannerText = new QLabel("Using anonymous guest key. Enter your TONE3000 Secret Key (t3k_cs_...) or Legacy API Key for full access:", m_apiKeyBanner);
    
    QLineEdit* keyInput = new QLineEdit(m_apiKeyBanner);
    keyInput->setPlaceholderText("Paste API Key here...");
    keyInput->setEchoMode(QLineEdit::Password);
    keyInput->setFixedWidth(200);
    
    QPushButton* saveKeyBtn = new QPushButton("Save Key", m_apiKeyBanner);
    
    bannerLayout->addWidget(warningIcon);
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(keyInput);
    bannerLayout->addWidget(saveKeyBtn);
    
    mainLayout->addWidget(m_apiKeyBanner);
    
    // Check if key is already saved to decide whether to hide
    {
        QSettings settings("PedalBoard", "PedalBoard");
        if (!settings.value("tone3000_api_key", "").toString().isEmpty()) {
            m_apiKeyBanner->hide();
        }
    }
    
    connect(saveKeyBtn, &QPushButton::clicked, this, [this, keyInput]() {
        QString text = keyInput->text().trimmed();
        if (!text.isEmpty()) {
            QSettings settings("PedalBoard", "PedalBoard");
            settings.setValue("tone3000_api_key", obfuscateKey(text));
            m_apiKeyBanner->hide();
            performSearch();
        }
    });

    // Search bar
    auto* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search NAM models (e.g. Plexi, Vox, Friedman, Twin)...");
    m_searchBtn = new QPushButton("Search", this);
    auto* sortLabel = new QLabel("Sort:", this);
    sortLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem("Trending", "trending");
    m_sortCombo->addItem("Most Downloaded", "downloads");
    m_sortCombo->addItem("Most Favorited", "favorites");
    m_sortCombo->addItem("Newest", "newest");
    m_sortCombo->addItem("Oldest", "oldest");
    m_sortCombo->addItem("Best Match", "best-match");
    m_sortCombo->setMaximumWidth(160);
    searchLayout->addWidget(m_searchEdit);
    searchLayout->addWidget(sortLabel);
    searchLayout->addWidget(m_sortCombo);
    searchLayout->addWidget(m_searchBtn);
    mainLayout->addLayout(searchLayout);

    // Filters row
    auto* filtersLayout = new QHBoxLayout();
    
    auto* gearLayout = new QHBoxLayout();
    auto* gearLabel = new QLabel("Filter by Gear Type:", this);
    gearLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    m_gearFilterCombo = new QComboBox(this);
    m_gearFilterCombo->addItems({
        "All Gears",
        "Full Rigs (Amp + Cab)",
        "Amps Only",
        "Cabs / IRs Only",
        "Stompboxes / Pedals"
    });
    gearLayout->addWidget(gearLabel);
    gearLayout->addWidget(m_gearFilterCombo);
    filtersLayout->addLayout(gearLayout);

    filtersLayout->addSpacing(15);

    auto* tagLayout = new QHBoxLayout();
    auto* tagLabel = new QLabel("Character/Tag:", this);
    tagLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    m_tagFilterCombo = new QComboBox(this);
    m_tagFilterCombo->addItems({
        "All Tags",
        "Clean",
        "Crunch",
        "Overdrive / Drive",
        "High-Gain / Metal",
        "Lead",
        "Vintage",
        "Rock"
    });
    tagLayout->addWidget(tagLabel);
    tagLayout->addWidget(m_tagFilterCombo);
    filtersLayout->addLayout(tagLayout);

    auto* archLayout = new QHBoxLayout();
    auto* archLabel = new QLabel("NAM Architecture:", this);
    archLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    m_archFilterCombo = new QComboBox(this);
    m_archFilterCombo->addItems({
        "All Versions (A1/A2)",
        "NAM v2 (A2) Only",
        "NAM v1 (A1) Only"
    });
    archLayout->addWidget(archLabel);
    archLayout->addWidget(m_archFilterCombo);
    filtersLayout->addLayout(archLayout);

    m_favoritesCheckbox = new QCheckBox("Favorites Only", this);
    m_favoritesCheckbox->setStyleSheet("color: #e2e2e6; font-weight: bold; margin-left: 15px;");
    filtersLayout->addWidget(m_favoritesCheckbox);
    
    mainLayout->addLayout(filtersLayout);

    // Splitter for Table and Details Panel
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet("QSplitter::handle { background-color: #2d3135; width: 4px; }");

    // Results table
    m_resultsTable = new QTableWidget(this);
    m_resultsTable->setColumnCount(5);
    m_resultsTable->setHorizontalHeaderLabels({"Title", "Author", "Type", "Downloads", "Favorites"});
    // Sorting is requested from TONE3000 so it applies across every page.
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->verticalHeader()->setVisible(false);
    connect(m_resultsTable->horizontalHeader(), &QHeaderView::sectionClicked, this, &Tone3000Dialog::onHeaderClicked);
    splitter->addWidget(m_resultsTable);

    // Setup Details Panel
    m_detailsArea = new QScrollArea(this);
    m_detailsArea->setWidgetResizable(true);
    m_detailsArea->setStyleSheet(
        "QScrollArea {"
        "  background-color: #161819;"
        "  border: 1px solid #2d3135;"
        "  border-radius: 4px;"
        "}"
    );

    auto* detailsContent = new QWidget(m_detailsArea);
    detailsContent->setStyleSheet("background-color: #161819; color: #e2e2e6;");
    auto* detailsLayout = new QVBoxLayout(detailsContent);
    detailsLayout->setContentsMargins(12, 12, 12, 12);
    detailsLayout->setSpacing(8);

    m_detailsTitleLabel = new QLabel("Select a Capture", detailsContent);
    m_detailsTitleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #00a3e0;");
    m_detailsTitleLabel->setWordWrap(true);
    detailsLayout->addWidget(m_detailsTitleLabel);

    m_detailsAuthorLabel = new QLabel("", detailsContent);
    m_detailsAuthorLabel->setStyleSheet("font-size: 11px; color: #a8aab0; font-style: italic;");
    detailsLayout->addWidget(m_detailsAuthorLabel);

    // Separator line
    auto* detailsSep = new QFrame(detailsContent);
    detailsSep->setFrameShape(QFrame::HLine);
    detailsSep->setFrameShadow(QFrame::Sunken);
    detailsSep->setStyleSheet("background-color: #2d3135; max-height: 1px; margin-top: 4px; margin-bottom: 4px;");
    detailsLayout->addWidget(detailsSep);

    // Stats layout
    m_detailsStatsLabel = new QLabel("", detailsContent);
    m_detailsStatsLabel->setStyleSheet("font-size: 11px; color: #a8aab0;");
    detailsLayout->addWidget(m_detailsStatsLabel);

    // Description
    auto* descTitle = new QLabel("Description / Info:", detailsContent);
    descTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: #a8aab0; margin-top: 4px;");
    detailsLayout->addWidget(descTitle);

    m_detailsDescText = new QLabel(detailsContent);
    m_detailsDescText->setStyleSheet("font-size: 12px; color: #e2e2e6;");
    m_detailsDescText->setWordWrap(true);
    m_detailsDescText->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_detailsDescText->setOpenExternalLinks(true);
    detailsLayout->addWidget(m_detailsDescText);

    // Make/Gear Details
    auto* gearTitle = new QLabel("Captured Gear & Details:", detailsContent);
    gearTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: #a8aab0; margin-top: 4px;");
    detailsLayout->addWidget(gearTitle);

    m_detailsGearLabel = new QLabel("", detailsContent);
    m_detailsGearLabel->setStyleSheet("font-size: 11px; color: #e2e2e6;");
    m_detailsGearLabel->setWordWrap(true);
    detailsLayout->addWidget(m_detailsGearLabel);

    // Tags
    auto* tagsTitle = new QLabel("Tags:", detailsContent);
    tagsTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: #a8aab0; margin-top: 4px;");
    detailsLayout->addWidget(tagsTitle);

    m_detailsTagsLabel = new QLabel("", detailsContent);
    m_detailsTagsLabel->setStyleSheet("font-size: 11px; color: #e2e2e6;");
    m_detailsTagsLabel->setWordWrap(true);
    detailsLayout->addWidget(m_detailsTagsLabel);

    detailsLayout->addStretch();
    m_detailsArea->setWidget(detailsContent);

    splitter->addWidget(m_detailsArea);
    mainLayout->addWidget(splitter, 1);

    // Set initial splitter stretch factor and hidden state
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);
    m_detailsArea->setVisible(false); // Hidden by default

    // Pagination Layout
    auto* paginationLayout = new QHBoxLayout();
    m_previousPageBtn = new QPushButton("Previous", this);
    m_nextPageBtn = new QPushButton("Next", this);
    m_pageLabel = new QLabel(this);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    
    m_infoToggleBtn = new QPushButton("ℹ Show Info", this);
    m_infoToggleBtn->setCheckable(true);
    m_infoToggleBtn->setChecked(false);
    m_infoToggleBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2d3135;"
        "  color: #e2e2e6;"
        "  border: 1px solid #43474a;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  font-weight: bold;"
        "  font-size: 13px;"
        "}"
        "QPushButton:checked {"
        "  background-color: #00a3e0;"
        "  color: #ffffff;"
        "  border: none;"
        "}"
    );

    paginationLayout->addWidget(m_previousPageBtn);
    paginationLayout->addStretch();
    paginationLayout->addWidget(m_pageLabel);
    paginationLayout->addStretch();
    paginationLayout->addWidget(m_nextPageBtn);
    paginationLayout->addSpacing(10);
    paginationLayout->addWidget(m_infoToggleBtn);
    mainLayout->addLayout(paginationLayout);

    connect(m_infoToggleBtn, &QPushButton::toggled, this, [splitter, this](bool checked) {
        m_detailsArea->setVisible(checked);
        if (checked) {
            splitter->setSizes({550, 250});
        }
    });

    m_previousPageBtn->setEnabled(false);
    m_nextPageBtn->setEnabled(false);
    m_pageLabel->setText("Page 1");

    // Bottom action area
    auto* bottomLayout = new QHBoxLayout();
    
    auto* modelSelectLayout = new QVBoxLayout();
    auto* modelLabel = new QLabel("Select Capture Variant:", this);
    modelLabel->setStyleSheet("color: #a8aab0; font-size: 11px; font-weight: bold;");
    m_modelsCombo = new QComboBox(this);
    m_modelsCombo->setEnabled(false);
    modelSelectLayout->setSpacing(4);
    modelSelectLayout->addWidget(modelLabel);
    modelSelectLayout->addWidget(m_modelsCombo);
    bottomLayout->addLayout(modelSelectLayout);

    bottomLayout->addSpacing(15);
    bottomLayout->setAlignment(Qt::AlignBottom);

    m_previewBtn = new QPushButton("Preview Tone", this);
    m_previewBtn->setEnabled(false);
    m_previewBtn->setStyleSheet(
        "QPushButton { background-color: #5E35B1; color: white; font-weight: bold; border-radius: 4px; padding: 6px 16px; border: none; }"
        "QPushButton:hover { background-color: #512DA8; }"
        "QPushButton:disabled { background-color: #2d3135; color: #8a8d90; }"
    );
    bottomLayout->addWidget(m_previewBtn);

    m_loadBtn = new QPushButton("Download & Load Tone", this);
    m_loadBtn->setEnabled(false);
    bottomLayout->addWidget(m_loadBtn);

    m_favoriteBtn = new QPushButton("☆ Favorite", this);
    m_favoriteBtn->setCheckable(true);
    m_favoriteBtn->setEnabled(false);
    m_favoriteBtn->setStyleSheet(
        "QPushButton { background-color: #2d3135; color: #e2e2e6; font-weight: bold; border-radius: 4px; padding: 6px 16px; border: 1px solid #43474a; }"
        "QPushButton:hover { background-color: #3d4145; }"
        "QPushButton:checked { background-color: #FFD700; color: #121212; border: none; font-weight: bold; }"
        "QPushButton:disabled { background-color: #2d3135; color: #8a8d90; border: none; }"
    );
    bottomLayout->addWidget(m_favoriteBtn);
    
    mainLayout->addLayout(bottomLayout);

    // Progress and status
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready.", this);
    m_statusLabel->setStyleSheet("color: #a8aab0; font-size: 12px;");
    mainLayout->addWidget(m_statusLabel);
}

void Tone3000Dialog::performSearch() {
    m_currentPage = 1;
    requestSearch();
}

void Tone3000Dialog::requestSearch() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply = nullptr;
    }

    m_modelsCombo->clear();
    m_modelsCombo->setEnabled(false);
    m_loadBtn->setEnabled(false);
    m_previewBtn->setEnabled(false);
    m_favoriteBtn->setEnabled(false);

    // Save filter settings
    saveFilterSettings();

    if (m_favoritesCheckbox->isChecked()) {
        m_previousPageBtn->setEnabled(false);
        m_nextPageBtn->setEnabled(false);
        m_pageLabel->setText("Favorites");
        loadFavoritesLocal();
        return;
    }

    m_statusLabel->setText("Searching TONE3000 library...");
    m_resultsTable->setRowCount(0);
    m_previousPageBtn->setEnabled(false);
    m_nextPageBtn->setEnabled(false);
    m_pageLabel->setText(QString("Page %1").arg(m_currentPage));
    
    QString query = m_searchEdit->text().trimmed();

    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    bool useOfficial = !savedKeyEnc.isEmpty();

    if (!useOfficial) {
        // --- GUEST SUPABASE API (POST) ---
        QJsonObject payload;
        payload["query_term"] = query;
        payload["page_number"] = m_currentPage;
        payload["page_size"] = PAGE_SIZE;
        QString sort = m_sortCombo->currentData().toString();
        if (sort == "favorites") {
            sort = "trending";
        }
        payload["order_by"] = sort;
        payload["make_names"] = QJsonValue::Null;

        int tagIdx = m_tagFilterCombo->currentIndex();
        if (tagIdx == 0) {
            payload["tag_names"] = QJsonValue::Null;
        } else {
            QJsonArray tagArr;
            if (tagIdx == 1) tagArr.append("clean");
            else if (tagIdx == 2) tagArr.append("crunch");
            else if (tagIdx == 3) { tagArr.append("overdrive"); tagArr.append("drive"); }
            else if (tagIdx == 4) { tagArr.append("high-gain"); tagArr.append("metal"); }
            else if (tagIdx == 5) tagArr.append("lead");
            else if (tagIdx == 6) tagArr.append("vintage");
            else if (tagIdx == 7) tagArr.append("rock");
            payload["tag_names"] = tagArr;
        }

        // Apply gear filter
        int gearIdx = m_gearFilterCombo->currentIndex();
        if (gearIdx == 0) {
            payload["gear_filters"] = QJsonValue::Null;
        } else {
            QJsonArray gearArr;
            if (gearIdx == 1) gearArr.append("amp-cab");
            else if (gearIdx == 2) gearArr.append("amp");
            else if (gearIdx == 3) gearArr.append("cab");
            else if (gearIdx == 4) gearArr.append("pedal");
            payload["gear_filters"] = gearArr;
        }

        payload["is_calibrated"] = false;
        payload["size_filters"] = QJsonValue::Null;
        payload["usernames"] = QJsonValue::Null;

        // Apply architecture/NAM version filter
        int archIdx = m_archFilterCombo->currentIndex();
        if (archIdx == 0) {
            payload["architecture_filter"] = QJsonValue::Null;
        } else if (archIdx == 1) {
            payload["architecture_filter"] = "2";
        } else {
            payload["architecture_filter"] = "1";
        }

        QNetworkRequest request;
        request.setUrl(QUrl(m_supabaseUrl + "/rest/v1/rpc/search_tones_a2"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("apikey", m_supabaseKey.toUtf8());
        request.setRawHeader("authorization", ("Bearer " + m_supabaseKey).toUtf8());

        QJsonDocument doc(payload);
        m_currentReply = m_networkManager->post(request, doc.toJson(QJsonDocument::Compact));
    } else {
        // --- OFFICIAL TONE3000 WEB API (GET) ---
        QString activeKey = deobfuscateKey(savedKeyEnc);
        
        QUrl url("https://www.tone3000.com/api/v1/tones/search");
        QUrlQuery q;
        
        QString queryStr = query;
        int tagIdx = m_tagFilterCombo->currentIndex();
        if (tagIdx > 0) {
            QString tagVal;
            if (tagIdx == 1) tagVal = "clean";
            else if (tagIdx == 2) tagVal = "crunch";
            else if (tagIdx == 3) tagVal = "overdrive";
            else if (tagIdx == 4) tagVal = "high-gain";
            else if (tagIdx == 5) tagVal = "lead";
            else if (tagIdx == 6) tagVal = "vintage";
            else if (tagIdx == 7) tagVal = "rock";
            
            if (!queryStr.isEmpty()) queryStr += " ";
            queryStr += tagVal;
        }
        
        if (!queryStr.isEmpty()) q.addQueryItem("query", queryStr);
        q.addQueryItem("page", QString::number(m_currentPage));
        q.addQueryItem("page_size", QString::number(PAGE_SIZE));
        
        QString sort = m_sortCombo->currentData().toString();
        if (sort == "favorites") {
            sort = !queryStr.isEmpty() ? "best-match" : "trending";
        } else if (sort == "downloads") {
            sort = "downloads-all-time";
        }
        q.addQueryItem("sort", sort);
        
        int gearIdx = m_gearFilterCombo->currentIndex();
        if (gearIdx > 0) {
            QString gearVal;
            if (gearIdx == 1) gearVal = "full-rig";
            else if (gearIdx == 2) gearVal = "amp";
            else if (gearIdx == 3) gearVal = "ir";
            else if (gearIdx == 4) gearVal = "pedal";
            q.addQueryItem("gears", gearVal);
        }
        
        int archIdx = m_archFilterCombo->currentIndex();
        if (archIdx == 1) q.addQueryItem("architecture", "2");
        else if (archIdx == 2) q.addQueryItem("architecture", "1");
        
        url.setQuery(q);
        
        QNetworkRequest request;
        request.setUrl(url);
        request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
        
        m_currentReply = m_networkManager->get(request);
    }
    
    connect(m_currentReply, &QNetworkReply::finished, this, &Tone3000Dialog::onSearchFinished);
}

void Tone3000Dialog::onSearchFinished() {
    if (!m_currentReply) return;
    
    if (m_currentReply->error() != QNetworkReply::NoError) {
        if (m_currentReply->error() != QNetworkReply::OperationCanceledError) {
            m_statusLabel->setText("Error connecting to TONE3000 API.");
            std::cerr << "Network Error: " << m_currentReply->errorString().toStdString() << std::endl;
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QByteArray data = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    QJsonArray tonesArray;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    bool isOfficialApi = false;

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("data") && obj["data"].isArray()) {
            tonesArray = obj["data"].toArray();
            isOfficialApi = true;
        } else {
            m_statusLabel->setText("Unexpected API response format.");
            return;
        }
    } else if (doc.isArray()) {
        tonesArray = doc.array();
    } else {
        m_statusLabel->setText("Unexpected API response format.");
        return;
    }

    m_resultsTable->setSortingEnabled(false);
    m_currentTones = tonesArray;

    // Local sort if sorting by favorites is selected
    if (m_sortCombo->currentData().toString() == "favorites") {
        std::vector<QJsonObject> tempVec;
        tempVec.reserve(m_currentTones.size());
        for (const auto& val : m_currentTones) {
            tempVec.push_back(val.toObject());
        }
        std::sort(tempVec.begin(), tempVec.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a["favorites_count"].toInt() > b["favorites_count"].toInt();
        });
        QJsonArray sortedArray;
        for (const auto& obj : tempVec) {
            sortedArray.append(obj);
        }
        m_currentTones = sortedArray;
    }

    m_hasNextPage = m_currentTones.size() == PAGE_SIZE;
    refreshTableRows();

    m_previousPageBtn->setEnabled(m_currentPage > 1);
    m_nextPageBtn->setEnabled(m_hasNextPage);
    m_pageLabel->setText(QString("Page %1").arg(m_currentPage));
    m_statusLabel->setText(QString("Showing %1 profiles on page %2.").arg(m_currentTones.size()).arg(m_currentPage));
}

void Tone3000Dialog::onPreviousPageClicked() {
    if (m_currentPage <= 1) return;
    --m_currentPage;
    requestSearch();
}

void Tone3000Dialog::onNextPageClicked() {
    if (!m_hasNextPage) return;
    ++m_currentPage;
    requestSearch();
}

void Tone3000Dialog::onRowSelectionChanged() {
    int row = m_resultsTable->currentRow();
    if (row < 0) {
        m_favoriteBtn->setEnabled(false);
        m_favoriteBtn->setChecked(false);
        m_favoriteBtn->setText("☆ Favorite");
        return;
    }

    QTableWidgetItem* titleItem = m_resultsTable->item(row, 0);
    if (!titleItem) {
        m_favoriteBtn->setEnabled(false);
        m_favoriteBtn->setChecked(false);
        m_favoriteBtn->setText("☆ Favorite");
        return;
    }

    int toneId = titleItem->data(Qt::UserRole).toInt();
    
    // Enable favorites only once the selected profile's variants are known.
    m_favoriteBtn->setEnabled(false);
    bool isFav = isFavoriteLocal(toneId);
    m_favoriteBtn->setChecked(isFav);
    m_favoriteBtn->setText(isFav ? "★ Favorite" : "☆ Favorite");

    // Update Details Panel
    if (row >= 0 && row < m_currentTones.size()) {
        QJsonObject tone = m_currentTones[row].toObject();
        
        m_detailsTitleLabel->setText(tone["title"].toString());
        
        QString username;
        if (tone.contains("user") && tone["user"].isObject()) {
            username = tone["user"].toObject()["username"].toString();
        } else {
            username = tone["username"].toString();
        }
        m_detailsAuthorLabel->setText("by " + username);
        
        int dls = tone["downloads_count"].toInt();
        int favs = tone["favorites_count"].toInt();
        m_detailsStatsLabel->setText(QString("📥 %1 downloads   ⭐ %2 favorites").arg(dls).arg(favs));
        
        QString desc = tone["description"].toString();
        if (desc.isEmpty()) {
            m_detailsDescText->setText("<span style='color: #8a8d90; font-style: italic;'>No description provided.</span>");
        } else {
            // Replace line breaks with HTML br for nice wrapping
            desc.replace("\n", "<br>");
            m_detailsDescText->setText(desc);
        }
        
        // Captured gear / info
        QString format = tone["format"].toString().toUpper();
        QString gearType = tone["gear"].toString();
        QString license = tone["license"].toString().toUpper();
        
        QString makesStr;
        if (tone.contains("makes") && tone["makes"].isArray()) {
            QJsonArray makes = tone["makes"].toArray();
            QStringList makeList;
            for (const auto& val : makes) {
                makeList << val.toObject()["name"].toString();
            }
            makesStr = makeList.join(", ");
        }
        if (makesStr.isEmpty()) {
            makesStr = "Unknown";
        }
        
        m_detailsGearLabel->setText(
            QString("<b>Format:</b> %1<br>"
                    "<b>Gear Type:</b> %2<br>"
                    "<b>Makes:</b> %3<br>"
                    "<b>License:</b> %4")
            .arg(format)
            .arg(gearType)
            .arg(makesStr)
            .arg(license)
        );
        
        // Tags
        QString tagsStr;
        if (tone.contains("tags") && tone["tags"].isArray()) {
            QJsonArray tags = tone["tags"].toArray();
            QStringList tagSpans;
            for (const auto& val : tags) {
                QString tag = val.toObject()["name"].toString();
                tagSpans << QString("<span style='background-color: #2d3135; color: #00a3e0; padding: 2px 6px; border-radius: 3px;'>#%1</span>").arg(tag);
            }
            tagsStr = tagSpans.join("  ");
        }
        if (tagsStr.isEmpty()) {
            tagsStr = "<span style='color: #8a8d90; font-style: italic;'>None</span>";
        }
        m_detailsTagsLabel->setText(tagsStr);
    }

    if (m_favoritesCheckbox->isChecked()) {
        // Load models list from local favorites
        QFile file(QDir::homePath() + "/.config/PedalBoard/favorites.json");
        if (file.open(QFile::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                for (int i = 0; i < arr.size(); ++i) {
                    QJsonObject fav = arr[i].toObject();
                    if (fav["id"].toInt() == toneId) {
                        m_currentModels = fav["models"].toArray();
                        m_modelsCombo->clear();
                        m_modelsCombo->setEnabled(!m_currentModels.isEmpty());
                        m_loadBtn->setEnabled(!m_currentModels.isEmpty());
                        m_previewBtn->setEnabled(!m_currentModels.isEmpty());
                        m_favoriteBtn->setEnabled(true);
                        
                        for (int j = 0; j < m_currentModels.size(); ++j) {
                            QJsonObject model = m_currentModels[j].toObject();
                            m_modelsCombo->addItem(model["name"].toString());
                        }
                        
                        if (!m_currentModels.isEmpty()) {
                            m_statusLabel->setText("Loaded local models for favorite.");
                        } else {
                            m_statusLabel->setText("No models found for this favorite.");
                        }
                        break;
                    }
                }
            }
            file.close();
        }
    } else {
        if (m_modelsByToneId.contains(toneId)) {
            populateModels(m_modelsByToneId.value(toneId));
        } else {
            fetchModelsForTone(toneId);
        }
    }
}

void Tone3000Dialog::refreshTableRows() {
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->setRowCount(m_currentTones.size());

    // Check if we are using the official API
    bool isOfficialApi = false;
    QSettings settings("PedalBoard", "PedalBoard");
    if (!settings.value("tone3000_api_key", "").toString().isEmpty()) {
        isOfficialApi = true;
    }

    for (int i = 0; i < m_currentTones.size(); ++i) {
        QJsonObject tone = m_currentTones[i].toObject();
        
        auto* titleItem = new QTableWidgetItem();
        titleItem->setData(Qt::DisplayRole, tone["title"].toString());
        titleItem->setData(Qt::UserRole, tone["id"].toInt());
        
        auto* authorItem = new QTableWidgetItem();
        QString username;
        if (isOfficialApi && tone.contains("user") && tone["user"].isObject()) {
            username = tone["user"].toObject()["username"].toString();
        } else {
            username = tone["username"].toString();
        }
        authorItem->setData(Qt::DisplayRole, username);
        
        auto* gearItem = new QTableWidgetItem();
        gearItem->setData(Qt::DisplayRole, tone["gear"].toString());
        
        auto* downloadsItem = new QTableWidgetItem();
        downloadsItem->setData(Qt::DisplayRole, tone["downloads_count"].toInt());

        auto* favoritesItem = new QTableWidgetItem();
        favoritesItem->setData(Qt::DisplayRole, tone["favorites_count"].toInt());

        // Styling items
        titleItem->setForeground(QColor("#e2e2e6"));
        authorItem->setForeground(QColor("#a8aab0"));
        gearItem->setForeground(QColor("#a8aab0"));
        downloadsItem->setForeground(QColor("#00a3e0"));
        favoritesItem->setForeground(QColor("#ffd700")); // gold for favorites

        m_resultsTable->setItem(i, 0, titleItem);
        m_resultsTable->setItem(i, 1, authorItem);
        m_resultsTable->setItem(i, 2, gearItem);
        m_resultsTable->setItem(i, 3, downloadsItem);
        m_resultsTable->setItem(i, 4, favoritesItem);
    }
}

void Tone3000Dialog::onHeaderClicked(int logicalIndex) {
    if (m_currentTones.isEmpty()) return;

    if (m_lastSortedColumn == logicalIndex) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_lastSortedColumn = logicalIndex;
        // Ascending by default for text columns (0, 1, 2), descending for numeric columns (3, 4)
        m_sortAscending = (logicalIndex == 0 || logicalIndex == 1 || logicalIndex == 2);
    }

    std::vector<QJsonObject> tempVec;
    tempVec.reserve(m_currentTones.size());
    for (const auto& val : m_currentTones) {
        tempVec.push_back(val.toObject());
    }

    bool isAsc = m_sortAscending;
    std::sort(tempVec.begin(), tempVec.end(), [logicalIndex, isAsc](const QJsonObject& a, const QJsonObject& b) {
        if (logicalIndex == 0) { // Title
            QString valA = a["title"].toString();
            QString valB = b["title"].toString();
            return isAsc ? (valA.localeAwareCompare(valB) < 0) : (valA.localeAwareCompare(valB) > 0);
        } else if (logicalIndex == 1) { // Author
            QString valA = a.contains("user") ? a["user"].toObject()["username"].toString() : a["username"].toString();
            QString valB = b.contains("user") ? b["user"].toObject()["username"].toString() : b["username"].toString();
            return isAsc ? (valA.localeAwareCompare(valB) < 0) : (valA.localeAwareCompare(valB) > 0);
        } else if (logicalIndex == 2) { // Type
            QString valA = a["gear"].toString();
            QString valB = b["gear"].toString();
            return isAsc ? (valA.localeAwareCompare(valB) < 0) : (valA.localeAwareCompare(valB) > 0);
        } else if (logicalIndex == 3) { // Downloads
            int valA = a["downloads_count"].toInt();
            int valB = b["downloads_count"].toInt();
            return isAsc ? (valA < valB) : (valA > valB);
        } else if (logicalIndex == 4) { // Favorites
            int valA = a["favorites_count"].toInt();
            int valB = b["favorites_count"].toInt();
            return isAsc ? (valA < valB) : (valA > valB);
        }
        return false;
    });

    QJsonArray sortedArray;
    for (const auto& obj : tempVec) {
        sortedArray.append(obj);
    }
    m_currentTones = sortedArray;

    refreshTableRows();
}

void Tone3000Dialog::fetchModelsForTone(int toneId) {
    if (m_modelsReply) {
        m_modelsReply->abort();
        m_modelsReply->deleteLater();
        m_modelsReply = nullptr;
    }

    m_statusLabel->setText("Fetching profile models...");
    m_currentModels = QJsonArray();
    m_modelsCombo->clear();
    m_modelsCombo->setEnabled(false);
    m_loadBtn->setEnabled(false);
    m_previewBtn->setEnabled(false);

    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    bool useOfficial = !savedKeyEnc.isEmpty();

    if (!useOfficial) {
        // --- GUEST SUPABASE API (GET) ---
        QString urlStr = QString("%1/rest/v1/models?select=id,name,model_url&tone_id=eq.%2")
                            .arg(m_supabaseUrl)
                            .arg(toneId);
        
        QNetworkRequest request;
        request.setUrl(QUrl(urlStr));
        request.setRawHeader("apikey", m_supabaseKey.toUtf8());
        request.setRawHeader("authorization", ("Bearer " + m_supabaseKey).toUtf8());
        m_modelsReply = m_networkManager->get(request);
    } else {
        // --- OFFICIAL TONE3000 WEB API (GET) ---
        QString activeKey = deobfuscateKey(savedKeyEnc);
        
        QUrl url("https://www.tone3000.com/api/v1/models");
        QUrlQuery q;
        q.addQueryItem("tone_id", QString::number(toneId));
        q.addQueryItem("page", "1");
        q.addQueryItem("page_size", "300");
        if (m_archFilterCombo->currentIndex() == 1) {
            q.addQueryItem("architecture", "2");
        } else if (m_archFilterCombo->currentIndex() == 2) {
            q.addQueryItem("architecture", "1");
        }
        url.setQuery(q);
        
        QNetworkRequest request;
        request.setUrl(url);
        request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
        
        m_modelsReply = m_networkManager->get(request);
    }

    QNetworkReply* reply = m_modelsReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, toneId]() {
        onModelsFinished(reply, toneId);
    });
}

void Tone3000Dialog::onModelsFinished(QNetworkReply* reply, int toneId) {
    if (reply != m_modelsReply) return;
    
    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText("Failed to load models.");
        reply->deleteLater();
        m_modelsReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_modelsReply = nullptr;

    QJsonArray modelsArray;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("data") && obj["data"].isArray()) {
            modelsArray = obj["data"].toArray();
        } else {
            m_statusLabel->setText("Unexpected models response.");
            return;
        }
    } else if (doc.isArray()) {
        modelsArray = doc.array();
    } else {
        m_statusLabel->setText("Unexpected models response.");
        return;
    }

    m_modelsByToneId.insert(toneId, modelsArray);

    // Cache every response, but only update the controls for the active selection.
    int selectedToneId = -1;
    if (auto* item = m_resultsTable->item(m_resultsTable->currentRow(), 0)) {
        selectedToneId = item->data(Qt::UserRole).toInt();
    }
    if (toneId != selectedToneId) return;
    populateModels(modelsArray);
}

void Tone3000Dialog::populateModels(const QJsonArray& models) {
    m_currentModels = models;
    m_modelsCombo->clear();
    if (m_currentModels.isEmpty()) {
        m_statusLabel->setText("No downloadable NAM models found for this profile.");
        return;
    }

    for (const auto& value : m_currentModels) {
        m_modelsCombo->addItem(value.toObject()["name"].toString());
    }

    m_modelsCombo->setEnabled(true);
    m_loadBtn->setEnabled(true);
    m_previewBtn->setEnabled(true);
    m_favoriteBtn->setEnabled(true);
    m_statusLabel->setText("Select capture variant to preview or load.");
}

void Tone3000Dialog::onDownloadClicked() {
    int idx = m_modelsCombo->currentIndex();
    if (idx < 0 || idx >= m_currentModels.size()) return;

    QJsonObject model = m_currentModels[idx].toObject();
    QString url = model["model_url"].toString();
    QString name = model["name"].toString();
    m_downloadedToneName = name;
    if (const int row = m_resultsTable->currentRow(); row >= 0 && row < m_currentTones.size()) {
        const QJsonObject tone = m_currentTones[row].toObject();
        const QString title = tone["title"].toString();
        if (!title.isEmpty() && title != name) m_downloadedToneName = title + " - " + name;
        m_downloadedToneUrl = tone["url"].toString();
        if (m_downloadedToneUrl.isEmpty()) {
            QString slug = tone["slug"].toString();
            if (slug.isEmpty()) {
                slug = title.toLower();
                slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
                slug.remove(QRegularExpression("^-|-$"));
                slug += "-" + QString::number(tone["id"].toInt());
            }
            m_downloadedToneUrl = "https://www.tone3000.com/tones/" + slug;
        }
    }
    
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Load Error", "Selected model has no download URL.");
        return;
    }

    QString safeName = name.replace(" ", "_").replace("/", "_").replace("\\", "_");
    if (!safeName.endsWith(".nam", Qt::CaseInsensitive)) {
        safeName += ".nam";
    }

    downloadModelFile(url, safeName, false);
}

void Tone3000Dialog::onPreviewClicked() {
    int idx = m_modelsCombo->currentIndex();
    if (idx < 0 || idx >= m_currentModels.size()) return;

    QJsonObject model = m_currentModels[idx].toObject();
    QString url = model["model_url"].toString();
    QString name = model["name"].toString();
    
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Load Error", "Selected model has no download URL.");
        return;
    }

    // Save as temporary preview file
    QString safeName = "preview_" + name.replace(" ", "_").replace("/", "_").replace("\\", "_");
    if (!safeName.endsWith(".nam", Qt::CaseInsensitive)) {
        safeName += ".nam";
    }

    downloadModelFile(url, safeName, true);
}

void Tone3000Dialog::downloadModelFile(const QString& url, const QString& filename, bool isPreview, bool isRedirect) {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply = nullptr;
    }

    m_previewDownload = isPreview;

    // Setup local cache path
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tone3000";
    QDir().mkpath(cacheDir);
    m_downloadedModelPath = (cacheDir + "/" + filename).toStdString();

    m_statusLabel->setText(isPreview ? "Downloading preview profile..." : "Downloading profile...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_loadBtn->setEnabled(false);
    m_previewBtn->setEnabled(false);

    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    bool useOfficial = !savedKeyEnc.isEmpty();

    // model_url is already a direct, signed/downloadable TONE3000 file URL.
    // Rewriting its host can return an HTML response instead of the NAM data.
    QString finalUrl = url;

    QNetworkRequest request;
    request.setUrl(QUrl(finalUrl));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    
    if (useOfficial && !isRedirect && QUrl(finalUrl).host().endsWith("tone3000.com")) {
        QString activeKey = deobfuscateKey(savedKeyEnc);
        request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
    }
    
    m_currentReply = m_networkManager->get(request);

    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &Tone3000Dialog::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::finished, this, &Tone3000Dialog::onDownloadFinished);
}

void Tone3000Dialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(percent);
        m_statusLabel->setText(QString("Downloading: %1%").arg(percent));
    }
}

void Tone3000Dialog::onDownloadFinished() {
    if (!m_currentReply) return;

    // Check for redirection manually to strip auth headers on redirect target
    QVariant redirectUrl = m_currentReply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectUrl.isValid()) {
        QUrl nextUrl = redirectUrl.toUrl();
        if (nextUrl.isRelative()) {
            nextUrl = m_currentReply->url().resolved(nextUrl);
        }
        QString fname = QFileInfo(QString::fromStdString(m_downloadedModelPath)).fileName();
        bool isPrev = m_previewDownload;
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        downloadModelFile(nextUrl.toString(), fname, isPrev, true);
        return;
    }

    m_progressBar->setVisible(false);
    m_loadBtn->setEnabled(true);
    m_previewBtn->setEnabled(true);

    if (m_currentReply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText("Download failed.");
        QMessageBox::critical(this, "Download Error", QString("Failed to download model file:\n%1").arg(m_currentReply->errorString()));
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QByteArray fileData = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    QFile file(QString::fromStdString(m_downloadedModelPath));
    if (!file.open(QIODevice::WriteOnly)) {
        m_statusLabel->setText("Failed to save file.");
        QMessageBox::critical(this, "Save Error", "Could not open local file for writing.");
        return;
    }

    file.write(fileData);
    file.close();

    if (m_previewDownload) {
        m_previewDownload = false;
        if (m_node && m_engine) {
            m_engine->suspendProcessing();
            m_node->loadModelFile(m_downloadedModelPath);
            m_engine->resumeProcessing();
            m_isPreviewing = true;
            m_statusLabel->setText("Live preview active! Play guitar to test.");
        }
    } else {
        // Store variants on the node before accepting
        if (m_node) {
            std::vector<AudioNode::ModelVariant> vars;
            for (int i = 0; i < m_currentModels.size(); ++i) {
                QJsonObject model = m_currentModels[i].toObject();
                AudioNode::ModelVariant var;
                var.name = model["name"].toString().toStdString();
                var.url = model["model_url"].toString().toStdString();
                
                if (i == m_modelsCombo->currentIndex()) {
                    var.localPath = m_downloadedModelPath;
                } else {
                    // Check if already cached
                    QString itemSafeName = model["name"].toString();
                    itemSafeName.replace(QRegularExpression("[^a-zA-Z0-9_\\-.]"), "_");
                    if (!itemSafeName.endsWith(".nam", Qt::CaseInsensitive) && !itemSafeName.endsWith(".json", Qt::CaseInsensitive)) {
                        itemSafeName += ".nam";
                    }
                    QString expectedPath = QDir::homePath() + "/.cache/PedalBoard/tone3000/" + itemSafeName;
                    if (QFile::exists(expectedPath)) {
                        var.localPath = expectedPath.toStdString();
                    }
                }
                vars.push_back(var);
            }
            m_node->setModelVariants(vars);
        }
        m_statusLabel->setText("Download successful!");
        accept();
    }
}

void Tone3000Dialog::reject() {
    if (m_isPreviewing && m_node && m_engine) {
        m_engine->suspendProcessing();
        m_node->loadModelFile(m_originalModelPath);
        m_engine->resumeProcessing();
    }
    QDialog::reject();
}

void Tone3000Dialog::onFavoritesToggled(bool checked) {
    m_sortCombo->setEnabled(!checked);
    m_gearFilterCombo->setEnabled(!checked);
    m_tagFilterCombo->setEnabled(!checked);
    m_archFilterCombo->setEnabled(!checked);
    performSearch();
}

void Tone3000Dialog::onFavoriteButtonClicked() {
    int row = m_resultsTable->currentRow();
    if (row < 0) return;
    
    QTableWidgetItem* titleItem = m_resultsTable->item(row, 0);
    if (!titleItem) return;
    
    int toneId = titleItem->data(Qt::UserRole).toInt();
    
    if (m_favoriteBtn->isChecked()) {
        QJsonObject selectedTone;
        for (int i = 0; i < m_currentTones.size(); ++i) {
            QJsonObject tone = m_currentTones[i].toObject();
            if (tone["id"].toInt() == toneId) {
                selectedTone = tone;
                break;
            }
        }
        
        if (!selectedTone.isEmpty()) {
            saveFavoriteLocal(selectedTone, m_currentModels);
            m_favoriteBtn->setText("★ Favorite");
            m_statusLabel->setText("Added to favorites.");
        }
    } else {
        removeFavoriteLocal(toneId);
        m_favoriteBtn->setText("☆ Favorite");
        m_statusLabel->setText("Removed from favorites.");
        
        if (m_favoritesCheckbox->isChecked()) {
            loadFavoritesLocal();
            m_modelsCombo->clear();
            m_modelsCombo->setEnabled(false);
            m_loadBtn->setEnabled(false);
            m_previewBtn->setEnabled(false);
            m_favoriteBtn->setEnabled(false);
        }
    }
}

void Tone3000Dialog::loadFavoritesLocal() {
    m_resultsTable->setRowCount(0);
    
    QString filePath = QDir::homePath() + "/.config/PedalBoard/favorites.json";
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly)) {
        m_statusLabel->setText("No favorites saved yet.");
        m_currentTones = QJsonArray();
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (!doc.isArray()) {
        m_statusLabel->setText("Favorites file corrupted.");
        m_currentTones = QJsonArray();
        return;
    }
    
    QJsonArray favoritesArray = doc.array();
    QString queryText = m_searchEdit->text().trimmed();
    QJsonArray filteredArray;
    
    for (int i = 0; i < favoritesArray.size(); ++i) {
        QJsonObject fav = favoritesArray[i].toObject();
        if (!queryText.isEmpty()) {
            QString title = fav["title"].toString();
            QString username = fav["username"].toString();
            if (!title.contains(queryText, Qt::CaseInsensitive) && 
                !username.contains(queryText, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filteredArray.append(fav);
    }
    
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->setRowCount(filteredArray.size());
    m_currentTones = filteredArray;
    
    for (int i = 0; i < filteredArray.size(); ++i) {
        QJsonObject fav = filteredArray[i].toObject();
        
        auto* titleItem = new QTableWidgetItem();
        titleItem->setData(Qt::DisplayRole, fav["title"].toString());
        titleItem->setData(Qt::UserRole, fav["id"].toInt());
        
        auto* authorItem = new QTableWidgetItem();
        authorItem->setData(Qt::DisplayRole, fav["username"].toString());
        
        auto* gearItem = new QTableWidgetItem();
        gearItem->setData(Qt::DisplayRole, fav["gear"].toString());
        
        auto* downloadsItem = new QTableWidgetItem();
        downloadsItem->setData(Qt::DisplayRole, fav["downloads_count"].toInt());
        
        auto* favoritesItem = new QTableWidgetItem();
        favoritesItem->setData(Qt::DisplayRole, fav["favorites_count"].toInt());
        
        titleItem->setForeground(QColor("#e2e2e6"));
        authorItem->setForeground(QColor("#a8aab0"));
        gearItem->setForeground(QColor("#a8aab0"));
        downloadsItem->setForeground(QColor("#00a3e0"));
        favoritesItem->setForeground(QColor("#ffd700"));
        
        m_resultsTable->setItem(i, 0, titleItem);
        m_resultsTable->setItem(i, 1, authorItem);
        m_resultsTable->setItem(i, 2, gearItem);
        m_resultsTable->setItem(i, 3, downloadsItem);
        m_resultsTable->setItem(i, 4, favoritesItem);
    }
    
    m_resultsTable->setSortingEnabled(false);
    m_statusLabel->setText(QString("Found %1 favorites.").arg(filteredArray.size()));
}

bool Tone3000Dialog::isFavoriteLocal(int toneId) const {
    QString filePath = QDir::homePath() + "/.config/PedalBoard/favorites.json";
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly)) return false;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (int i = 0; i < arr.size(); ++i) {
            if (arr[i].toObject()["id"].toInt() == toneId) {
                return true;
            }
        }
    }
    return false;
}

void Tone3000Dialog::saveFavoriteLocal(const QJsonObject& toneObj, const QJsonArray& modelsArray) {
    QString configDir = QDir::homePath() + "/.config/PedalBoard";
    QDir().mkpath(configDir);
    QString filePath = configDir + "/favorites.json";
    
    QJsonArray favoritesArray;
    QFile file(filePath);
    if (file.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            favoritesArray = doc.array();
        }
        file.close();
    }
    
    int toneId = toneObj["id"].toInt();
    for (int i = 0; i < favoritesArray.size(); ++i) {
        if (favoritesArray[i].toObject()["id"].toInt() == toneId) {
            return;
        }
    }
    
    QJsonObject favObj = toneObj;
    favObj["models"] = modelsArray;
    favoritesArray.append(favObj);
    
    if (file.open(QFile::WriteOnly)) {
        QJsonDocument doc(favoritesArray);
        file.write(doc.toJson());
        file.close();
    }
}

void Tone3000Dialog::removeFavoriteLocal(int toneId) {
    QString filePath = QDir::homePath() + "/.config/PedalBoard/favorites.json";
    QJsonArray favoritesArray;
    QFile file(filePath);
    if (file.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            favoritesArray = doc.array();
        }
        file.close();
    }
    
    QJsonArray newFavorites;
    for (int i = 0; i < favoritesArray.size(); ++i) {
        QJsonObject fav = favoritesArray[i].toObject();
        if (fav["id"].toInt() != toneId) {
            newFavorites.append(fav);
        }
    }
    
    if (file.open(QFile::WriteOnly)) {
        QJsonDocument doc(newFavorites);
        file.write(doc.toJson());
        file.close();
    }
}

void Tone3000Dialog::saveFilterSettings() {
    QString configDir = QDir::homePath() + "/.config/PedalBoard";
    QDir().mkpath(configDir);
    QString filePath = configDir + "/browser_settings.json";
    
    QJsonObject settings;
    settings["search"] = m_searchEdit->text();
    settings["sort"] = m_sortCombo->currentIndex();
    settings["gear"] = m_gearFilterCombo->currentIndex();
    settings["tag"] = m_tagFilterCombo->currentIndex();
    settings["arch"] = m_archFilterCombo->currentIndex();
    settings["favorites_only"] = m_favoritesCheckbox->isChecked();
    
    QFile file(filePath);
    if (file.open(QFile::WriteOnly)) {
        QJsonDocument doc(settings);
        file.write(doc.toJson());
        file.close();
    }
}

void Tone3000Dialog::loadFilterSettings() {
    QString filePath = QDir::homePath() + "/.config/PedalBoard/browser_settings.json";
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (doc.isObject()) {
        QJsonObject settings = doc.object();
        
        m_searchEdit->blockSignals(true);
        m_sortCombo->blockSignals(true);
        m_gearFilterCombo->blockSignals(true);
        m_tagFilterCombo->blockSignals(true);
        m_archFilterCombo->blockSignals(true);
        m_favoritesCheckbox->blockSignals(true);
        
        if (settings.contains("search")) m_searchEdit->setText(settings["search"].toString());
        if (settings.contains("sort")) m_sortCombo->setCurrentIndex(settings["sort"].toInt());
        if (settings.contains("gear")) m_gearFilterCombo->setCurrentIndex(settings["gear"].toInt());
        if (settings.contains("tag")) m_tagFilterCombo->setCurrentIndex(settings["tag"].toInt());
        if (settings.contains("arch")) m_archFilterCombo->setCurrentIndex(settings["arch"].toInt());
        if (settings.contains("favorites_only")) {
            bool favs = settings["favorites_only"].toBool();
            m_favoritesCheckbox->setChecked(favs);
            m_sortCombo->setEnabled(!favs);
            m_gearFilterCombo->setEnabled(!favs);
            m_tagFilterCombo->setEnabled(!favs);
            m_archFilterCombo->setEnabled(!favs);
        }
        
        m_searchEdit->blockSignals(false);
        m_sortCombo->blockSignals(false);
        m_gearFilterCombo->blockSignals(false);
        m_tagFilterCombo->blockSignals(false);
        m_archFilterCombo->blockSignals(false);
        m_favoritesCheckbox->blockSignals(false);
    }
}

QString Tone3000Dialog::getEffectiveApiKey() const {
    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    if (!savedKeyEnc.isEmpty()) {
        return deobfuscateKey(savedKeyEnc);
    }
    return m_supabaseKey;
}
