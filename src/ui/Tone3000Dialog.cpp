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
#include <QToolButton>
#include <QMenu>
#include <QWidgetAction>
#include <QScrollBar>
#include <QFrame>
#include <QStyle>
#include <functional>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QListWidget>

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

class ToneCardFrame : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void()> onClicked;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (auto* label = qobject_cast<QLabel*>(childAt(event->position().toPoint()));
            label && label->openExternalLinks()) {
            event->ignore();
            return;
        }
        if (onClicked) onClicked();
        QFrame::mousePressEvent(event);
    }
};

Tone3000Dialog::Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent) 
    : QDialog(parent), m_node(node), m_engine(engine) {
    
    m_originalModelPath = node ? node->getModelFilePath() : "";
    
    setupUI();
    m_networkManager = new QNetworkAccessManager(this);
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(400);
    
    // Load filter settings
    loadFilterSettings();
    
    // Connect search and sorting
    connect(m_searchBtn, &QPushButton::clicked, this, &Tone3000Dialog::performSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &Tone3000Dialog::performSearch);
    connect(m_searchEdit, &QLineEdit::textChanged, m_searchDebounceTimer, qOverload<>(&QTimer::start));
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &Tone3000Dialog::performSearch);
    
    // Connect filters
    connect(m_gearFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_characterFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_archFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_sizeFilterCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_calibratedCheckbox, &QCheckBox::toggled, this, &Tone3000Dialog::performSearch);
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &Tone3000Dialog::performSearch);
    connect(m_favoritesCheckbox, &QCheckBox::toggled, this, &Tone3000Dialog::onFavoritesToggled);
    
    // Download load and preview buttons
    connect(m_loadBtn, &QPushButton::clicked, this, &Tone3000Dialog::onDownloadClicked);
    connect(m_previewBtn, &QPushButton::clicked, this, &Tone3000Dialog::onPreviewClicked);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &Tone3000Dialog::onFavoriteButtonClicked);
    connect(m_modelsCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_isPopulatingModels || index < 0 || index >= m_currentModels.size()) return;
        const QSignalBlocker blocker(m_variantsList);
        for (int row = 0; row < m_variantsList->count(); ++row) {
            QListWidgetItem* item = m_variantsList->item(row);
            const QString label = item->data(Qt::UserRole).toString();
            item->setText(label);
            item->setIcon(row == index ? style()->standardIcon(QStyle::SP_MediaPlay) : QIcon());
            item->setForeground(row == index ? QColor("#ffffff") : QColor("#dce2e6"));
        }
        m_variantsList->setCurrentRow(index);
        m_loadBtn->setEnabled(true);
        m_previewBtn->setEnabled(true);
        onPreviewClicked();
    });
    connect(m_resultsArea->verticalScrollBar(), &QScrollBar::valueChanged, this, &Tone3000Dialog::onScrollChanged);
    
    // Load trending tones on startup
    QMetaObject::invokeMethod(this, "performSearch", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, [this]() { fetchFavoriteIds(); }, Qt::QueuedConnection);
}

void Tone3000Dialog::setupUI() {
    setWindowTitle("TONE3000 Profiles Browser");
    setFocusPolicy(Qt::StrongFocus);
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::min(1200, available.width() - 80), std::min(860, available.height() - 80));
    
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
        "QScrollArea { background-color: #141618; border: 1px solid #2d3135; border-radius: 8px; }"
        "QScrollBar:vertical { background: #17191b; width: 10px; margin: 2px; border-radius: 5px; }"
        "QScrollBar::handle:vertical { background: #43474a; border-radius: 5px; min-height: 32px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QComboBox {"
        "  background-color: #2d3135;"
        "  color: #e2e2e6;"
        "  border: 1px solid #43474a;"
        "  border-radius: 4px;"
        "  padding: 6px;"
        "  min-width: 130px;"
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

    // API Key Required Banner
    m_apiKeyBanner = new QWidget(this);
    m_apiKeyBanner->setStyleSheet(
        "QWidget { background-color: #171d27; border: 1px solid #00B0FF; border-radius: 6px; }"
        "QLabel { color: #ECECF0; font-size: 11px; border: none; background: transparent; }"
        "QLineEdit { background-color: #222834; color: #ffffff; border: 1px solid #384656; padding: 5px; font-size: 11px; border-radius: 4px; }"
        "QPushButton { background-color: #00B0FF; color: #000000; font-weight: bold; border-radius: 4px; padding: 5px 12px; font-size: 11px; }"
        "QPushButton:hover { background-color: #38c5ff; }"
    );
    QHBoxLayout* bannerLayout = new QHBoxLayout(m_apiKeyBanner);
    bannerLayout->setContentsMargins(12, 8, 12, 8);
    bannerLayout->setSpacing(10);
    
    QLabel* warningIcon = new QLabel("🔑", m_apiKeyBanner);
    warningIcon->setStyleSheet("font-size: 16px;");
    
    QLabel* bannerText = new QLabel(
        "<b>TONE3000 Secret Key Required:</b> Enter your Secret Key (starts with <code>t3k_cs_...</code>) from "
        "<a href='https://tone3000.com/settings' style='color:#00B0FF;'>tone3000.com/settings</a> -> <b>API & Developer Keys</b>:",
        m_apiKeyBanner
    );
    bannerText->setOpenExternalLinks(true);
    
    QLineEdit* keyInput = new QLineEdit(m_apiKeyBanner);
    keyInput->setPlaceholderText("Paste t3k_cs_... Secret Key");
    keyInput->setEchoMode(QLineEdit::Password);
    keyInput->setFixedWidth(220);
    
    QPushButton* saveKeyBtn = new QPushButton("Save Key", m_apiKeyBanner);
    
    bannerLayout->addWidget(warningIcon);
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(keyInput);
    bannerLayout->addWidget(saveKeyBtn);
    
    mainLayout->addWidget(m_apiKeyBanner);
    
    // Hide banner if key is present
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
            m_favoritesCheckbox->setText("TONE3000 Favorites");
            m_favoritesCheckbox->setToolTip("Shows favorites from your TONE3000 account");
            performSearch();
        }
    });

    // Search bar
    auto* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search tones, amps, or creators...");
    m_searchBtn = new QPushButton("Search", this);
    auto* sortLabel = new QLabel("Sort", this);
    sortLabel->setStyleSheet("color: #a8aab0; font-size: 11px; font-weight: bold;");
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem("Trending", "trending");
    m_sortCombo->addItem("Best Match", "best-match");
    m_sortCombo->addItem("Most Downloaded", "downloads");
    m_sortCombo->addItem("Most Favorited", "favorites");
    m_sortCombo->addItem("Newest", "newest");
    m_sortCombo->addItem("Oldest", "oldest");
    m_sortCombo->setMinimumWidth(145);
    searchLayout->addWidget(m_searchEdit);
    searchLayout->addWidget(sortLabel);
    searchLayout->addWidget(m_sortCombo);
    searchLayout->addWidget(m_searchBtn);
    mainLayout->addLayout(searchLayout);

    auto* browseLayout = new QHBoxLayout();
    browseLayout->setContentsMargins(0, 0, 0, 0);
    browseLayout->setSpacing(8);
    auto addFilterLabel = [&browseLayout, this](const QString& text) {
        auto* label = new QLabel(text, this);
        label->setStyleSheet("color: #a8aab0; font-size: 11px; font-weight: bold;");
        browseLayout->addWidget(label);
    };

    m_gearFilterCombo = new QComboBox(this);
    m_gearFilterCombo->addItem("Amp + Cab", "amp-cab");
    m_gearFilterCombo->addItem("All gear", "");
    m_gearFilterCombo->addItem("Amp", "amp");
    m_gearFilterCombo->addItem("Cab", "cab");
    m_gearFilterCombo->addItem("Pedal", "pedal");
    m_gearFilterCombo->addItem("Outboard", "outboard");

    m_characterFilterCombo = new QComboBox(this);
    m_characterFilterCombo->addItem("All characters", "");
    m_characterFilterCombo->addItem("Clean", "clean");
    m_characterFilterCombo->addItem("Crunch", "crunch");
    m_characterFilterCombo->addItem("Drive", "overdrive");
    m_characterFilterCombo->addItem("High gain", "high-gain");
    m_characterFilterCombo->addItem("Lead", "lead");
    m_characterFilterCombo->addItem("Vintage", "vintage");
    m_characterFilterCombo->addItem("Rock", "rock");

    m_archFilterCombo = new QComboBox(this);
    m_archFilterCombo->addItem("A2", "2");
    m_archFilterCombo->addItem("A1 + Custom (legacy)", "");
    m_archFilterCombo->addItem("A1", "1");
    m_sizeFilterCombo = new QComboBox(this);
    m_sizeFilterCombo->addItem("All sizes", "");
    m_sizeFilterCombo->addItem("Standard", "standard");
    m_sizeFilterCombo->addItem("Lite", "lite");
    m_sizeFilterCombo->addItem("Feather", "feather");
    m_sizeFilterCombo->addItem("Nano", "nano");
    m_calibratedCheckbox = new QCheckBox("Calibrated", this);
    m_favoritesCheckbox = new QCheckBox("TONE3000 Favorites", this);
    m_favoritesCheckbox->setToolTip("Shows favorites from your TONE3000 account");

    addFilterLabel("Gear");
    browseLayout->addWidget(m_gearFilterCombo);
    addFilterLabel("Character");
    browseLayout->addWidget(m_characterFilterCombo);
    addFilterLabel("NAM");
    browseLayout->addWidget(m_archFilterCombo);
    addFilterLabel("Size");
    browseLayout->addWidget(m_sizeFilterCombo);
    browseLayout->addWidget(m_calibratedCheckbox);
    browseLayout->addWidget(m_favoritesCheckbox);
    browseLayout->addStretch();
    mainLayout->addLayout(browseLayout);

    m_filterChipsWidget = new QWidget(this);
    m_filterChipsLayout = new QHBoxLayout(m_filterChipsWidget);
    m_filterChipsLayout->setContentsMargins(0, 0, 0, 0);
    m_filterChipsLayout->setSpacing(6);
    mainLayout->addWidget(m_filterChipsWidget);

    auto* contentSplitter = m_contentSplitter = new QSplitter(Qt::Horizontal, this);
    m_resultsArea = new QScrollArea(contentSplitter);
    m_resultsArea->setWidgetResizable(true);
    m_resultsContent = new QWidget(m_resultsArea);
    m_resultsContent->setStyleSheet("background-color: #141618;");
    m_resultsLayout = new QVBoxLayout(m_resultsContent);
    m_resultsLayout->setContentsMargins(10, 10, 10, 10);
    m_resultsLayout->setSpacing(10);
    m_resultsLayout->addStretch();
    m_resultsArea->setWidget(m_resultsContent);
    m_infoArea = new QScrollArea(contentSplitter);
    m_infoArea->setWidgetResizable(true);
    auto* infoContent = new QWidget(m_infoArea);
    auto* infoLayout = new QVBoxLayout(infoContent);
    infoLayout->setContentsMargins(14, 14, 14, 14);
    infoLayout->setAlignment(Qt::AlignTop);
    m_infoTitleLabel = new QLabel("Capture details", infoContent);
    m_infoTitleLabel->setWordWrap(true);
    m_infoTitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_infoTitleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00a3e0;");
    m_infoCreatorLabel = new QLabel("Select a tone to see capture notes, tags, and links.", infoContent);
    m_infoCreatorLabel->setWordWrap(true);
    m_infoCreatorLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_infoCreatorLabel->setStyleSheet("color: #a8aab0; font-size: 12px;");
    m_infoText = new QTextBrowser(infoContent);
    m_infoText->setOpenExternalLinks(false);
    m_infoText->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_infoText->setStyleSheet("QTextBrowser { background: transparent; border: none; color: #e2e2e6; font-size: 12px; }");
    connect(m_infoText, &QTextBrowser::anchorClicked, this, &Tone3000Dialog::onInfoLinkClicked);
    m_variantsLabel = new QLabel("Capture variants", infoContent);
    m_variantsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_variantsLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #dce2e6; margin-top: 8px;");
    m_variantsList = new QListWidget(infoContent);
    m_variantsList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_variantsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_variantsList->installEventFilter(this);
    m_variantsList->setStyleSheet(
        "QListWidget { background: #1b2024; border: 1px solid #33404a; border-radius: 5px; color: #dce2e6; }"
        "QListWidget::item { padding: 5px 8px; border-bottom: 1px solid #283138; }"
        "QListWidget::item:selected { background: #00897B; color: #ffffff; font-weight: bold; }"
        "QListWidget::item:hover { background: #24313a; }"
    );
    connect(m_variantsList, &QListWidget::currentRowChanged, this, [this](int index) {
        if (index >= 0 && index < m_currentModels.size() && index != m_modelsCombo->currentIndex()) {
            m_modelsCombo->setCurrentIndex(index);
        }
    });
    infoLayout->addWidget(m_infoTitleLabel);
    infoLayout->addWidget(m_infoCreatorLabel);
    infoLayout->addWidget(m_infoText);
    infoLayout->addWidget(m_variantsLabel);
    infoLayout->addWidget(m_variantsList);
    m_infoArea->setWidget(infoContent);
    contentSplitter->addWidget(m_resultsArea);
    contentSplitter->addWidget(m_infoArea);
    contentSplitter->setStretchFactor(0, 7);
    contentSplitter->setStretchFactor(1, 3);
    contentSplitter->setSizes({780, 340});
    m_infoArea->hide();
    mainLayout->addWidget(contentSplitter, 1);

    auto* paginationLayout = new QHBoxLayout();
    m_pageLabel = new QLabel(this);
    m_pageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_pageLabel->setStyleSheet("color: #a8aab0; font-size: 12px; font-weight: bold;");
    paginationLayout->addWidget(m_pageLabel);
    paginationLayout->addStretch();
    m_infoToggleBtn = new QPushButton("Show Info", this);
    m_infoToggleBtn->setCheckable(true);
    m_infoToggleBtn->setStyleSheet("QPushButton { background-color: #2d3135; color: #c5dde8; border: 1px solid #43474a; } QPushButton:checked { background-color: #173241; color: #80d8ff; border-color: #00a3e0; }");
    paginationLayout->addWidget(m_infoToggleBtn);
    mainLayout->addLayout(paginationLayout);
    m_pageLabel->setText("Ready");

    m_modelsCombo = new QComboBox(this);
    m_modelsCombo->setPlaceholderText("Select a capture variant");
    m_previewBtn = new QPushButton("Preview", this);
    m_previewBtn->hide();
    m_loadBtn = new QPushButton("Load", this);
    m_favoriteBtn = new QPushButton("☆ Favorite", this);
    m_favoriteBtn->setCheckable(true);
    m_modelsCombo->setEnabled(false);
    m_previewBtn->setEnabled(false);
    m_loadBtn->setEnabled(false);
    m_favoriteBtn->setEnabled(false);
    auto* actionLayout = new QHBoxLayout();
    auto* captureLabel = new QLabel("Capture", this);
    captureLabel->setStyleSheet("color: #a8aab0; font-size: 11px; font-weight: bold;");
    actionLayout->addWidget(captureLabel);
    actionLayout->addWidget(m_modelsCombo, 1);
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_favoriteBtn);
    mainLayout->addLayout(actionLayout);

    connect(m_infoToggleBtn, &QPushButton::toggled, this, [contentSplitter, this](bool visible) {
        m_infoArea->setVisible(visible);
        m_infoToggleBtn->setText(visible ? "Hide Info" : "Show Info");
        if (visible) {
            contentSplitter->setSizes({780, 340});
            QTimer::singleShot(0, this, [this]() {
                m_infoText->document()->setTextWidth(m_infoText->viewport()->width());
                m_infoText->setFixedHeight(static_cast<int>(m_infoText->document()->size().height()) + 8);
            });
        }
    });

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
    if (m_searchDebounceTimer) m_searchDebounceTimer->stop();
    ++m_searchGeneration;
    m_currentPage = 1;
    m_totalPages = 0;
    m_totalResults = 0;
    m_hasNextPage = false;
    m_isLoadingPage = false;
    m_selectedToneIndex = -1;
    m_selectedToneId = -1;
    m_currentTones = QJsonArray();
    requestSearch();
}

void Tone3000Dialog::setInitialSearchQuery(const QString& query) {
    if (m_searchEdit) {
        m_searchEdit->setText(query);
        performSearch();
    }
}

void Tone3000Dialog::setInitialTone(const QString& sourceUrl, const QString& captureName) {
    m_initialToneUrl = sourceUrl;
    setInitialSearchQuery(captureName);
}

void Tone3000Dialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        if (m_currentTones.isEmpty()) return;
        const int direction = event->key() == Qt::Key_Up ? -1 : 1;
        const int next = std::clamp(m_selectedToneIndex < 0 ? (direction > 0 ? -1 : 0) : m_selectedToneIndex + direction,
                                    0, static_cast<int>(m_currentTones.size()) - 1);
        selectTone(next);
        if (QLayoutItem* item = m_resultsLayout->itemAt(next)) {
            if (QWidget* card = item->widget()) m_resultsArea->ensureWidgetVisible(card);
        }
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool Tone3000Dialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_variantsList && event->type() == QEvent::Wheel && m_variantsList->count() <= 40) {
        const auto* wheelEvent = static_cast<QWheelEvent*>(event);
        QScrollBar* scrollBar = m_infoArea->verticalScrollBar();
        scrollBar->setValue(scrollBar->value() - wheelEvent->angleDelta().y());
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void Tone3000Dialog::requestSearch() {
    requestPage(1, false);
}

void Tone3000Dialog::fetchFavoriteIds(int page) {
    QSettings settings("PedalBoard", "PedalBoard");
    const QString savedKey = settings.value("tone3000_api_key", "").toString();
    if (savedKey.isEmpty()) return;

    if (m_favoritesReply) {
        QNetworkReply* reply = m_favoritesReply.data();
        m_favoritesReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    QUrl url("https://www.tone3000.com/api/v1/tones/favorited");
    QUrlQuery query;
    query.addQueryItem("page", QString::number(page));
    query.addQueryItem("page_size", "100");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + deobfuscateKey(savedKey)).toUtf8());
    m_favoritesReply = m_networkManager->get(request);
    QNetworkReply* reply = m_favoritesReply.data();
    connect(reply, &QNetworkReply::finished, this, [this, reply, page]() {
        onFavoriteIdsFinished(reply, page);
    });
}

void Tone3000Dialog::onFavoriteIdsFinished(QNetworkReply* reply, int page) {
    if (reply != m_favoritesReply.data()) return;
    m_favoritesReply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (!document.isObject()) return;
    const QJsonObject response = document.object();
    const QJsonArray tones = response.value("data").toArray();
    for (const QJsonValue& value : tones) m_favoriteToneIds.insert(value.toObject().value("id").toInt());

    if (!m_currentTones.isEmpty()) rebuildCards();
    const int totalPages = response.value("total_pages").toInt();
    if (totalPages > page) fetchFavoriteIds(page + 1);
}

QString Tone3000Dialog::requestCacheKey(int page) const {
    return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(m_searchEdit->text().trimmed())
        .arg(m_gearFilterCombo->currentData().toString())
        .arg(m_characterFilterCombo->currentData().toString())
        .arg(m_archFilterCombo->currentData().toString())
        .arg(m_sizeFilterCombo->currentData().toString())
        .arg(m_calibratedCheckbox->isChecked() ? "1" : "0")
        .arg(m_sortCombo->currentData().toString())
        .arg(m_favoritesCheckbox->isChecked() ? "favorites" : "catalog")
        .arg(page);
}

void Tone3000Dialog::requestPage(int page, bool append) {
    if (m_currentReply) {
        QNetworkReply* reply = m_currentReply.data();
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    if (!append) {
        m_modelsCombo->clear();
        m_modelsCombo->setEnabled(false);
        m_loadBtn->setEnabled(false);
        m_previewBtn->setEnabled(false);
        m_favoriteBtn->setEnabled(false);
        rebuildCards();
    }

    // Save filter settings
    saveFilterSettings();

    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    const bool useOfficial = !savedKeyEnc.isEmpty();
    if (m_favoritesCheckbox->isChecked() && !useOfficial) {
        m_pageLabel->setText("Favorites");
        loadFavoritesLocal();
        return;
    }

    updateActiveFilterChips();
    m_isLoadingPage = true;
    m_statusLabel->setText(append ? "Loading more TONE3000 profiles..." : "Searching TONE3000 library...");
    m_pageLabel->setText(QString("Page %1").arg(page));

    const QString cacheKey = requestCacheKey(page);
    if (m_pageCache.contains(cacheKey)) {
        QJsonArray cached = m_pageCache.value(cacheKey);
        const QJsonObject metadata = m_pageMetadata.value(cacheKey);
        m_totalResults = metadata.value("total").toInt(m_totalResults);
        m_totalPages = metadata.value("totalPages").toInt(m_totalPages);
        if (!append) m_currentTones = QJsonArray();
        for (const auto& v : cached) m_currentTones.append(v);
        m_currentPage = page;
        m_hasNextPage = m_totalPages > 0 ? page < m_totalPages : cached.size() == PAGE_SIZE;
        m_isLoadingPage = false;
        rebuildCards();
        m_statusLabel->setText(QString("Showing %1 cached profiles.").arg(m_currentTones.size()));
        return;
    }
    
    QString query = m_searchEdit->text().trimmed();

    if (!useOfficial) {
        // --- GUEST SUPABASE API (POST) ---
        QJsonObject payload;
        payload["query_term"] = query;
        payload["page_number"] = page;
        payload["page_size"] = PAGE_SIZE;
        QString sort = m_sortCombo->currentData().toString();
        if (sort == "favorites") {
            sort = "trending";
        }
        payload["order_by"] = sort;
        payload["make_names"] = QJsonValue::Null;

        const QString character = m_characterFilterCombo->currentData().toString();
        if (character.isEmpty()) {
            payload["tag_names"] = QJsonValue::Null;
        } else {
            QJsonArray tags;
            tags.append(character);
            payload["tag_names"] = tags;
        }

        // Apply gear filter
        QString gear = m_gearFilterCombo->currentData().toString();
        if (gear.isEmpty()) {
            payload["gear_filters"] = QJsonValue::Null;
        } else {
            QJsonArray gearArr;
            gearArr.append(gear);
            payload["gear_filters"] = gearArr;
        }

        payload["is_calibrated"] = m_calibratedCheckbox->isChecked();
        QString size = m_sizeFilterCombo->currentData().toString();
        if (size.isEmpty()) payload["size_filters"] = QJsonValue::Null;
        else { QJsonArray sizes; sizes.append(size); payload["size_filters"] = sizes; }
        payload["usernames"] = QJsonValue::Null;

        QString arch = m_archFilterCombo->currentData().toString();
        if (arch.isEmpty()) {
            payload["architecture_filter"] = QJsonValue::Null;
        } else {
            payload["architecture_filter"] = arch;
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
        
        QUrl url(m_favoritesCheckbox->isChecked()
            ? "https://www.tone3000.com/api/v1/tones/favorited"
            : "https://www.tone3000.com/api/v1/tones/search");
        QUrlQuery q;
        
        QString queryStr = query;
        const QString character = m_characterFilterCombo->currentData().toString();
        if (!character.isEmpty()) {
            if (!queryStr.isEmpty()) queryStr += " ";
            queryStr += character;
        }
        if (!queryStr.isEmpty() && !m_favoritesCheckbox->isChecked()) q.addQueryItem("query", queryStr);
        q.addQueryItem("page", QString::number(page));
        q.addQueryItem("page_size", QString::number(PAGE_SIZE));
        if (!m_favoritesCheckbox->isChecked()) q.addQueryItem("format", "nam");
        
        QString sort = m_sortCombo->currentData().toString();
        if (sort == "favorites") {
            sort = !queryStr.isEmpty() ? "best-match" : "trending";
        } else if (sort == "downloads") {
            sort = "downloads-all-time";
        }
        if (!m_favoritesCheckbox->isChecked()) q.addQueryItem("sort", sort);
        
        QString gear = m_gearFilterCombo->currentData().toString();
        if (!gear.isEmpty() && !m_favoritesCheckbox->isChecked()) q.addQueryItem("gears", gear);
        
        QString arch = m_archFilterCombo->currentData().toString();
        if (!arch.isEmpty() && !m_favoritesCheckbox->isChecked()) q.addQueryItem("architecture", arch);
        QString size = m_sizeFilterCombo->currentData().toString();
        if (!size.isEmpty() && !m_favoritesCheckbox->isChecked()) q.addQueryItem("sizes", size);
        if (m_calibratedCheckbox->isChecked() && !m_favoritesCheckbox->isChecked()) q.addQueryItem("calibrated", "true");
        
        url.setQuery(q);
        
        QNetworkRequest request;
        request.setUrl(url);
        if (!activeKey.isEmpty()) {
            request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
        }
        
        m_currentReply = m_networkManager->get(request);
    }
    
    QNetworkReply* reply = m_currentReply.data();
    reply->setProperty("tone3000_page", page);
    reply->setProperty("tone3000_append", append);
    reply->setProperty("tone3000_generation", QVariant::fromValue<qulonglong>(m_searchGeneration));
    reply->setProperty("tone3000_cache_key", cacheKey);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onSearchFinished(reply);
    });
}

void Tone3000Dialog::onSearchFinished(QNetworkReply* reply) {
    if (reply != m_currentReply.data()) return;
    m_currentReply = nullptr;
    const int page = reply->property("tone3000_page").toInt();
    const bool append = reply->property("tone3000_append").toBool();
    const quint64 generation = reply->property("tone3000_generation").toULongLong();
    const QString cacheKey = reply->property("tone3000_cache_key").toString();
    m_isLoadingPage = false;
    if (generation != m_searchGeneration) {
        reply->deleteLater();
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            m_statusLabel->setText(status == 429 ? "TONE3000 rate limit reached. Please wait and try again." : "Error connecting to TONE3000 API.");
            std::cerr << "Network Error: " << reply->errorString().toStdString() << std::endl;
        }
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        m_statusLabel->setText("TONE3000 rate limit reached. Please wait and try again.");
        return;
    }

    QJsonArray tonesArray;
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("data") && obj["data"].isArray()) {
            tonesArray = obj["data"].toArray();
            m_totalResults = obj["total"].toInt(obj["total_count"].toInt(m_totalResults));
            m_totalPages = obj["total_pages"].toInt(m_totalPages);
            if (m_totalPages == 0 && m_totalResults > 0) m_totalPages = (m_totalResults + PAGE_SIZE - 1) / PAGE_SIZE;
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

    if (m_favoritesCheckbox->isChecked() && !m_searchEdit->text().trimmed().isEmpty()) {
        const QString query = m_searchEdit->text().trimmed();
        QJsonArray filtered;
        for (const QJsonValue& value : tonesArray) {
            const QJsonObject tone = value.toObject();
            QStringList metadata{
                tone["title"].toString(),
                tone.contains("user") ? tone["user"].toObject()["username"].toString() : tone["username"].toString(),
                tone["description"].toString()
            };
            for (const QJsonValue& tag : tone["tags"].toArray()) metadata << tag.toObject()["name"].toString();
            for (const QJsonValue& make : tone["makes"].toArray()) metadata << make.toObject()["name"].toString();
            if (metadata.join(" ").contains(query, Qt::CaseInsensitive)) filtered.append(tone);
        }
        tonesArray = filtered;
    }

    if (m_favoritesCheckbox->isChecked()) {
        QFile localFile(QDir::homePath() + "/.config/PedalBoard/favorites.json");
        if (localFile.open(QFile::ReadOnly)) {
            const QJsonArray localFavorites = QJsonDocument::fromJson(localFile.readAll()).array();
            QSet<int> ids;
            for (const QJsonValue& value : tonesArray) ids.insert(value.toObject()["id"].toInt());
            m_favoriteToneIds.unite(ids);
            for (const QJsonValue& value : localFavorites) {
                const QJsonObject favorite = value.toObject();
                m_favoriteToneIds.insert(favorite["id"].toInt());
                if (!ids.contains(favorite["id"].toInt())) tonesArray.append(favorite);
            }
        }
    }

    m_pageCache.insert(cacheKey, tonesArray);
    m_pageMetadata.insert(cacheKey, QJsonObject{
        {"total", m_totalResults},
        {"totalPages", m_totalPages}
    });
    if (!append) m_currentTones = QJsonArray();
    for (const auto& v : tonesArray) m_currentTones.append(v);
    m_currentPage = page;

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

    m_hasNextPage = m_totalPages > 0 ? page < m_totalPages : tonesArray.size() == PAGE_SIZE;
    rebuildCards();
    if (!m_initialToneUrl.isEmpty()) {
        const QString requestedUrl = QUrl(m_initialToneUrl).adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery | QUrl::StripTrailingSlash).toString();
        for (int i = 0; i < m_currentTones.size(); ++i) {
            QString toneUrl = m_currentTones[i].toObject()["url"].toString();
            if (toneUrl.startsWith('/')) toneUrl.prepend("https://www.tone3000.com");
            toneUrl = QUrl(toneUrl).adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery | QUrl::StripTrailingSlash).toString();
            if (toneUrl == requestedUrl) {
                m_initialToneUrl.clear();
                selectTone(i);
                break;
            }
        }
    }
    m_pageLabel->setText(m_totalPages > 0 ? QString("Page %1 of %2").arg(m_currentPage).arg(m_totalPages) : QString("Page %1").arg(m_currentPage));
    m_statusLabel->setText(m_totalResults > 0 ? QString("Showing %1 of %2 profiles.").arg(m_currentTones.size()).arg(m_totalResults) : QString("Showing %1 profiles.").arg(m_currentTones.size()));
}

void Tone3000Dialog::onScrollChanged(int value) {
    if (m_favoritesCheckbox->isChecked() || m_isLoadingPage || !m_hasNextPage) return;
    QScrollBar* bar = m_resultsArea->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) return;
    if (value >= static_cast<int>(bar->maximum() * 0.75)) {
        requestPage(m_currentPage + 1, true);
    }
}

void Tone3000Dialog::selectTone(int row) {
    if (row < 0 || row >= m_currentTones.size()) return;
    if (row == m_selectedToneIndex) return;
    setFocus();
    m_selectedToneIndex = row;
    QJsonObject tone = m_currentTones[row].toObject();
    showToneInfo(tone);
    int toneId = tone["id"].toInt();
    m_selectedToneId = toneId;
    m_currentModels = QJsonArray();
    m_isPopulatingModels = true;
    m_modelsCombo->clear();
    m_modelsCombo->setCurrentIndex(-1);
    m_isPopulatingModels = false;
    m_loadBtn->setEnabled(false);
    m_previewBtn->setEnabled(false);

    // Enable favorites only once the selected profile's variants are known.
    m_favoriteBtn->setEnabled(false);
    QSettings settings("PedalBoard", "PedalBoard");
    const bool usingOfficialFavorites = m_favoritesCheckbox->isChecked()
        && !settings.value("tone3000_api_key", "").toString().isEmpty();
    bool isFav = usingOfficialFavorites || m_favoriteToneIds.contains(toneId) || isFavoriteLocal(toneId);
    m_favoriteBtn->setChecked(isFav);
    m_favoriteBtn->setText(isFav ? "★ Favorite" : "☆ Favorite");

    rebuildCards();

    const bool useOfficial = !settings.value("tone3000_api_key", "").toString().isEmpty();
    if (m_favoritesCheckbox->isChecked() && !useOfficial) {
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
                        m_isPopulatingModels = true;
                        m_modelsCombo->clear();
                        m_modelsCombo->setEnabled(!m_currentModels.isEmpty());
                        m_loadBtn->setEnabled(false);
                        m_previewBtn->setEnabled(false);
                        m_favoriteBtn->setEnabled(true);
                        
                        for (int j = 0; j < m_currentModels.size(); ++j) {
                            QJsonObject model = m_currentModels[j].toObject();
                            m_modelsCombo->addItem(model["name"].toString());
                        }
                        m_modelsCombo->setCurrentIndex(-1);
                        m_isPopulatingModels = false;
                        
                        if (!m_currentModels.isEmpty()) {
                            m_statusLabel->setText("Loaded local models for favorite.");
                        } else {
                            m_statusLabel->setText("No models found for this favorite.");
                        }
                        rebuildCards();
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

void Tone3000Dialog::rebuildCards() {
    while (m_resultsLayout->count() > 0) {
        QLayoutItem* item = m_resultsLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < m_currentTones.size(); ++i) appendToneCard(m_currentTones[i].toObject(), i);
    m_resultsLayout->addStretch();
}

static QString toneUsername(const QJsonObject& tone) {
    if (tone.contains("user") && tone["user"].isObject()) return tone["user"].toObject()["username"].toString();
    return tone["username"].toString();
}

static QString objectNames(const QJsonObject& tone, const QString& key) {
    QStringList names;
    if (tone.contains(key) && tone[key].isArray()) {
        for (const auto& v : tone[key].toArray()) {
            QString name = v.toObject()["name"].toString();
            if (!name.isEmpty()) names << name;
        }
    }
    return names.join(", ");
}

void Tone3000Dialog::appendToneCard(const QJsonObject& tone, int index) {
    const bool selected = index == m_selectedToneIndex;
    auto* card = new ToneCardFrame(m_resultsContent);
    card->onClicked = [this, index]() { selectTone(index); };
    card->setCursor(Qt::PointingHandCursor);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setFixedHeight(78);
    card->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 10px; } QLabel { border: none; background: transparent; }")
        .arg(selected ? "#1d2730" : "#1b1e20", selected ? "#00a3e0" : "#2d3135"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto* top = new QHBoxLayout();
    auto* title = new QLabel(tone["title"].toString(), card);
    title->setWordWrap(true);
    title->setStyleSheet("font-size: 15px; font-weight: 700; color: #f1f2f4;");
    const QString username = toneUsername(tone);
    auto* meta = new QLabel(QString("by %1").arg(username), card);
    meta->setStyleSheet("font-size: 12px; color: #a8aab0;");
    top->addWidget(title, 1);
    if (m_favoriteToneIds.contains(tone["id"].toInt())) {
        auto* favoriteBadge = new QLabel("★ Favorited", card);
        favoriteBadge->setStyleSheet("background-color: #FFD700; color: #121212; font-size: 10px; font-weight: bold; border-radius: 9px; padding: 3px 7px;");
        top->addWidget(favoriteBadge);
    }
    top->addWidget(meta);
    layout->addLayout(top);

    const int modelCount = tone["models_count"].toInt(tone["model_count"].toInt());
    auto* facts = new QLabel(QString("%1  •  %2 downloads  •  %3 favorites  •  %4 model%5")
        .arg(tone["gear"].toString("Unknown gear"))
        .arg(QString::number(tone["downloads_count"].toInt()))
        .arg(QString::number(tone["favorites_count"].toInt()))
        .arg(QString::number(modelCount))
        .arg(modelCount == 1 ? "" : "s"), card);
    facts->setStyleSheet("font-size: 12px; color: #b7bbc0;");
    layout->addWidget(facts);

    m_resultsLayout->addWidget(card);
}

void Tone3000Dialog::showToneInfo(const QJsonObject& tone) {
    if (!m_infoArea) return;
    m_infoTitleLabel->setText(tone["title"].toString());
    const QJsonObject user = tone["user"].toObject();
    QString profileUrl = user["url"].toString().isEmpty() ? "https://www.tone3000.com/" + toneUsername(tone) : user["url"].toString();
    if (profileUrl.startsWith('/')) profileUrl.prepend("https://www.tone3000.com");
    m_infoCreatorLabel->setText(QString("by <a href='%1'>%2</a>").arg(profileUrl.toHtmlEscaped(), toneUsername(tone).toHtmlEscaped()));
    m_infoCreatorLabel->setOpenExternalLinks(true);
    const QString description = tone["description"].toString().toHtmlEscaped().replace("\n", "<br>");
    QString toneUrl = tone["url"].toString();
    if (toneUrl.startsWith('/')) toneUrl.prepend("https://www.tone3000.com");
    QStringList tagPills;
    for (const QJsonValue& value : tone["tags"].toArray()) {
        const QString tag = value.toObject()["name"].toString().toHtmlEscaped();
        if (!tag.isEmpty()) tagPills << QString("<span style='background:#1f3440; color:#80d8ff; padding:3px 6px; border-radius:8px;'>%1</span>").arg(tag);
    }
    const QString tags = tagPills.isEmpty() ? "None" : tagPills.join(" ");
    m_infoText->setHtml(QString("<p><b>Gear:</b> %1</p>"
                              "<p><b>Makes:</b> %3</p>"
                              "<p><b>Tags:</b> %4</p>"
                              "<hr><p>%5</p>%6")
        .arg(tone["gear"].toString().toHtmlEscaped(),
             objectNames(tone, "makes").toHtmlEscaped(),
             tags,
              description.isEmpty() ? "No description provided." : description,
              toneUrl.isEmpty() ? QString() : QString("<p><a href='%1'>Open on TONE3000</a></p>").arg(toneUrl.toHtmlEscaped())));
    m_infoText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (m_infoArea->isVisible() && m_infoText->viewport()->width() > 50) {
        m_infoText->document()->setTextWidth(m_infoText->viewport()->width());
        m_infoText->setFixedHeight(static_cast<int>(m_infoText->document()->size().height()) + 8);
    }

    const QSignalBlocker blocker(m_variantsList);
    m_variantsList->clear();
    if (tone["id"].toInt() != m_selectedToneId) {
        m_variantsLabel->setText("Capture variants");
        m_variantsList->setEnabled(false);
        m_variantsList->setFixedHeight(32);
        return;
    }
    m_variantsLabel->setText(QString("Capture variants (%1)").arg(m_currentModels.size()));
    if (m_currentModels.isEmpty()) {
        m_variantsList->addItem("Loading capture variants...");
        m_variantsList->setEnabled(false);
        m_variantsList->setFixedHeight(32);
        return;
    }
    for (const QJsonValue& value : m_currentModels) {
        const QJsonObject model = value.toObject();
        QString details = model["size"].toString();
        const QString architecture = model["architecture_version"].toString();
        if (!architecture.isEmpty()) details += QString(details.isEmpty() ? "" : " · ") + "A" + architecture;
        const QString label = model["name"].toString() + (details.isEmpty() ? "" : "  (" + details + ")");
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, label);
        item->setToolTip("Click to preview this capture");
        m_variantsList->addItem(item);
    }
    m_variantsList->setEnabled(true);
    const int visibleVariantRows = std::min(m_variantsList->count(), 40);
    m_variantsList->setVerticalScrollBarPolicy(m_variantsList->count() > 40
        ? Qt::ScrollBarAsNeeded
        : Qt::ScrollBarAlwaysOff);
    int variantsHeight = m_variantsList->frameWidth() * 2;
    for (int row = 0; row < visibleVariantRows; ++row) {
        variantsHeight += m_variantsList->sizeHintForRow(row);
    }
    m_variantsList->setFixedHeight(variantsHeight);
    m_variantsList->setCurrentRow(-1);
}

void Tone3000Dialog::onInfoLinkClicked(const QUrl& url) {
    QDesktopServices::openUrl(url);
}

void Tone3000Dialog::updateActiveFilterChips() {
    while (m_filterChipsLayout->count() > 0) {
        QLayoutItem* item = m_filterChipsLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    auto addChip = [this](const QString& text, std::function<void()> clearFn) {
        auto* chip = new QPushButton(text + "  ×", m_filterChipsWidget);
        chip->setStyleSheet("QPushButton { background-color: #252a2e; color: #d9dde1; border: 1px solid #3b4248; border-radius: 12px; padding: 4px 9px; font-size: 11px; } QPushButton:hover { border-color: #00a3e0; }");
        connect(chip, &QPushButton::clicked, this, [clearFn]() { clearFn(); });
        m_filterChipsLayout->addWidget(chip);
    };
    if (m_gearFilterCombo->currentData().toString() != "amp-cab") addChip("Gear: " + m_gearFilterCombo->currentText(), [this]() { m_gearFilterCombo->setCurrentIndex(0); });
    if (!m_characterFilterCombo->currentData().toString().isEmpty()) addChip("Character: " + m_characterFilterCombo->currentText(), [this]() { m_characterFilterCombo->setCurrentIndex(0); });
    if (m_archFilterCombo->currentData().toString() != "2") addChip("Architecture: " + m_archFilterCombo->currentText(), [this]() { m_archFilterCombo->setCurrentIndex(0); });
    if (!m_sizeFilterCombo->currentData().toString().isEmpty()) addChip("Size: " + m_sizeFilterCombo->currentText(), [this]() { m_sizeFilterCombo->setCurrentIndex(0); });
    if (m_calibratedCheckbox->isChecked()) addChip("Calibrated", [this]() { m_calibratedCheckbox->setChecked(false); });
    if (m_sortCombo->currentData().toString() != "trending") addChip("Sort: " + m_sortCombo->currentText(), [this]() { m_sortCombo->setCurrentIndex(0); });
    if (m_favoritesCheckbox->isChecked()) addChip("Favorites", [this]() { m_favoritesCheckbox->setChecked(false); });
    m_filterChipsLayout->addStretch();
    m_filterChipsWidget->setVisible(m_filterChipsLayout->count() > 1);
}

void Tone3000Dialog::fetchModelsForTone(int toneId) {
    if (m_modelsReply) {
        QNetworkReply* reply = m_modelsReply.data();
        m_modelsReply = nullptr;
        reply->abort();
        reply->deleteLater();
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
        QString arch = m_archFilterCombo->currentData().toString();
        if (!arch.isEmpty()) q.addQueryItem("architecture", arch);
        url.setQuery(q);
        
        QNetworkRequest request;
        request.setUrl(url);
        request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
        
        m_modelsReply = m_networkManager->get(request);
    }

    QNetworkReply* reply = m_modelsReply.data();
    connect(reply, &QNetworkReply::finished, this, [this, reply, toneId]() {
        onModelsFinished(reply, toneId);
    });
}

void Tone3000Dialog::onModelsFinished(QNetworkReply* reply, int toneId) {
    if (reply != m_modelsReply.data()) return;
    m_modelsReply = nullptr;
    
    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText("Failed to load models.");
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

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
    if (toneId != m_selectedToneId) return;
    populateModels(modelsArray);
}

void Tone3000Dialog::populateModels(const QJsonArray& models) {
    m_currentModels = models;
    m_isPopulatingModels = true;
    m_modelsCombo->clear();
    if (m_currentModels.isEmpty()) {
        m_isPopulatingModels = false;
        m_statusLabel->setText("No downloadable NAM models found for this profile.");
        rebuildCards();
        return;
    }

    for (const auto& value : m_currentModels) {
        m_modelsCombo->addItem(value.toObject()["name"].toString());
    }
    m_modelsCombo->setCurrentIndex(-1);
    m_isPopulatingModels = false;

    m_modelsCombo->setEnabled(true);
    m_loadBtn->setEnabled(false);
    m_previewBtn->setEnabled(false);
    m_favoriteBtn->setEnabled(true);
    m_statusLabel->setText("Select capture variant to preview or load.");
    if (m_selectedToneIndex >= 0 && m_selectedToneIndex < m_currentTones.size()) {
        showToneInfo(m_currentTones[m_selectedToneIndex].toObject());
    }
    rebuildCards();
    if (m_pendingAction != PendingAction::None) {
        const PendingAction action = m_pendingAction;
        m_pendingAction = PendingAction::None;
        QTimer::singleShot(0, this, [this, action]() {
            if (action == PendingAction::Preview) onPreviewClicked();
            else onDownloadClicked();
        });
    }
}

void Tone3000Dialog::onDownloadClicked() {
    int idx = m_modelsCombo->currentIndex();
    if (idx < 0 || idx >= m_currentModels.size()) return;

    QJsonObject model = m_currentModels[idx].toObject();
    QString url = model["model_url"].toString();
    QString name = model["name"].toString();
    m_downloadedToneName = name;
    QString toneFolder = "tone_models";

    m_downloadedMetadata = AudioNode::ModelMetadata{};
    m_downloadedVariants.clear();
    if (const int row = m_selectedToneIndex; row >= 0 && row < m_currentTones.size()) {
        const QJsonObject tone = m_currentTones[row].toObject();
        const QString title = tone["title"].toString();
        const int toneId = tone["id"].toInt();
        if (!title.isEmpty() && title != name) m_downloadedToneName = title + " - " + name;
        m_downloadedToneUrl = tone["url"].toString();
        
        QString slug = tone["slug"].toString();
        if (slug.isEmpty()) {
            slug = title.toLower();
            slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
            slug.remove(QRegularExpression("^-|-$"));
        }
        if (m_downloadedToneUrl.isEmpty()) {
            m_downloadedToneUrl = "https://www.tone3000.com/tones/" + (slug.isEmpty() ? QString::number(toneId) : slug);
        }

        QString safeSlug = slug;
        safeSlug.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
        if (safeSlug.isEmpty()) safeSlug = "profile";
        toneFolder = QString("tone_%1_%2").arg(toneId > 0 ? QString::number(toneId) : "0").arg(safeSlug);

        m_downloadedMetadata.toneId = toneId > 0 ? QString::number(toneId).toStdString() : "";
        m_downloadedMetadata.toneTitle = title.toStdString();
        m_downloadedMetadata.toneSlug = slug.toStdString();

        QString username;
        if (tone.contains("user") && tone["user"].isObject()) {
            username = tone["user"].toObject()["username"].toString();
        } else if (tone.contains("username")) {
            username = tone["username"].toString();
        }
        m_downloadedMetadata.author = username.toStdString();
        m_downloadedMetadata.gearType = tone["gear"].toString().toStdString();

        QStringList tagList;
        if (tone.contains("tags") && tone["tags"].isArray()) {
            for (const auto& val : tone["tags"].toArray()) {
                tagList << val.toObject()["name"].toString();
            }
        }
        m_downloadedMetadata.tags = tagList.join(", ").toStdString();
        m_downloadedMetadata.description = tone["description"].toString().toStdString();
    }
    
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Load Error", "Selected model has no download URL.");
        return;
    }

    QString safeName = name.replace(" ", "_").replace("/", "_").replace("\\", "_");
    if (!safeName.endsWith(".nam", Qt::CaseInsensitive)) {
        safeName += ".nam";
    }

    QString cacheDir = QDir::homePath() + "/.cache/PedalBoard/tone3000/" + toneFolder;
    QDir().mkpath(cacheDir);
    m_downloadedModelPath = (cacheDir + "/" + safeName).toStdString();

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
    if (m_downloadReply) {
        QNetworkReply* reply = m_downloadReply.data();
        m_downloadReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    m_previewDownload = isPreview;

    if (m_downloadedModelPath.empty()) {
        QString cacheDir = QDir::homePath() + "/.cache/PedalBoard/tone3000/tone_preview";
        QDir().mkpath(cacheDir);
        m_downloadedModelPath = (cacheDir + "/" + filename).toStdString();
    }

    m_statusLabel->setText(isPreview ? "Downloading preview profile..." : "Downloading profile...");
    m_progressBar->setVisible(!isPreview);
    if (!isPreview) m_progressBar->setValue(0);
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
    
    m_downloadReply = m_networkManager->get(request);

    QNetworkReply* reply = m_downloadReply.data();
    connect(reply, &QNetworkReply::downloadProgress, this, &Tone3000Dialog::onDownloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onDownloadFinished(reply);
    });
}

void Tone3000Dialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(percent);
        m_statusLabel->setText(QString("Downloading: %1%").arg(percent));
    }
}

void Tone3000Dialog::onDownloadFinished(QNetworkReply* reply) {
    if (reply != m_downloadReply.data()) return;
    m_downloadReply = nullptr;

    // Check for redirection manually to strip auth headers on redirect target
    QVariant redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectUrl.isValid()) {
        QUrl nextUrl = redirectUrl.toUrl();
        if (nextUrl.isRelative()) {
            nextUrl = reply->url().resolved(nextUrl);
        }
        QString fname = QFileInfo(QString::fromStdString(m_downloadedModelPath)).fileName();
        bool isPrev = m_previewDownload;
        reply->deleteLater();
        downloadModelFile(nextUrl.toString(), fname, isPrev, true);
        return;
    }

    m_progressBar->setVisible(false);
    m_loadBtn->setEnabled(true);
    m_previewBtn->setEnabled(true);

    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText("Download failed.");
        QMessageBox::critical(this, "Download Error", QString("Failed to download model file:\n%1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray fileData = reply->readAll();
    reply->deleteLater();

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
            QString currentCacheDir = QFileInfo(QString::fromStdString(m_downloadedModelPath)).absoluteDir().absolutePath();
            std::vector<AudioNode::ModelVariant> vars;
            for (int i = 0; i < m_currentModels.size(); ++i) {
                QJsonObject model = m_currentModels[i].toObject();
                AudioNode::ModelVariant var;
                var.name = model["name"].toString().toStdString();
                var.url = model["model_url"].toString().toStdString();
                
                if (i == m_modelsCombo->currentIndex()) {
                    var.localPath = m_downloadedModelPath;
                } else {
                    // Check if already cached in currentCacheDir
                    QString itemSafeName = model["name"].toString();
                    itemSafeName.replace(QRegularExpression("[^a-zA-Z0-9_\\-.]"), "_");
                    if (!itemSafeName.endsWith(".nam", Qt::CaseInsensitive) && !itemSafeName.endsWith(".json", Qt::CaseInsensitive)) {
                        itemSafeName += ".nam";
                    }
                    QString expectedPath = currentCacheDir + "/" + itemSafeName;
                    if (QFile::exists(expectedPath)) {
                        var.localPath = expectedPath.toStdString();
                    }
                }
                vars.push_back(var);
            }
            m_downloadedVariants = vars;
            m_node->setModelVariants(vars);
        }
        m_statusLabel->setText("Download successful!");
        saveFilterSettings();
        accept();
    }
}

void Tone3000Dialog::reject() {
    if (m_isPreviewing && m_node && m_engine) {
        m_engine->suspendProcessing();
        m_node->loadModelFile(m_originalModelPath);
        m_engine->resumeProcessing();
    }
    saveFilterSettings();
    QDialog::reject();
}

void Tone3000Dialog::onFavoritesToggled(bool checked) {
    m_sortCombo->setEnabled(!checked);
    m_gearFilterCombo->setEnabled(!checked);
    m_characterFilterCombo->setEnabled(!checked);
    m_archFilterCombo->setEnabled(!checked);
    m_sizeFilterCombo->setEnabled(!checked);
    m_calibratedCheckbox->setEnabled(!checked);
    performSearch();
}

void Tone3000Dialog::onFavoriteButtonClicked() {
    int row = m_selectedToneIndex;
    if (row < 0) return;
    int toneId = m_selectedToneId;
    QJsonObject selectedTone;
    for (const QJsonValue& value : m_currentTones) {
        if (value.toObject()["id"].toInt() == toneId) {
            selectedTone = value.toObject();
            break;
        }
    }

    QSettings settings("PedalBoard", "PedalBoard");
    const QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    if (!savedKeyEnc.isEmpty()) {
        const bool favorite = m_favoriteBtn->isChecked();
        QNetworkRequest request(QUrl(QString("https://www.tone3000.com/api/v1/tones/%1/favorite").arg(toneId)));
        request.setRawHeader("Authorization", ("Bearer " + deobfuscateKey(savedKeyEnc)).toUtf8());
        QNetworkReply* reply = favorite ? m_networkManager->put(request, QByteArray()) : m_networkManager->deleteResource(request);
        const QJsonArray selectedModels = m_currentModels;
        connect(reply, &QNetworkReply::finished, this, [this, reply, favorite, toneId, selectedTone, selectedModels]() {
            const bool succeeded = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!succeeded) {
                m_favoriteBtn->setChecked(!favorite);
                m_statusLabel->setText("Could not update TONE3000 favorite.");
            } else {
                if (favorite) m_favoriteToneIds.insert(toneId);
                else m_favoriteToneIds.remove(toneId);
                if (favorite && !selectedTone.isEmpty()) saveFavoriteLocal(selectedTone, selectedModels);
                if (!favorite) removeFavoriteLocal(toneId);
                m_statusLabel->setText(favorite ? "Added to TONE3000 favorites." : "Removed from TONE3000 favorites.");
                m_pageCache.clear();
                m_pageMetadata.clear();
                if (m_favoritesCheckbox->isChecked()) {
                    performSearch();
                    return;
                }
            }
            rebuildCards();
        });
        return;
    }
    
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
    rebuildCards();
}

void Tone3000Dialog::loadFavoritesLocal() {
    m_selectedToneIndex = -1;
    m_selectedToneId = -1;
    
    QString filePath = QDir::homePath() + "/.config/PedalBoard/favorites.json";
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly)) {
        m_statusLabel->setText("No favorites saved yet.");
        m_currentTones = QJsonArray();
        rebuildCards();
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (!doc.isArray()) {
        m_statusLabel->setText("Favorites file corrupted.");
        m_currentTones = QJsonArray();
        rebuildCards();
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
            QString description = fav["description"].toString();
            QStringList metadata;
            for (const QJsonValue& value : fav["tags"].toArray()) metadata << value.toObject()["name"].toString();
            for (const QJsonValue& value : fav["makes"].toArray()) metadata << value.toObject()["name"].toString();
            const QString searchable = QStringList{title, username, description, metadata.join(" ")}.join(" ");
            if (!searchable.contains(queryText, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filteredArray.append(fav);
    }
    
    m_currentTones = filteredArray;
    rebuildCards();
    updateActiveFilterChips();
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
    settings["gearValue"] = m_gearFilterCombo->currentData().toString();
    settings["characterValue"] = m_characterFilterCombo->currentData().toString();
    settings["architectureValue"] = m_archFilterCombo->currentData().toString();
    settings["sizeValue"] = m_sizeFilterCombo->currentData().toString();
    settings["calibrated"] = m_calibratedCheckbox->isChecked();
    settings["favorites_only"] = m_favoritesCheckbox->isChecked();
    settings["windowGeometry"] = QString::fromLatin1(saveGeometry().toBase64());
    settings["infoVisible"] = m_infoToggleBtn->isChecked();
    QJsonArray splitterSizes;
    for (const int size : m_contentSplitter->sizes()) splitterSizes.append(size);
    settings["splitterSizes"] = splitterSizes;
    
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
        m_characterFilterCombo->blockSignals(true);
        m_archFilterCombo->blockSignals(true);
        m_sizeFilterCombo->blockSignals(true);
        m_calibratedCheckbox->blockSignals(true);
        m_favoritesCheckbox->blockSignals(true);
        
        if (settings.contains("search")) m_searchEdit->setText(settings["search"].toString());
        if (settings.contains("sort")) m_sortCombo->setCurrentIndex(settings["sort"].toInt());
        if (settings.contains("gearValue")) {
            const int index = m_gearFilterCombo->findData(settings["gearValue"].toString());
            if (index >= 0) m_gearFilterCombo->setCurrentIndex(index);
        } else if (settings.contains("gear")) {
            // The previous control ordered All gear before Amp + Cab.
            const int legacyIndex = settings["gear"].toInt();
            m_gearFilterCombo->setCurrentIndex(legacyIndex == 0 ? 1 : legacyIndex == 1 ? 0 : legacyIndex);
        }
        if (settings.contains("characterValue")) {
            const int index = m_characterFilterCombo->findData(settings["characterValue"].toString());
            if (index >= 0) m_characterFilterCombo->setCurrentIndex(index);
        }
        if (settings.contains("architectureValue")) {
            const int index = m_archFilterCombo->findData(settings["architectureValue"].toString());
            if (index >= 0) m_archFilterCombo->setCurrentIndex(index);
        } else if (settings.contains("arch")) {
            // The previous control ordered A1/A2 before A2.
            const int legacyIndex = settings["arch"].toInt();
            m_archFilterCombo->setCurrentIndex(legacyIndex == 0 ? 1 : legacyIndex == 1 ? 0 : legacyIndex);
        }
        if (settings.contains("sizeValue")) {
            const int index = m_sizeFilterCombo->findData(settings["sizeValue"].toString());
            if (index >= 0) m_sizeFilterCombo->setCurrentIndex(index);
        }
        if (settings.contains("calibrated")) m_calibratedCheckbox->setChecked(settings["calibrated"].toBool());
        if (settings.contains("favorites_only")) {
            bool favs = settings["favorites_only"].toBool();
            m_favoritesCheckbox->setChecked(favs);
            m_sortCombo->setEnabled(!favs);
            m_gearFilterCombo->setEnabled(!favs);
            m_characterFilterCombo->setEnabled(!favs);
            m_archFilterCombo->setEnabled(!favs);
            m_sizeFilterCombo->setEnabled(!favs);
            m_calibratedCheckbox->setEnabled(!favs);
        }

        const QByteArray geometry = QByteArray::fromBase64(settings["windowGeometry"].toString().toLatin1());
        if (!geometry.isEmpty()) restoreGeometry(geometry);
        m_infoToggleBtn->setChecked(settings["infoVisible"].toBool(false));
        if (settings["splitterSizes"].isArray()) {
            QList<int> splitterSizes;
            for (const QJsonValue& value : settings["splitterSizes"].toArray()) splitterSizes.append(value.toInt());
            if (splitterSizes.size() == m_contentSplitter->count()) m_contentSplitter->setSizes(splitterSizes);
        }
        
        m_searchEdit->blockSignals(false);
        m_sortCombo->blockSignals(false);
        m_gearFilterCombo->blockSignals(false);
        m_characterFilterCombo->blockSignals(false);
        m_archFilterCombo->blockSignals(false);
        m_sizeFilterCombo->blockSignals(false);
        m_calibratedCheckbox->blockSignals(false);
        m_favoritesCheckbox->blockSignals(false);
        updateActiveFilterChips();
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
