#include "MainWindow.h"
#include "../audio/LV2Host.h"
#include "../audio/VST3Host.h"
#include "../audio/CLAPHost.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "../audio/BypassNode.h"
#include "NodeWidget.h"
#include "Tone3000Dialog.h"
#include <filesystem>
#include <iostream>
#include "PortWidget.h"
#include <QSplitter>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMenu>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QPixmap>
#include <QFileInfo>
#include <suil/suil.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QGroupBox>
#include <QScrollBar>
#include <QScrollArea>
#include <QStyle>
#include <QMessageBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QTimer>
#include <QSizePolicy>
#include <QGuiApplication>
#include <QWindow>
#include <QScreen>
#include <QResizeEvent>
#include <QLibrary>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QDataStream>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <lv2/ui/ui.h>
#include <lv2/options/options.h>
#include <lv2/atom/atom.h>
#include <climits>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <QPainter>
#include <QDial>
#include <cmath>
#include <limits>
#include <tuple>

class ModernKnob : public QDial {
public:
    explicit ModernKnob(QWidget* parent = nullptr) : QDial(parent) {
        setMinimumSize(32, 32);
        setMaximumSize(32, 32);
        setNotchesVisible(false);
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        int side = qMin(width(), height());
        QRectF rect((width() - side) / 2.0 + 3, (height() - side) / 2.0 + 3, side - 6, side - 6);

        // Draw background track circle
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#252525"));
        painter.drawEllipse(rect);

        // Calculate value ratio
        double valueRatio = static_cast<double>(value() - minimum()) / (maximum() - minimum());
        if (valueRatio < 0.0) valueRatio = 0.0;
        if (valueRatio > 1.0) valueRatio = 1.0;

        // Angle math: bottom-left (225 degrees) to bottom-right (315 degrees is -45 degrees)
        double startAngle = 225.0;
        double spanAngle = -270.0 * valueRatio;

        // Draw active value arc (light blue #00B0FF)
        QPen arcPen(QColor("#00B0FF"), 3);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(rect.adjusted(1.5, 1.5, -1.5, -1.5), startAngle * 16, spanAngle * 16);

        // Draw inner cap
        QRectF innerRect = rect.adjusted(4, 4, -4, -4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#333333"));
        painter.drawEllipse(innerRect);

        // Draw indicator dot
        const double PI = 3.14159265358979323846;
        double angleRad = (startAngle + spanAngle) * PI / 180.0;
        
        QPointF center = innerRect.center();
        double innerR = innerRect.width() / 2.0;
        QPointF dotPos(center.x() + (innerR - 2.5) * cos(angleRad), center.y() - (innerR - 2.5) * sin(angleRad));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#FFFFFF"));
        painter.drawEllipse(dotPos, 1.5, 1.5);
    }
};

class ResetGainSlider final : public QSlider {
public:
    explicit ResetGainSlider(QWidget* parent = nullptr) : QSlider(Qt::Horizontal, parent) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        setValue(0);
        event->accept();
    }
};

class ResetGainSpinBox final : public QDoubleSpinBox {
public:
    explicit ResetGainSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        setValue(0.0);
        selectAll();
        event->accept();
    }
};

class GainPopover final : public QFrame {
public:
    GainPopover(const QString& title, float value, std::function<void(float)> changed,
                std::function<void()> committed, QWidget* parent)
        : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint), m_changed(std::move(changed)) {
        setObjectName("gainPopover");
        setFixedWidth(330);
        setStyleSheet(
            "QFrame#gainPopover { background: #2b2b31; border: 1px solid #41414a; border-radius: 10px; }"
            "QLabel { background: transparent; border: none; }"
            "QDoubleSpinBox { background: #202025; border: 1px solid #464650; border-radius: 4px; padding: 4px; }"
            "QProgressBar { background: #17171b; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00d084, stop:0.8 #ffd54a, stop:1 #ff5252); }"
        );

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(7);
        auto* heading = new QHBoxLayout();
        auto* label = new QLabel(title, this);
        label->setStyleSheet("font-weight: bold; color: #f0f0f3;");
        m_spin = new ResetGainSpinBox(this);
        m_spin->setRange(-40.0, 24.0);
        m_spin->setDecimals(1);
        m_spin->setSingleStep(0.1);
        m_spin->setSuffix(" dB");
        m_spin->setValue(value);
        heading->addWidget(label);
        heading->addStretch();
        heading->addWidget(m_spin);
        layout->addLayout(heading);

        m_slider = new ResetGainSlider(this);
        m_slider->setRange(-400, 240);
        m_slider->setSingleStep(1);
        m_slider->setPageStep(10);
        m_slider->setValue(qRound(value * 10.0f));
        m_slider->setToolTip("Drag or use the mouse wheel. Double-click to reset to 0 dB.");
        layout->addWidget(m_slider);

        auto* scale = new QHBoxLayout();
        auto* minimum = new QLabel("-40 dB", this);
        auto* resetHint = new QLabel("Double-click: 0 dB", this);
        auto* maximum = new QLabel("+24 dB", this);
        for (auto* item : {minimum, resetHint, maximum}) item->setStyleSheet("color: #92929c; font-size: 10px;");
        scale->addWidget(minimum);
        scale->addStretch();
        scale->addWidget(resetHint);
        scale->addStretch();
        scale->addWidget(maximum);
        layout->addLayout(scale);

        m_meter = new QProgressBar(this);
        m_meter->setRange(0, 100);
        m_meter->setTextVisible(false);
        m_meter->setFixedHeight(7);
        m_meter->setToolTip("Signal level (-60 to 0 dBFS)");
        layout->addWidget(m_meter);

        auto* commitTimer = new QTimer(this);
        commitTimer->setSingleShot(true);
        commitTimer->setInterval(350);
        connect(commitTimer, &QTimer::timeout, this, committed);

        connect(m_slider, &QSlider::valueChanged, this, [this, commitTimer](int sliderValue) {
            const float db = sliderValue / 10.0f;
            const QSignalBlocker blocker(m_spin);
            m_spin->setValue(db);
            m_changed(db);
            commitTimer->start();
        });
        connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this, commitTimer](double spinValue) {
            const QSignalBlocker blocker(m_slider);
            m_slider->setValue(qRound(spinValue * 10.0));
            m_changed(static_cast<float>(spinValue));
            commitTimer->start();
        });
        connect(m_slider, &QSlider::sliderReleased, this, committed);
        connect(m_spin, &QDoubleSpinBox::editingFinished, this, committed);
    }

    QProgressBar* meter() const { return m_meter; }

    void showBelow(QWidget* anchor) {
        adjustSize();
        QPoint position = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 7));
        if (QScreen* screen = anchor->screen()) {
            const QRect available = screen->availableGeometry();
            position.setX(std::clamp(position.x(), available.left(), available.right() - width()));
            if (position.y() + height() > available.bottom()) {
                position.setY(anchor->mapToGlobal(QPoint(0, -height() - 7)).y());
            }
        }
        move(position);
        show();
        raise();
    }

private:
    std::function<void(float)> m_changed;
    ResetGainSlider* m_slider = nullptr;
    ResetGainSpinBox* m_spin = nullptr;
    QProgressBar* m_meter = nullptr;
};

namespace {
struct PickerPluginInfo {
    QString name;
    QString uri;
    QString category;
    QString brand;
    QString thumbnailPath;
    QString format;
    QString searchable;
};

static QString pluginCategoryGlyph(const QString& category) {
    if (category == "Amplifiers") return "AMP";
    if (category == "Delays") return "DLY";
    if (category == "Reverbs") return "RVB";
    if (category == "Distortions") return "DST";
    if (category == "Dynamics") return "DYN";
    if (category == "EQ & Filters") return "EQ";
    if (category == "Modulations") return "MOD";
    if (category == "VST3 Plugins") return "VST";
    return "FX";
}

class PluginPickerDialog final : public QDialog {
public:
    PluginPickerDialog(
        const std::vector<PickerPluginInfo>& plugins,
        QSet<QString>* favorites,
        std::function<void()> favoritesChanged,
        QWidget* parent = nullptr)
        : QDialog(parent), m_plugins(plugins), m_favorites(favorites), m_favoritesChanged(std::move(favoritesChanged)) {
        setWindowTitle("Add Plugin");
        setModal(true);
        resize(620, 540);
        setStyleSheet(
            "QDialog { background: #1b1b1f; color: #e9e9ee; }"
            "QLineEdit, QComboBox, QListWidget { background: #242429; border: 1px solid #393941; border-radius: 5px; color: #ececf0; padding: 7px; }"
            "QListWidget::item { border: none; padding: 0; }"
            "QListWidget::item:selected { background: transparent; }"
            "QToolButton { border: none; color: #ffc857; font-size: 18px; padding: 5px; }"
            "QPushButton { background: #00a8e8; border: none; border-radius: 5px; color: white; font-weight: bold; padding: 7px 14px; }"
            "QPushButton:hover { background: #27b9f0; }");

        // Pre-load and scale all thumbnails to cache
        for (const auto& plugin : m_plugins) {
            if (!plugin.thumbnailPath.isEmpty() && QFileInfo::exists(plugin.thumbnailPath)) {
                QPixmap thumbnail(plugin.thumbnailPath);
                m_thumbnailCache.insert(plugin.thumbnailPath, thumbnail.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(10);

        auto* filterRow = new QHBoxLayout();
        m_search = new QLineEdit(this);
        m_search->setPlaceholderText("Search plugins by name, category, brand, or URI...");
        m_category = new QComboBox(this);
        m_category->setMinimumWidth(155);
        m_resultCount = new QLabel(this);
        m_resultCount->setStyleSheet("color: #9d9da8; font-size: 11px;");
        filterRow->addWidget(m_search, 1);
        filterRow->addWidget(m_category);
        filterRow->addWidget(m_resultCount);
        layout->addLayout(filterRow);

        m_list = new QListWidget(this);
        m_list->setSpacing(5);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(m_list, 1);

        auto* actions = new QHBoxLayout();
        auto* hint = new QLabel("Enter or double-click to add. Star plugins for quick access.", this);
        hint->setStyleSheet("color: #9898a2; font-size: 11px;");
        auto* cancel = new QPushButton("Cancel", this);
        cancel->setStyleSheet("QPushButton { background: #35353c; } QPushButton:hover { background: #464650; }");
        auto* add = new QPushButton("Add Plugin", this);
        actions->addWidget(hint, 1);
        actions->addWidget(cancel);
        actions->addWidget(add);
        layout->addLayout(actions);

        QSet<QString> categories;
        for (const auto& plugin : m_plugins) categories.insert(plugin.category);
        m_category->addItem("All Categories");
        m_category->addItem("Favorites");
        QStringList categoryList = categories.values();
        categoryList.sort();
        m_category->addItems(categoryList);

        m_searchTimer = new QTimer(this);
        m_searchTimer->setSingleShot(true);
        connect(m_searchTimer, &QTimer::timeout, this, [this] { refreshResults(); });

        connect(m_search, &QLineEdit::textChanged, this, [this] { m_searchTimer->start(120); });
        connect(m_search, &QLineEdit::returnPressed, this, [this] { acceptSelection(); });
        connect(m_category, &QComboBox::currentTextChanged, this, [this] { refreshResults(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(add, &QPushButton::clicked, this, [this] { acceptSelection(); });
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { acceptSelection(); });
        refreshResults();
        m_search->setFocus();
    }

    QString selectedUri() const { return m_selectedUri; }

private:
    void refreshResults() {
        const QString query = m_search->text().trimmed().toLower();
        const QString category = m_category->currentText();
        std::vector<PickerPluginInfo> results;
        for (const auto& plugin : m_plugins) {
            const bool favorite = m_favorites->contains(plugin.uri);
            if (category == "Favorites" && !favorite) continue;
            if (category != "All Categories" && category != "Favorites" && plugin.category != category) continue;
            if (!query.isEmpty() && !plugin.searchable.contains(query)) continue;
            results.push_back(plugin);
        }
        std::sort(results.begin(), results.end(), [this](const PickerPluginInfo& left, const PickerPluginInfo& right) {
            const bool leftFavorite = m_favorites->contains(left.uri);
            const bool rightFavorite = m_favorites->contains(right.uri);
            if (leftFavorite != rightFavorite) return leftFavorite;
            return QString::localeAwareCompare(left.name, right.name) < 0;
        });

        m_list->clear();
        m_resultCount->setText(QString::number(results.size()) + " plugins");
        for (const auto& plugin : results) {
            const bool hasThumbnail = !plugin.thumbnailPath.isEmpty() && m_thumbnailCache.contains(plugin.thumbnailPath);
            auto* item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, plugin.uri);
            item->setSizeHint(QSize(0, hasThumbnail ? 54 : 40));
            auto* row = new QWidget(m_list);
            row->setStyleSheet("QWidget { background: #242429; border: 1px solid #303038; border-radius: 5px; } QWidget:hover { background: #2c2c33; border-color: #00a8e8; }");
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(6, 4, 6, 4);
            rowLayout->setSpacing(7);

            auto* visual = new QLabel(row);
            visual->setFixedSize(hasThumbnail ? 42 : 28, hasThumbnail ? 42 : 28);
            visual->setAlignment(Qt::AlignCenter);
            if (hasThumbnail) {
                visual->setPixmap(m_thumbnailCache.value(plugin.thumbnailPath));
            } else {
                visual->setText(pluginCategoryGlyph(plugin.category));
                visual->setStyleSheet("background: #123348; color: #38c5ff; border-radius: 4px; font-weight: bold; font-size: 9px;");
            }
            rowLayout->addWidget(visual);

            auto* name = new QLabel(plugin.name, row);
            name->setStyleSheet("font-weight: bold; color: #f3f3f6; border: none;");
            rowLayout->addWidget(name, 1);
            const QString metadata = plugin.brand.isEmpty()
                ? plugin.category + " · " + plugin.format
                : plugin.brand + " · " + plugin.category + " · " + plugin.format;
            auto* metadataLabel = new QLabel(metadata, row);
            metadataLabel->setStyleSheet("font-size: 10px; color: #9d9da8; border: none;");
            rowLayout->addWidget(metadataLabel);

            auto* favorite = new QToolButton(row);
            favorite->setText(m_favorites->contains(plugin.uri) ? "★" : "☆");
            favorite->setToolTip("Toggle favorite");
            favorite->setFixedSize(28, 28);
            connect(favorite, &QToolButton::clicked, this, [this, plugin] {
                if (m_favorites->contains(plugin.uri)) m_favorites->remove(plugin.uri);
                else m_favorites->insert(plugin.uri);
                m_favoritesChanged();
                refreshResults();
            });
            rowLayout->addWidget(favorite);
            m_list->setItemWidget(item, row);
        }
        if (m_list->count()) m_list->setCurrentRow(0);
    }

    void acceptSelection() {
        if (auto* item = m_list->currentItem()) {
            m_selectedUri = item->data(Qt::UserRole).toString();
            accept();
        }
    }

    std::vector<PickerPluginInfo> m_plugins;
    QSet<QString>* m_favorites;
    std::function<void()> m_favoritesChanged;
    QLineEdit* m_search = nullptr;
    QComboBox* m_category = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_resultCount = nullptr;
    QTimer* m_searchTimer = nullptr;
    QHash<QString, QPixmap> m_thumbnailCache;
    QString m_selectedUri;
};
}

#include <QSettings>

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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("PedalBoard - Guitar Multieffects host");
    resize(1200, 800);
    
    m_networkManager = new QNetworkAccessManager(this);
    
    // Create configs dir
    QDir().mkpath(QDir::homePath() + "/.config/PedalBoard/presets");
    loadFavoritePlugins();
    
    // Initialize audio engine
    if (!m_engine.init("PedalBoard")) {
        std::cerr << "JACK engine failed to initialize!" << std::endl;
    }
    
    // Load configuration settings
    int savedBufferSize = 256;
    std::string savedHwInputLeft, savedHwInputRight;
    std::string savedHwOutputLeft, savedHwOutputRight;
    bool savedHwInputStereo = true;
    bool savedHwOutputStereo = true;
    float savedInputGain = 0.0f;
    float savedOutputGain = 0.0f;
    
    QFile configFile(QDir::homePath() + "/.config/PedalBoard/config.json");
    if (configFile.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("bufferSize")) {
                savedBufferSize = obj["bufferSize"].toInt();
            }
            if (obj.contains("hwInputLeft")) {
                savedHwInputLeft = obj["hwInputLeft"].toString().toStdString();
            }
            if (obj.contains("hwInputRight")) {
                savedHwInputRight = obj["hwInputRight"].toString().toStdString();
            }
            if (obj.contains("hwOutputLeft")) {
                savedHwOutputLeft = obj["hwOutputLeft"].toString().toStdString();
            }
            if (obj.contains("hwOutputRight")) {
                savedHwOutputRight = obj["hwOutputRight"].toString().toStdString();
            }
            if (obj.contains("hwInputStereo")) {
                savedHwInputStereo = obj["hwInputStereo"].toBool();
            }
            if (obj.contains("hwOutputStereo")) {
                savedHwOutputStereo = obj["hwOutputStereo"].toBool();
            }
            if (obj.contains("inputGain")) {
                savedInputGain = obj["inputGain"].toDouble();
            }
            if (obj.contains("outputGain")) {
                savedOutputGain = obj["outputGain"].toDouble();
            }
        }
        configFile.close();
    }
    m_engine.setInputGain(savedInputGain);
    m_engine.setOutputGain(savedOutputGain);
    if (!savedHwInputLeft.empty()) {
        m_engine.setHardwareInputPorts(savedHwInputLeft, savedHwInputRight, savedHwInputStereo);
    }
    if (!savedHwOutputLeft.empty()) {
        m_engine.setHardwareOutputPorts(savedHwOutputLeft, savedHwOutputRight, savedHwOutputStereo);
    }
    
    // JACK must be active before asking it to restore the saved buffer size.
    m_engine.start();
    m_engine.setBufferSize(savedBufferSize);

    // Scan plugins
    scanPlugins();
    
    // Setup UI
    setupUI();
    m_canvas->setSystemChannelModes(m_engine.isHardwareInputStereo(), m_engine.isHardwareOutputStereo());
    m_canvas->applyRoutingChange(true);
    
    // Connect canvas signals
    connect(m_canvas, &NodeCanvas::editPluginUI, this, &MainWindow::showPluginControls);
    connect(m_canvas, &NodeCanvas::nodeSelected, this, &MainWindow::onNodeSelected);
    connect(m_canvas, &NodeCanvas::plusButtonClicked, this, &MainWindow::onPlusButtonClicked);
    connect(m_canvas, &NodeCanvas::nodeContextMenuRequested, this, &MainWindow::onNodeContextMenuRequested);
    connect(m_canvas, &NodeCanvas::branchSelected, this, &MainWindow::showBranchControls);
    connect(m_canvas, &NodeCanvas::routingNodeSelected, this, &MainWindow::showRoutingNodeControls);
    connect(m_canvas, &NodeCanvas::routingChanged, this, [this]() {
        if (!m_isLoadingPreset) {
            setUnsavedChanges(true);
        }
    });
    
    // Start Status Timer
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateCPUStatus);
    m_statusTimer->start(30);

    m_audioPortTimer = new QTimer(this);
    connect(m_audioPortTimer, &QTimer::timeout, this, &MainWindow::refreshAudioPorts);
    m_audioPortTimer->start(1000);
    
    // Refresh presets list
    refreshPresetList();

    // Autoload the last used preset if it was saved in config
    QString lastPresetName;
    QFile configFileCheck(QDir::homePath() + "/.config/PedalBoard/config.json");
    if (configFileCheck.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFileCheck.readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("lastPreset")) {
                lastPresetName = obj["lastPreset"].toString();
            }
        }
        configFileCheck.close();
    }
    
    if (!lastPresetName.isEmpty()) {
        int idx = m_presetCombo->findText(lastPresetName);
        if (idx != -1) {
            m_presetCombo->setCurrentIndex(idx);
            m_currentPresetIndex = idx;
            QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + lastPresetName + ".json";
            if (QFile::exists(fullPath)) {
                loadPresetFromFile(fullPath);
                setUnsavedChanges(false);
            }
        }
    } else {
        setUnsavedChanges(false);
    }
}

MainWindow::~MainWindow() {
    if (m_currentDownloadReply) {
        m_currentDownloadReply->abort();
        m_currentDownloadReply->deleteLater();
    }
    if (m_lilvWorld) {
        lilv_world_free(m_lilvWorld);
    }
}

void MainWindow::setupUI() {
    // Apply Dark Modern Style
    setStyleSheet(R"(
        QMainWindow {
            background-color: #121212;
        }
        QWidget {
            background-color: #121212;
            color: #E0E0E0;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 13px;
        }
        QTreeWidget, QListWidget, QLineEdit, QComboBox {
            background-color: #1E1E1E;
            border: 1px solid #333333;
            border-radius: 4px;
            padding: 4px;
            color: #E0E0E0;
        }
        QTreeWidget::item:hover, QListWidget::item:hover {
            background-color: #2D2D2D;
        }
        QTreeWidget::item:selected, QListWidget::item:selected {
            background-color: #007ACC;
            color: white;
        }
        QPushButton {
            background-color: #00897B;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 6px 12px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #009688;
        }
        QPushButton:pressed {
            background-color: #00796B;
        }
        QProgressBar {
            border: 1px solid #333333;
            border-radius: 4px;
            text-align: center;
            background-color: #1E1E1E;
        }
        QProgressBar::chunk {
            background-color: #00E676;
            width: 10px;
        }
        QSlider::groove:horizontal {
            border: 1px solid #333333;
            height: 6px;
            background: #1E1E1E;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #00B0FF;
            border: 1px solid #0091EA;
            width: 14px;
            height: 14px;
            margin: -4px 0;
            border-radius: 7px;
        }
        QMenu {
            background-color: #1E1E1E;
            border: 1px solid #333333;
            color: #E0E0E0;
        }
        QMenu::item {
            background-color: transparent;
            padding: 6px 24px 6px 16px;
        }
        QMenu::item:selected {
            background-color: #007ACC;
            color: white;
        }
    )");

    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // --- TOP BAR ---
    QWidget* topBarWidget = new QWidget(central);
    topBarWidget->setFixedHeight(36);
    QHBoxLayout* topBar = new QHBoxLayout(topBarWidget);
    topBar->setContentsMargins(0, 0, 0, 0);
    
    QLabel* logo = new QLabel("PEDALBOARD", this);
    logo->setStyleSheet("font-size: 20px; font-weight: bold; color: #00B0FF; letter-spacing: 2px;");
    topBar->addWidget(logo);
    
    topBar->addSpacing(30);
    
    // Preset actions
    topBar->addWidget(new QLabel("Preset:", this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setFixedWidth(130);
    m_presetCombo->setEditable(false);
    connect(m_presetCombo, &QComboBox::activated, this, &MainWindow::onPresetComboActivated);
    topBar->addWidget(m_presetCombo);
    
    QPushButton* saveBtn = new QPushButton("Save", this);
    saveBtn->setToolTip("Save changes to current preset file");
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSavePreset);
    topBar->addWidget(saveBtn);
    
    QPushButton* presetMenuBtn = new QPushButton("⋯", this);
    presetMenuBtn->setToolTip("Preset actions (New, Save As, Rename, Delete)");
    presetMenuBtn->setFixedWidth(30);
    
    QMenu* presetMenu = new QMenu(this);
    QAction* newAct = presetMenu->addAction("New Empty Preset");
    newAct->setShortcut(QKeySequence::New);
    presetMenu->addSeparator();
    QAction* saveAsAct = presetMenu->addAction("Save As...");
    QAction* renameAct = presetMenu->addAction("Rename...");
    QAction* deleteAct = presetMenu->addAction("Delete");
    
    connect(newAct, &QAction::triggered, this, &MainWindow::onNewPreset);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSavePresetAs);
    connect(renameAct, &QAction::triggered, this, &MainWindow::onRenamePreset);
    connect(deleteAct, &QAction::triggered, this, &MainWindow::onDeletePreset);
    
    presetMenuBtn->setMenu(presetMenu);
    topBar->addWidget(presetMenuBtn);
    
    topBar->addStretch();
    
    // Setup Settings Dialog
    m_settingsDialog = new QDialog(this);
    m_settingsDialog->setWindowTitle("Audio Settings");
    m_settingsDialog->setMinimumWidth(400);
    m_settingsDialog->setStyleSheet(styleSheet());
    
    QVBoxLayout* dialogLayout = new QVBoxLayout(m_settingsDialog);
    dialogLayout->setContentsMargins(15, 15, 15, 15);
    dialogLayout->setSpacing(12);
    
    QGroupBox* ioBox = new QGroupBox("Audio Configuration", m_settingsDialog);
    ioBox->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333333; border-radius: 6px; margin-top: 10px; padding: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
    );
    QFormLayout* formLayout = new QFormLayout(ioBox);
    formLayout->setSpacing(10);
    
    m_hwInputModeCombo = new QComboBox(m_settingsDialog);
    m_hwInputModeCombo->addItems({"Mono", "Stereo"});
    m_hwInputModeCombo->setCurrentIndex(m_engine.isHardwareInputStereo() ? 1 : 0);
    connect(m_hwInputModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onInputModeChanged);
    
    m_hwInputCombo = new QComboBox(m_settingsDialog);
    m_hwInputCombo->setToolTip("Audio input device");
    connect(m_hwInputCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onInputHardwareChanged);
    
    m_hwOutputModeCombo = new QComboBox(m_settingsDialog);
    m_hwOutputModeCombo->addItems({"Mono", "Stereo"});
    m_hwOutputModeCombo->setCurrentIndex(m_engine.isHardwareOutputStereo() ? 1 : 0);
    connect(m_hwOutputModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onOutputModeChanged);
    
    m_hwOutputCombo = new QComboBox(m_settingsDialog);
    m_hwOutputCombo->setToolTip("Audio output device");
    connect(m_hwOutputCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onOutputHardwareChanged);
    
    m_bufferSizeCombo = new QComboBox(m_settingsDialog);
    m_bufferSizeCombo->addItems({"64", "128", "256", "512", "1024"});
    int currentSize = m_engine.getBufferSize();
    int sizeIdx = m_bufferSizeCombo->findText(QString::number(currentSize));
    if (sizeIdx != -1) m_bufferSizeCombo->setCurrentIndex(sizeIdx);
    connect(m_bufferSizeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onBufferSizeChanged);
    
    formLayout->addRow("Input Mode:", m_hwInputModeCombo);
    formLayout->addRow("Input Device:", m_hwInputCombo);
    formLayout->addRow("Output Mode:", m_hwOutputModeCombo);
    formLayout->addRow("Output Device:", m_hwOutputCombo);
    formLayout->addRow("Buffer Size:", m_bufferSizeCombo);
    
    dialogLayout->addWidget(ioBox);
    
    // TONE3000 Integration
    QGroupBox* toneBox = new QGroupBox("TONE3000 Integration", m_settingsDialog);
    toneBox->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333333; border-radius: 6px; margin-top: 10px; padding: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
    );
    QFormLayout* toneFormLayout = new QFormLayout(toneBox);
    toneFormLayout->setSpacing(10);
    
    QLineEdit* apiKeyEdit = new QLineEdit(m_settingsDialog);
    apiKeyEdit->setPlaceholderText("Enter your t3k_cs_... Secret Key or Legacy API Key");
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    
    // Load existing value
    {
        QSettings settings("PedalBoard", "PedalBoard");
        QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
        if (!savedKeyEnc.isEmpty()) {
            apiKeyEdit->setText(deobfuscateKey(savedKeyEnc));
        }
    }
    
    connect(apiKeyEdit, &QLineEdit::textChanged, this, [](const QString& text) {
        QSettings settings("PedalBoard", "PedalBoard");
        if (text.trimmed().isEmpty()) {
            settings.remove("tone3000_api_key");
        } else {
            settings.setValue("tone3000_api_key", obfuscateKey(text.trimmed()));
        }
    });
    
    toneFormLayout->addRow("API Key:", apiKeyEdit);
    dialogLayout->addWidget(toneBox);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* closeBtn = new QPushButton("Close", m_settingsDialog);
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, m_settingsDialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    dialogLayout->addLayout(btnLayout);
    
    populateInputPorts();
    populateOutputPorts();
    
    const QString meterStyle =
        "QProgressBar { background-color: #111111; border: 1px solid #2c2c32; border-radius: 4px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00d084, stop:0.8 #ffd54a, stop:1 #ff5252); }";

    // Top bar In controls
    m_inputGainLabel = new QToolButton(this);
    m_inputGainLabel->setText(QString("In  %1 dB").arg(m_engine.getInputGainDB(), 0, 'f', 1));
    m_inputGainLabel->setToolTip("Open input gain fader");
    m_inputGainLabel->setStyleSheet("QToolButton { background: transparent; border: none; color: #b8b8c0; padding: 4px; } QToolButton:hover { color: white; background: #29292f; border-radius: 4px; }");
    topBar->addWidget(m_inputGainLabel);

    m_inputMeter = new QProgressBar(this);
    m_inputMeter->setRange(0, 100);
    m_inputMeter->setValue(0);
    m_inputMeter->setTextVisible(false);
    m_inputMeter->setFixedHeight(10);
    m_inputMeter->setFixedWidth(60);
    m_inputMeter->setStyleSheet(meterStyle);
    topBar->addWidget(m_inputMeter);

    auto* inputPopover = new GainPopover(
        "Input Gain", m_engine.getInputGainDB(),
        [this](float value) {
            m_engine.setInputGain(value);
            m_inputGainLabel->setText(QString("In  %1 dB").arg(value, 0, 'f', 1));
        },
        [this] { saveConfigSettings(); }, this);
    m_inputPopupMeter = inputPopover->meter();
    connect(m_inputGainLabel, &QToolButton::clicked, this, [inputPopover, this] {
        inputPopover->showBelow(m_inputGainLabel);
    });
    
    topBar->addSpacing(15);
    
    // Top bar Out controls
    m_outputGainLabel = new QToolButton(this);
    m_outputGainLabel->setText(QString("Out  %1 dB").arg(m_engine.getOutputGainDB(), 0, 'f', 1));
    m_outputGainLabel->setToolTip("Open output gain fader");
    m_outputGainLabel->setStyleSheet(m_inputGainLabel->styleSheet());
    topBar->addWidget(m_outputGainLabel);

    m_outputMeter = new QProgressBar(this);
    m_outputMeter->setRange(0, 100);
    m_outputMeter->setValue(0);
    m_outputMeter->setTextVisible(false);
    m_outputMeter->setFixedHeight(10);
    m_outputMeter->setFixedWidth(60);
    m_outputMeter->setStyleSheet(meterStyle);
    topBar->addWidget(m_outputMeter);

    auto* outputPopover = new GainPopover(
        "Output Gain", m_engine.getOutputGainDB(),
        [this](float value) {
            m_engine.setOutputGain(value);
            m_outputGainLabel->setText(QString("Out  %1 dB").arg(value, 0, 'f', 1));
        },
        [this] { saveConfigSettings(); }, this);
    m_outputPopupMeter = outputPopover->meter();
    connect(m_outputGainLabel, &QToolButton::clicked, this, [outputPopover, this] {
        outputPopover->showBelow(m_outputGainLabel);
    });
    
    topBar->addSpacing(15);
    
    // Settings Button
    QPushButton* settingsBtn = new QPushButton("Settings", this);
    settingsBtn->setToolTip("Open audio input/output and buffer settings");
    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        m_settingsDialog->exec();
    });
    topBar->addWidget(settingsBtn);

    topBar->addSpacing(15);
    
    // CPU load meter
    topBar->addWidget(new QLabel("DSP CPU:", this));
    m_cpuBar = new QProgressBar(this);
    m_cpuBar->setRange(0, 100);
    m_cpuBar->setValue(0);
    m_cpuBar->setMaximumWidth(100);
    m_cpuBar->setFixedHeight(16);
    topBar->addWidget(m_cpuBar);
    
    mainLayout->addWidget(topBarWidget);
    
    // --- WORKSPACE SPLITTER ---
    QSplitter* midSplitter = new QSplitter(Qt::Horizontal, this);
    
    // Center: Node Graph Canvas
    m_canvas = new NodeCanvas(&m_engine, this);
    m_canvas->setMinimumWidth(0);
    midSplitter->addWidget(m_canvas);
    midSplitter->setStretchFactor(0, 1);
    
    // Right panel: parameter sliders
    m_paramContainer = new QWidget(this);
    m_paramContainer->setMinimumWidth(240);
    m_paramContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_paramContainer->setStyleSheet("background-color: #1E1E1E; border-left: 1px solid #333333;");
    
    QVBoxLayout* rightLayout = new QVBoxLayout(m_paramContainer);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    
    // --- PARAMETER CONTROL PANEL HEADER ---
    QLabel* paramHeader = new QLabel("Inspector", this);
    paramHeader->setStyleSheet("font-weight: bold; font-size: 14px; color: #00B0FF;");
    rightLayout->addWidget(paramHeader);
    
    m_noParamLabel = new QLabel("Select an effect node\nto show parameters", this);
    m_noParamLabel->setAlignment(Qt::AlignCenter);
    m_noParamLabel->setStyleSheet("color: #666666; font-style: italic;");
    rightLayout->addWidget(m_noParamLabel);
    
    auto* paramScroll = new QScrollArea(m_paramContainer);
    paramScroll->setWidgetResizable(true);
    paramScroll->setFrameShape(QFrame::NoFrame);
    paramScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    paramScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    paramScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* paramContent = new QWidget(paramScroll);
    m_paramLayout = new QVBoxLayout(paramContent);
    m_paramLayout->setContentsMargins(0, 0, 0, 0);
    m_paramLayout->setSpacing(6);
    m_paramLayout->setAlignment(Qt::AlignTop);
    paramScroll->setWidget(paramContent);
    rightLayout->addWidget(paramScroll, 1);
    
    midSplitter->addWidget(m_paramContainer);
    midSplitter->setStretchFactor(1, 0);
    midSplitter->setSizes({960, 260});
    
    mainLayout->addWidget(midSplitter);
    
    // --- STATUS BAR ---
    m_statusLabel = new QLabel("Ready | JACK Latency: " + QString::number(currentSize * 1000.0 / m_engine.getSampleRate(), 'f', 2) + " ms", this);
    m_statusLabel->setFixedHeight(24);
    m_statusLabel->setContentsMargins(8, 0, 8, 0);
    m_statusLabel->setStyleSheet("color: #888888; font-size: 11px; background-color: #18181B; border-top: 1px solid #29292D;");
    mainLayout->addWidget(m_statusLabel);
}

void MainWindow::scanPlugins() {
    m_lilvWorld = lilv_world_new();
    lilv_world_load_all(m_lilvWorld);
    
    const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
    LilvNode* brandProperty = lilv_new_uri(m_lilvWorld, "http://moddevices.com/ns/mod#brand");
    LilvNode* thumbnailProperty = lilv_new_uri(m_lilvWorld, "http://moddevices.com/ns/modgui#thumbnail");
    
    LILV_FOREACH(plugins, i, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, i);
        
        PluginInfo info;
        
        LilvNode* nameNode = lilv_plugin_get_name(p);
        info.name = lilv_node_as_string(nameNode);
        lilv_node_free(nameNode);
        
        const LilvNode* uriNode = lilv_plugin_get_uri(p);
        info.uri = lilv_node_as_string(uriNode);
        if (LilvNodes* brands = lilv_plugin_get_value(p, brandProperty)) {
            if (const LilvNode* brand = lilv_nodes_get_first(brands)) {
                info.brand = lilv_node_as_string(brand);
            }
            lilv_nodes_free(brands);
        }
        if (LilvNodes* thumbnails = lilv_plugin_get_value(p, thumbnailProperty)) {
            if (const LilvNode* thumbnail = lilv_nodes_get_first(thumbnails)) {
                if (char* path = lilv_file_uri_parse(lilv_node_as_uri(thumbnail), nullptr)) {
                    info.thumbnailPath = QString::fromLocal8Bit(path);
                    lilv_free(path);
                }
            }
            lilv_nodes_free(thumbnails);
        }
        
        info.isLV2 = true;
        
        // Extract category
        const LilvPluginClass* pclass = lilv_plugin_get_class(p);
        const LilvNode* classNode = lilv_plugin_class_get_uri(pclass);
        std::string classURI = lilv_node_as_string(classNode);
        
        if (classURI.find("Amplifier") != std::string::npos) info.category = "Amplifiers";
        else if (classURI.find("Delay") != std::string::npos) info.category = "Delays";
        else if (classURI.find("Reverb") != std::string::npos) info.category = "Reverbs";
        else if (classURI.find("Distortion") != std::string::npos) info.category = "Distortions";
        else if (classURI.find("Dynamics") != std::string::npos) info.category = "Dynamics";
        else if (classURI.find("Filter") != std::string::npos || classURI.find("EQ") != std::string::npos) info.category = "EQ & Filters";
        else if (classURI.find("Modulator") != std::string::npos || classURI.find("Chorus") != std::string::npos || classURI.find("Flanger") != std::string::npos || classURI.find("Phaser") != std::string::npos) info.category = "Modulations";
        else info.category = "Utilities";
        
        m_availablePlugins.push_back(info);
    }
    lilv_node_free(brandProperty);
    lilv_node_free(thumbnailProperty);
    
    // Scan standard VST3 paths dynamically
    std::vector<std::string> vst3Dirs = {
        "/usr/lib/vst3",
        "/usr/lib64/vst3",
        "/usr/local/lib/vst3",
        (QDir::homePath() + "/.vst3").toStdString()
    };
    
    QSet<QString> scannedPaths;
    for (const auto& dirPath : vst3Dirs) {
        if (!std::filesystem::exists(dirPath)) continue;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (entry.path().extension() == ".vst3") {
                    QString fullPath = QString::fromStdString(entry.path().string());
                    if (scannedPaths.contains(fullPath)) continue;
                    scannedPaths.insert(fullPath);
                    
                    std::string name = entry.path().stem().string();
                    PluginInfo vstInfo = { name, entry.path().string(), "VST3 Plugins", "", "", false };
                    m_availablePlugins.push_back(vstInfo);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning VST3 directory " << dirPath << ": " << e.what() << std::endl;
        }
    }
    
    // Add VST3 stubs if not found on disk
    if (!scannedPaths.contains("/usr/lib64/vst3/Guitarix.vst3")) {
        PluginInfo vst3Info1 = { "GxGuitarix", "/usr/lib64/vst3/Guitarix.vst3", "VST3 Plugins", "", "", false };
        m_availablePlugins.push_back(vst3Info1);
    }
    if (!scannedPaths.contains("/usr/lib64/vst3/Multi_Tap_Delay.vst3")) {
        PluginInfo vst3Info2 = { "Multi Tap Delay", "/usr/lib64/vst3/Multi_Tap_Delay.vst3", "VST3 Plugins", "", "", false };
        m_availablePlugins.push_back(vst3Info2);
    }
    
    // Scan CLAP plugins
    auto clapPlugins = CLAPPluginNode::scanStandardPaths();
    for (const auto& clapDesc : clapPlugins) {
        std::string uri = clapDesc.pluginPath + ":" + std::to_string(clapDesc.pluginIndex);
        std::string category = "CLAP Plugins";
        if (!clapDesc.features.empty()) {
            std::string feat = clapDesc.features[0];
            if (feat.find("distortion") != std::string::npos || feat.find("fuzz") != std::string::npos || feat.find("overdrive") != std::string::npos) category = "Distortions";
            else if (feat.find("delay") != std::string::npos || feat.find("reverb") != std::string::npos) category = "Delays & Reverbs";
            else if (feat.find("filter") != std::string::npos || feat.find("equalizer") != std::string::npos) category = "EQ & Filters";
            else if (feat.find("modulation") != std::string::npos || feat.find("chorus") != std::string::npos || feat.find("flanger") != std::string::npos || feat.find("phaser") != std::string::npos) category = "Modulations";
        }
        PluginInfo clapInfo = { clapDesc.name, uri, category, clapDesc.vendor, "", false };
        m_availablePlugins.push_back(clapInfo);
    }

    PluginInfo bypassInfo = { "Bypass / Pass-through", "builtin:bypass", "Utilities", "", "", false };
    m_availablePlugins.push_back(bypassInfo);
}

void MainWindow::refreshPresetList() {
    m_presetCombo->clear();
    QDir presetsDir(QDir::homePath() + "/.config/PedalBoard/presets");
    QStringList files = presetsDir.entryList({"*.json"}, QDir::Files);
    for (const auto& file : files) {
        m_presetCombo->addItem(file.left(file.length() - 5));
    }
}

void MainWindow::loadFavoritePlugins() {
    QFile file(QDir::homePath() + "/.config/PedalBoard/plugin-favorites.json");
    if (!file.open(QFile::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    for (const QJsonValue& value : document.array()) {
        if (value.isString()) m_favoritePluginUris.insert(value.toString());
    }
}

void MainWindow::saveFavoritePlugins() const {
    QJsonArray favorites;
    QStringList values = m_favoritePluginUris.values();
    values.sort();
    for (const QString& value : values) favorites.append(value);
    QFile file(QDir::homePath() + "/.config/PedalBoard/plugin-favorites.json");
    if (file.open(QFile::WriteOnly | QFile::Truncate)) {
        file.write(QJsonDocument(favorites).toJson());
    }
}

QString MainWindow::pluginPresetDirectory(const AudioNode& node) const {
    const QByteArray identifier = QByteArray::fromStdString(node.getPluginURI().empty() ? node.getName() : node.getPluginURI());
    const QString pluginId = QString::fromLatin1(QCryptographicHash::hash(identifier, QCryptographicHash::Sha256).toHex());
    return QDir::homePath() + "/.config/PedalBoard/plugin-presets/" + pluginId;
}

bool MainWindow::isValidPluginPresetName(const QString& name) const {
    return !name.trimmed().isEmpty() && !name.contains('/') && !name.contains('\\');
}

void MainWindow::refreshPluginPresetList(
    const std::shared_ptr<AudioNode>& node, QComboBox* combo, const QString& selected) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem("Plugin preset...", "");
    QDir directory(pluginPresetDirectory(*node));
    const QStringList files = directory.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const QString name = file.left(file.size() - 5);
        combo->addItem(name, name);
    }
    const int index = combo->findData(selected);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

bool MainWindow::savePluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name) {
    if (!isValidPluginPresetName(name)) {
        QMessageBox::warning(this, "Plugin Preset", "Preset names cannot be empty or contain path separators.");
        return false;
    }
    const QString directoryPath = pluginPresetDirectory(*node);
    if (!QDir().mkpath(directoryPath)) return false;

    QJsonArray parameters;
    for (const auto& port : node->getControlPorts()) {
        if (!port.isOutput) {
            parameters.append(QJsonObject{{"index", static_cast<int>(port.index)}, {"value", port.value}});
        }
    }
    QJsonObject preset{
        {"pluginUri", QString::fromStdString(node->getPluginURI())},
        {"pluginName", QString::fromStdString(node->getName())},
        {"parameters", parameters}
    };
    const QString modelPath = QString::fromStdString(node->getModelFilePath());
    QString modelFileName;
    if (!modelPath.isEmpty()) {
        const QFileInfo modelInfo(modelPath);
        if (!modelInfo.isFile()) {
            QMessageBox::warning(this, "Plugin Preset", "The active NAM model file is no longer available.");
            return false;
        }
        const QString suffix = modelInfo.suffix().isEmpty() ? "nam" : modelInfo.suffix();
        modelFileName = "model-" + QString::fromLatin1(
            QCryptographicHash::hash(name.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex()) + "." + suffix;
        const QString savedModelPath = QDir(directoryPath).filePath(modelFileName);
        if (modelPath != savedModelPath) {
            QFile::remove(savedModelPath);
            if (!QFile::copy(modelPath, savedModelPath)) {
                QMessageBox::warning(this, "Plugin Preset", "Could not save the active NAM model file.");
                return false;
            }
        }
        preset["modelFile"] = modelFileName;
        preset["modelDisplayName"] = QString::fromStdString(node->getModelDisplayName());
        preset["modelName"] = modelInfo.fileName();
        preset["modelSourceUrl"] = QString::fromStdString(node->getModelSourceUrl());
    }
    QJsonArray modelVariants;
    for (const auto& variant : node->getModelVariants()) {
        QJsonObject value{
            {"name", QString::fromStdString(variant.name)},
            {"url", QString::fromStdString(variant.url)}
        };
        if (!modelFileName.isEmpty() && variant.localPath == modelPath.toStdString()) {
            value["localFile"] = modelFileName;
        }
        modelVariants.append(value);
    }
    preset["modelVariants"] = modelVariants;
    QFile file(QDir(directoryPath).filePath(name.trimmed() + ".json"));
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) return false;
    file.write(QJsonDocument(preset).toJson());
    m_activePluginPresetNodeId = node->uniqueId;
    m_activePluginPresetName = name.trimmed();
    return true;
}

bool MainWindow::loadPluginPreset(const std::shared_ptr<AudioNode>& node, const QString& name) {
    QFile file(QDir(pluginPresetDirectory(*node)).filePath(name + ".json"));
    if (!file.open(QFile::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const QJsonObject preset = document.object();
    if (preset.value("pluginUri").toString() != QString::fromStdString(node->getPluginURI())) return false;
    for (const QJsonValue& value : preset.value("parameters").toArray()) {
        const QJsonObject parameter = value.toObject();
        node->setParameter(parameter.value("index").toInt(), static_cast<float>(parameter.value("value").toDouble()));
    }
    const QString modelFile = preset.value("modelFile").toString();
    QString loadedModelPath;
    QString displayName = preset.value("modelDisplayName").toString();
    if (!modelFile.isEmpty() && !modelFile.contains('/') && !modelFile.contains('\\')) {
        const QString modelPath = QDir(pluginPresetDirectory(*node)).filePath(modelFile);
        if (!QFileInfo(modelPath).isFile()) {
            QMessageBox::warning(this, "Plugin Preset", "The NAM model saved with this preset is missing.");
            return false;
        }
        m_engine.suspendProcessing();
        node->loadModelFile(modelPath.toStdString());
        m_engine.resumeProcessing();
        loadedModelPath = modelPath;
        node->setModelSourceUrl(preset.value("modelSourceUrl").toString().toStdString());
    }
    if (preset.contains("modelVariants")) {
        std::vector<AudioNode::ModelVariant> modelVariants;
        for (const QJsonValue& value : preset.value("modelVariants").toArray()) {
            const QJsonObject savedVariant = value.toObject();
            AudioNode::ModelVariant variant;
            variant.name = savedVariant.value("name").toString().toStdString();
            variant.url = savedVariant.value("url").toString().toStdString();
            const QString localFile = savedVariant.value("localFile").toString();
            if (!localFile.isEmpty() && !localFile.contains('/') && !localFile.contains('\\')) {
                const QString localPath = QDir(pluginPresetDirectory(*node)).filePath(localFile);
                if (QFileInfo(localPath).isFile()) variant.localPath = localPath.toStdString();
            }
            modelVariants.push_back(std::move(variant));
        }
        node->setModelVariants(modelVariants);
    }
    if (!loadedModelPath.isEmpty()) {
        if (displayName.isEmpty()) displayName = preset.value("modelName").toString();
        if (displayName.isEmpty() && node->getModelVariants().size() == 1) {
            displayName = QString::fromStdString(node->getModelVariants().front().name);
        }
        node->setModelDisplayName((displayName.isEmpty() ? QFileInfo(loadedModelPath).fileName() : displayName).toStdString());
    }
    syncParameterControls();
    m_activePluginPresetNodeId = node->uniqueId;
    m_activePluginPresetName = name;
    setUnsavedChanges(true);
    QTimer::singleShot(0, this, [this, node]() {
        if (m_parameterControlNode == node) showPluginControls(node);
    });
    return true;
}

void MainWindow::onPlusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol) {
    Q_UNUSED(screenPos);
    std::vector<PickerPluginInfo> plugins;
    plugins.reserve(m_availablePlugins.size());
    for (const auto& info : m_availablePlugins) {
        QString name = QString::fromStdString(info.name);
        QString category = QString::fromStdString(info.category);
        QString brand = QString::fromStdString(info.brand);
        QString uri = QString::fromStdString(info.uri);
        QString searchable = (name + " " + category + " " + brand + " " + uri).toLower();
        plugins.push_back({
            name,
            uri,
            category,
            brand,
            info.thumbnailPath,
            info.isLV2 ? "LV2" : (info.uri == "builtin:bypass" ? "Built-in" : (info.category == "CLAP Plugins" || info.uri.find(".clap") != std::string::npos ? "CLAP" : "VST3")),
            searchable
        });
    }
    PluginPickerDialog picker(plugins, &m_favoritePluginUris, [this] { saveFavoritePlugins(); }, this);
    if (picker.exec() != QDialog::Accepted) return;

    const std::string uri = picker.selectedUri().toStdString();
    std::shared_ptr<AudioNode> newNode;
    for (const auto& info : m_availablePlugins) {
        if (info.uri == uri) {
            if (uri == "builtin:bypass") {
                newNode = std::make_shared<BypassNode>();
            } else if (info.isLV2) {
                const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
                LILV_FOREACH(plugins, i, plugins) {
                    const LilvPlugin* p = lilv_plugins_get(plugins, i);
                    const LilvNode* uriNode = lilv_plugin_get_uri(p);
                    std::string puri = lilv_node_as_string(uriNode);
                    if (puri == uri) {
                        newNode = std::make_shared<LV2PluginNode>(m_lilvWorld, p);
                        break;
                    }
                }
            } else if (info.category == "CLAP Plugins" || info.uri.find(".clap") != std::string::npos) {
                std::string path = info.uri;
                uint32_t idx = 0;
                auto colonPos = path.rfind(':');
                if (colonPos != std::string::npos && colonPos > path.find(".clap")) {
                    idx = std::stoul(path.substr(colonPos + 1));
                    path = path.substr(0, colonPos);
                }
                newNode = std::make_shared<CLAPPluginNode>(path, idx);
            } else {
                newNode = std::make_shared<VST3PluginNode>(info.uri);
            }
            break;
        }
    }
    
    if (newNode) {
        newNode->uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        // col here is the chain insertion index (from PlusButtonWidget), not a fixed grid column.
        // Use insertPluginBefore to shift the chain correctly.
        m_canvas->insertPluginBefore(row, col, newNode, isSecondOfCol);
        showPluginControls(newNode);
    }
}

void MainWindow::onNodeContextMenuRequested(int row, int col, QPoint screenPos) {
    auto node = m_canvas->getPluginAt(row, col);
    if (!node) return;
    
    QMenu menu(this);
    menu.setStyleSheet(R"(
        QMenu {
            background-color: #1E1E1E;
            color: #E0E0E0;
            border: 1px solid #333333;
        }
        QMenu::item:selected {
            background-color: #007ACC;
            color: white;
        }
    )");
    
    QAction* bypassAct = menu.addAction(node->isBypassed() ? "Enable" : "Bypass");
    QAction* removeAct = menu.addAction("Remove Effect");
    QAction* replaceAct = menu.addAction("Replace Effect");
    
    QAction* selected = menu.exec(screenPos);
    if (!selected) return;
    
    if (selected == bypassAct) {
        node->setBypassed(!node->isBypassed());
        m_canvas->updateLayout();
    } else if (selected == removeAct) {
        m_canvas->removePluginAt(row, col);
        showPluginControls(nullptr);
    } else if (selected == replaceAct) {
        std::vector<PickerPluginInfo> plugins;
        plugins.reserve(m_availablePlugins.size());
        for (const auto& info : m_availablePlugins) {
            QString name = QString::fromStdString(info.name);
            QString category = QString::fromStdString(info.category);
            QString brand = QString::fromStdString(info.brand);
            QString uri = QString::fromStdString(info.uri);
            QString searchable = (name + " " + category + " " + brand + " " + uri).toLower();
            plugins.push_back({
                name,
                uri,
                category,
                brand,
                info.thumbnailPath,
                info.isLV2 ? "LV2" : (info.uri == "builtin:bypass" ? "Built-in" : (info.category == "CLAP Plugins" || info.uri.find(".clap") != std::string::npos ? "CLAP" : "VST3")),
                searchable
            });
        }
        PluginPickerDialog picker(plugins, &m_favoritePluginUris, [this] { saveFavoritePlugins(); }, this);
        if (picker.exec() == QDialog::Accepted) {
            std::string uri = picker.selectedUri().toStdString();
            
            std::shared_ptr<AudioNode> newNode;
            if (uri == "builtin:bypass") {
                newNode = std::make_shared<BypassNode>();
            } else {
                for (const auto& info : m_availablePlugins) {
                    if (info.uri == uri) {
                        if (info.isLV2) {
                            const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
                            LILV_FOREACH(plugins, i, plugins) {
                                const LilvPlugin* p = lilv_plugins_get(plugins, i);
                                const LilvNode* uriNode = lilv_plugin_get_uri(p);
                                std::string puri = lilv_node_as_string(uriNode);
                                if (puri == uri) {
                                    newNode = std::make_shared<LV2PluginNode>(m_lilvWorld, p);
                                    break;
                                }
                            }
                        } else if (info.category == "CLAP Plugins" || info.uri.find(".clap") != std::string::npos) {
                            std::string path = info.uri;
                            uint32_t idx = 0;
                            auto colonPos = path.rfind(':');
                            if (colonPos != std::string::npos && colonPos > path.find(".clap")) {
                                idx = std::stoul(path.substr(colonPos + 1));
                                path = path.substr(0, colonPos);
                            }
                            newNode = std::make_shared<CLAPPluginNode>(path, idx);
                        } else {
                            newNode = std::make_shared<VST3PluginNode>(info.uri);
                        }
                        break;
                    }
                }
            }
            
            if (newNode) {
                newNode->uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                m_canvas->replacePluginAt(row, col, newNode);
                showPluginControls(newNode);
            }
        }
    }
}

void MainWindow::setUnsavedChanges(bool unsaved) {
    m_unsavedChanges = unsaved;
    
    QString title = "PedalBoard - Guitar Multieffects host";
    if (m_unsavedChanges) {
        title += " *";
    }
    
    QString currentPreset = m_presetCombo->currentText();
    if (!currentPreset.isEmpty()) {
        title += " [" + currentPreset + "]";
    }
    
    setWindowTitle(title);
}

bool MainWindow::promptUnsavedChanges() {
    if (!m_unsavedChanges) return true; // Safe to proceed

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Unsaved Changes");
    msgBox.setText("You have unsaved changes in your current preset. Do you want to save them?");
    msgBox.setIcon(QMessageBox::Warning);
    QPushButton* saveBtn = msgBox.addButton("Save", QMessageBox::AcceptRole);
    QPushButton* discardBtn = msgBox.addButton("Discard", QMessageBox::DestructiveRole);
    QPushButton* cancelBtn = msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    if (msgBox.clickedButton() == saveBtn) {
        onSavePreset();
        return true; // Safe to proceed after saving
    } else if (msgBox.clickedButton() == discardBtn) {
        setUnsavedChanges(false);
        return true; // Safe to proceed
    }

    return false; // Cancelled
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (promptUnsavedChanges()) {
        saveConfigSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick && watched->property("branchResetValue").isValid()) {
        if (auto* slider = qobject_cast<QSlider*>(watched)) {
            slider->setValue(watched->property("branchResetValue").toInt());
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick && m_parameterControlNode) {
        if (watched->property("parameterEdit").toBool()) {
            const uint32_t index = watched->property("parameterIndex").toUInt();
            const float minimum = watched->property("parameterMinimum").toFloat();
            const float maximum = watched->property("parameterMaximum").toFloat();
            float currentValue = minimum;
            for (const auto& port : m_parameterControlNode->getControlPorts()) {
                if (port.index == index) {
                    currentValue = port.value;
                    break;
                }
            }
            bool accepted = false;
            const double value = QInputDialog::getDouble(
                this, "Set Parameter", "Value:", currentValue, minimum, maximum, 4, &accepted);
            if (accepted) {
                m_parameterControlNode->setParameter(index, static_cast<float>(value));
                syncParameterControls();
                setUnsavedChanges(true);
            }
            return true;
        }
        const QVariant index = watched->property("parameterIndex");
        const QVariant defaultValue = watched->property("parameterDefault");
        if (index.isValid() && defaultValue.isValid()) {
            m_parameterControlNode->setParameter(index.toUInt(), defaultValue.toFloat());
            syncParameterControls();
            setUnsavedChanges(true);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSavePreset() {
    QString presetName = m_presetCombo->currentText();
    if (presetName.isEmpty()) {
        onSavePresetAs();
        return;
    }
    
    QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + presetName + ".json";
    savePresetToFile(fullPath);
    setUnsavedChanges(false);
    saveConfigSettings();
}

void MainWindow::onNewPreset() {
    if (!promptUnsavedChanges()) return;

    m_isLoadingPreset = true;
    m_parameterControlNode.reset();
    m_parameterControlBindings.clear();
    m_activePluginPresetNodeId.clear();
    m_activePluginPresetName.clear();
    showPluginControls(nullptr);
    m_canvas->clearCanvas();
    m_presetCombo->setCurrentIndex(-1);
    m_presetCombo->setPlaceholderText("Untitled");
    m_currentPresetIndex = -1;
    m_isLoadingPreset = false;
    setUnsavedChanges(true);
    saveConfigSettings();
}

void MainWindow::onSavePresetAs() {
    bool ok;
    QString name = QInputDialog::getText(this, "Save Preset As",
                                         "Enter preset name:", QLineEdit::Normal,
                                         "", &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString presetName = name.trimmed();
        presetName.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "");
        if (presetName.isEmpty()) return;
        
        QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + presetName + ".json";
        
        if (QFile::exists(fullPath)) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Overwrite Preset?",
                                               "A preset named \"" + presetName + "\" already exists. Overwrite?",
                                               QMessageBox::Yes|QMessageBox::No);
            if (reply == QMessageBox::No) return;
        }
        
        savePresetToFile(fullPath);
        refreshPresetList();
        m_presetCombo->setCurrentText(presetName);
        m_currentPresetIndex = m_presetCombo->currentIndex();
        setUnsavedChanges(false);
        saveConfigSettings();
    }
}

void MainWindow::onRenamePreset() {
    QString oldPresetName = m_presetCombo->currentText();
    if (oldPresetName.isEmpty()) return;
    
    bool ok;
    QString name = QInputDialog::getText(this, "Rename Preset",
                                         "Enter new name for \"" + oldPresetName + "\":",
                                         QLineEdit::Normal, oldPresetName, &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString newPresetName = name.trimmed();
        newPresetName.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "");
        if (newPresetName.isEmpty() || newPresetName == oldPresetName) return;
        
        QString oldPath = QDir::homePath() + "/.config/PedalBoard/presets/" + oldPresetName + ".json";
        QString newPath = QDir::homePath() + "/.config/PedalBoard/presets/" + newPresetName + ".json";
        
        if (QFile::exists(newPath)) {
            QMessageBox::critical(this, "Error", "A preset named \"" + newPresetName + "\" already exists.");
            return;
        }
        
        if (QFile::rename(oldPath, newPath)) {
            refreshPresetList();
            m_presetCombo->setCurrentText(newPresetName);
            m_currentPresetIndex = m_presetCombo->currentIndex();
            setUnsavedChanges(m_unsavedChanges);
            saveConfigSettings();
        } else {
            QMessageBox::critical(this, "Error", "Failed to rename the preset file.");
        }
    }
}

void MainWindow::onDeletePreset() {
    QString presetName = m_presetCombo->currentText();
    if (presetName.isEmpty()) return;
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Preset",
                                       "Are you sure you want to delete the preset \"" + presetName + "\"?\nThis cannot be undone.",
                                       QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + presetName + ".json";
        if (QFile::remove(fullPath)) {
            setUnsavedChanges(false);
            m_canvas->clearCanvas();
            m_canvas->updateLayout();
            refreshPresetList();
            if (m_presetCombo->count() > 0) {
                m_presetCombo->setCurrentIndex(0);
                m_currentPresetIndex = 0;
                onLoadPreset();
            } else {
                m_presetCombo->setCurrentText("");
                m_currentPresetIndex = -1;
            }
            saveConfigSettings();
        } else {
            QMessageBox::critical(this, "Error", "Failed to delete the preset file.");
        }
    }
}

void MainWindow::onPresetComboActivated(int index) {
    if (index < 0 || index >= m_presetCombo->count()) return;
    
    if (m_currentPresetIndex == index) return; // No change

    // Temporarily restore index in combobox so prompt can revert if user cancels
    m_presetCombo->setCurrentIndex(m_currentPresetIndex != -1 ? m_currentPresetIndex : index);

    if (promptUnsavedChanges()) {
        m_presetCombo->setCurrentIndex(index);
        QString presetName = m_presetCombo->itemText(index);
        QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + presetName + ".json";
        loadPresetFromFile(fullPath);
        m_currentPresetIndex = index;
        setUnsavedChanges(false);
        saveConfigSettings();
    }
}

void MainWindow::onLoadPreset() {
    QString presetName = m_presetCombo->currentText();
    if (presetName.isEmpty()) return;
    
    QString fullPath = QDir::homePath() + "/.config/PedalBoard/presets/" + presetName + ".json";
    loadPresetFromFile(fullPath);
    m_currentPresetIndex = m_presetCombo->currentIndex();
    setUnsavedChanges(false);
}

void MainWindow::onBufferSizeChanged(int index) {
    int size = m_bufferSizeCombo->currentText().toInt();
    m_engine.setBufferSize(size);
    m_statusLabel->setText("Ready | JACK Latency: " + QString::number(size * 1000.0 / m_engine.getSampleRate(), 'f', 2) + " ms");
    saveConfigSettings();
}

void MainWindow::onInputModeChanged(int index) {
    populateInputPorts();
    onInputHardwareChanged(m_hwInputCombo->currentIndex());
    m_canvas->setSystemChannelModes(index == 1, m_hwOutputModeCombo->currentIndex() == 1);
}

void MainWindow::onOutputModeChanged(int index) {
    populateOutputPorts();
    onOutputHardwareChanged(m_hwOutputCombo->currentIndex());
    m_canvas->setSystemChannelModes(m_hwInputModeCombo->currentIndex() == 1, index == 1);
}

void MainWindow::populateInputPorts() {
    m_hwInputCombo->blockSignals(true);
    m_hwInputCombo->clear();
    
    auto inputs = m_engine.getPhysicalInputs();
    m_knownPhysicalInputs = inputs;
    bool isStereo = m_hwInputModeCombo->currentIndex() == 1;
    
    if (isStereo) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (i + 1 < inputs.size()) {
                std::string p1 = inputs[i];
                std::string p2 = inputs[i+1];
                size_t colon1 = p1.find(':');
                size_t colon2 = p2.find(':');
                if (colon1 != std::string::npos && colon2 != std::string::npos &&
                    p1.substr(0, colon1) == p2.substr(0, colon2)) {
                    QString label = QString::fromStdString(p1) + " + " + QString::fromStdString(p2.substr(colon2 + 1));
                    m_hwInputCombo->addItem(label, QVariant(QStringList{QString::fromStdString(p1), QString::fromStdString(p2)}));
                    i++;
                    continue;
                }
            }
        }
    } else {
        for (const auto& port : inputs) {
            m_hwInputCombo->addItem(QString::fromStdString(port), QVariant(QStringList{QString::fromStdString(port), ""}));
        }
    }
    
    std::string currentL = m_engine.getHardwareInputLeft();
    int selectIdx = currentL.empty() ? 0 : -1;
    for (int i = 0; i < m_hwInputCombo->count(); ++i) {
        QStringList ports = m_hwInputCombo->itemData(i).toStringList();
        if (!ports.isEmpty() && ports[0].toStdString() == currentL) {
            selectIdx = i;
            break;
        }
    }
    m_hwInputCombo->setCurrentIndex(selectIdx);
    m_hwInputCombo->blockSignals(false);
}

void MainWindow::populateOutputPorts() {
    m_hwOutputCombo->blockSignals(true);
    m_hwOutputCombo->clear();
    
    auto outputs = m_engine.getPhysicalOutputs();
    m_knownPhysicalOutputs = outputs;
    bool isStereo = m_hwOutputModeCombo->currentIndex() == 1;
    
    if (isStereo) {
        for (size_t i = 0; i < outputs.size(); ++i) {
            if (i + 1 < outputs.size()) {
                std::string p1 = outputs[i];
                std::string p2 = outputs[i+1];
                size_t colon1 = p1.find(':');
                size_t colon2 = p2.find(':');
                if (colon1 != std::string::npos && colon2 != std::string::npos &&
                    p1.substr(0, colon1) == p2.substr(0, colon2)) {
                    QString label = QString::fromStdString(p1) + " + " + QString::fromStdString(p2.substr(colon2 + 1));
                    m_hwOutputCombo->addItem(label, QVariant(QStringList{QString::fromStdString(p1), QString::fromStdString(p2)}));
                    i++;
                    continue;
                }
            }
        }
    } else {
        for (const auto& port : outputs) {
            m_hwOutputCombo->addItem(QString::fromStdString(port), QVariant(QStringList{QString::fromStdString(port), ""}));
        }
    }
    
    std::string currentL = m_engine.getHardwareOutputLeft();
    int selectIdx = currentL.empty() ? 0 : -1;
    for (int i = 0; i < m_hwOutputCombo->count(); ++i) {
        QStringList ports = m_hwOutputCombo->itemData(i).toStringList();
        if (!ports.isEmpty() && ports[0].toStdString() == currentL) {
            selectIdx = i;
            break;
        }
    }
    m_hwOutputCombo->setCurrentIndex(selectIdx);
    m_hwOutputCombo->blockSignals(false);
}

void MainWindow::refreshAudioPorts() {
    const auto inputs = m_engine.getPhysicalInputs();
    const auto outputs = m_engine.getPhysicalOutputs();
    const bool inputsChanged = inputs != m_knownPhysicalInputs;
    const bool outputsChanged = outputs != m_knownPhysicalOutputs;

    if (inputsChanged) populateInputPorts();
    if (outputsChanged) populateOutputPorts();
    if (inputsChanged || outputsChanged) m_engine.updateHardwareConnections();
}

void MainWindow::onInputHardwareChanged(int index) {
    if (index < 0 || index >= m_hwInputCombo->count()) return;
    QStringList ports = m_hwInputCombo->itemData(index).toStringList();
    if (ports.size() >= 2) {
        m_engine.setHardwareInputPorts(ports[0].toStdString(), ports[1].toStdString(), m_hwInputModeCombo->currentIndex() == 1);
    }
    m_canvas->setSystemChannelModes(m_hwInputModeCombo->currentIndex() == 1, m_hwOutputModeCombo->currentIndex() == 1);
    saveConfigSettings();
}

void MainWindow::onOutputHardwareChanged(int index) {
    if (index < 0 || index >= m_hwOutputCombo->count()) return;
    QStringList ports = m_hwOutputCombo->itemData(index).toStringList();
    if (ports.size() >= 2) {
        m_engine.setHardwareOutputPorts(ports[0].toStdString(), ports[1].toStdString(), m_hwOutputModeCombo->currentIndex() == 1);
    }
    m_canvas->setSystemChannelModes(m_hwInputModeCombo->currentIndex() == 1, m_hwOutputModeCombo->currentIndex() == 1);
    saveConfigSettings();
}

void MainWindow::saveConfigSettings() {
    QJsonObject configObj;
    
    QFile configFileRead(QDir::homePath() + "/.config/PedalBoard/config.json");
    if (configFileRead.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFileRead.readAll());
        if (doc.isObject()) {
            configObj = doc.object();
        }
        configFileRead.close();
    }
    
    configObj["bufferSize"] = m_bufferSizeCombo->currentText().toInt();
    configObj["hwInputLeft"] = QString::fromStdString(m_engine.getHardwareInputLeft());
    configObj["hwInputRight"] = QString::fromStdString(m_engine.getHardwareInputRight());
    configObj["hwOutputLeft"] = QString::fromStdString(m_engine.getHardwareOutputLeft());
    configObj["hwOutputRight"] = QString::fromStdString(m_engine.getHardwareOutputRight());
    configObj["hwInputStereo"] = m_hwInputModeCombo->currentIndex() == 1;
    configObj["hwOutputStereo"] = m_hwOutputModeCombo->currentIndex() == 1;
    configObj["inputGain"] = m_engine.getInputGainDB();
    configObj["outputGain"] = m_engine.getOutputGainDB();
    configObj["lastPreset"] = m_presetCombo->currentText();
    
    QFile configFileWrite(QDir::homePath() + "/.config/PedalBoard/config.json");
    if (configFileWrite.open(QFile::WriteOnly)) {
        QJsonDocument doc(configObj);
        configFileWrite.write(doc.toJson());
        configFileWrite.close();
    }
}

void MainWindow::syncParameterControls() {
    if (!m_parameterControlNode) return;
    for (const auto& binding : m_parameterControlBindings) {
        for (const auto& port : m_parameterControlNode->getControlPorts()) {
            if (port.index == binding.index) {
                binding.setValue(port.value);
                break;
            }
        }
    }
}

void MainWindow::updateCPUStatus() {
    static int parameterTickCounter = 0;
    if (parameterTickCounter++ >= 2) {
        parameterTickCounter = 0;
        syncParameterControls();
    }
    // Update CPU load less frequently (every 15 ticks, i.e., ~450ms) to prevent erratic jumping
    static int cpuTickCounter = 0;
    if (cpuTickCounter++ >= 15) {
        cpuTickCounter = 0;
        float cpu = m_engine.getCPULoad();
        m_cpuBar->setValue(static_cast<int>(cpu));
    }
    
    // Update Input and Output peak level meters (linear peak 0..100) with smooth decay
    float inPeak = m_engine.getInputPeak();
    float outPeak = m_engine.getOutputPeak();
    
    if (inPeak > m_inputLevelDecay) {
        m_inputLevelDecay = inPeak;
    } else {
        m_inputLevelDecay *= 0.94f;
        if (m_inputLevelDecay < 0.001f) m_inputLevelDecay = 0.0f;
    }
    
    if (outPeak > m_outputLevelDecay) {
        m_outputLevelDecay = outPeak;
    } else {
        m_outputLevelDecay *= 0.94f;
        if (m_outputLevelDecay < 0.001f) m_outputLevelDecay = 0.0f;
    }
    
    auto meterValue = [](float linearPeak) {
        const float db = 20.0f * std::log10(std::max(linearPeak, 0.001f));
        return qRound(std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f) * 100.0f);
    };
    const int inputValue = meterValue(m_inputLevelDecay);
    const int outputValue = meterValue(m_outputLevelDecay);
    m_inputMeter->setValue(inputValue);
    m_outputMeter->setValue(outputValue);
    if (m_inputPopupMeter && m_inputPopupMeter->isVisible()) m_inputPopupMeter->setValue(inputValue);
    if (m_outputPopupMeter && m_outputPopupMeter->isVisible()) m_outputPopupMeter->setValue(outputValue);
}

void MainWindow::onInputGainChanged(int value) {
    m_engine.setInputGain(static_cast<float>(value));
    m_inputGainLabel->setText(QString::number(value) + " dB");
    saveConfigSettings();
}

void MainWindow::onOutputGainChanged(int value) {
    m_engine.setOutputGain(static_cast<float>(value));
    m_outputGainLabel->setText(QString::number(value) + " dB");
    saveConfigSettings();
}

void MainWindow::savePresetToFile(const QString& path) {
    QJsonObject presetObj;
    presetObj["formatVersion"] = 4;
    
    QJsonArray nodesArray;
    for (int r = 0; r < NodeCanvas::NUM_ROWS; ++r) {
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
            auto node = m_canvas->getPluginAt(r, c);
            if (node) {
                QJsonObject nodeObj;
                nodeObj["id"] = QString::fromStdString(node->uniqueId);
                nodeObj["name"] = QString::fromStdString(node->getName());
                nodeObj["uri"] = QString::fromStdString(node->getPluginURI());
                nodeObj["type"] = node->getType() == NodeType::LV2Plugin ? "LV2Plugin" : (node->getType() == NodeType::CLAPPlugin ? "CLAPPlugin" : "VST3Plugin");
                nodeObj["row"] = r;
                nodeObj["col"] = c;
                nodeObj["bypassed"] = node->isBypassed();
                nodeObj["model_file_path"] = QString::fromStdString(node->getModelFilePath());
                nodeObj["model_display_name"] = QString::fromStdString(node->getModelDisplayName());
                nodeObj["model_source_url"] = QString::fromStdString(node->getModelSourceUrl());
                
                // Serialize file properties
                QJsonObject filePropsObj;
                for (const auto& fp : node->getFileProperties()) {
                    filePropsObj[QString::fromStdString(fp.uri)] = QString::fromStdString(fp.fileValue);
                }
                nodeObj["file_properties"] = filePropsObj;
                
                // Serialize model variants
                QJsonArray varsArray;
                for (const auto& var : node->getModelVariants()) {
                    QJsonObject vObj;
                    vObj["name"] = QString::fromStdString(var.name);
                    vObj["url"] = QString::fromStdString(var.url);
                    vObj["local_path"] = QString::fromStdString(var.localPath);
                    varsArray.append(vObj);
                }
                nodeObj["model_variants"] = varsArray;
                
                QJsonArray paramsArray;
                for (const auto& param : node->getControlPorts()) {
                    if (!param.isOutput) {
                        QJsonObject pObj;
                        pObj["index"] = static_cast<int>(param.index);
                        pObj["value"] = param.value;
                        paramsArray.append(pObj);
                    }
                }
                nodeObj["parameters"] = paramsArray;
                nodesArray.append(nodeObj);
            }
        }
    }
    auto serializeBranch = [this](int row) {
        QJsonObject branch;
        branch["hasSplitSection"] = m_canvas->hasSplitSection(row);
        branch["parentRow"] = m_canvas->getSplitParentRow(row);
        branch["splitMode"] = m_canvas->getSplitMode(row) == GridRow::SplitMode::AB ? "ab" : "copy";
        branch["splitPosition"] = static_cast<double>(m_canvas->getSplitPosition(row));
        branch["mainInputEnabled"] = m_canvas->isMainInputEnabled(row);
        branch["mainMix"] = static_cast<double>(m_canvas->getMainMix(row));
        branch["name"] = m_canvas->getBranchName(row);
        branch["splitAfterNodeId"] = QString::fromStdString(m_canvas->getSplitAnchor(row));
        branch["mergeBeforeNodeId"] = QString::fromStdString(m_canvas->getMergeAnchor(row));
        branch["mix"] = static_cast<double>(m_canvas->getMix(row));
        branch["pan"] = static_cast<double>(m_canvas->getPan(row));
        branch["enabled"] = m_canvas->isBranchEnabled(row);
        branch["polarityInverted"] = m_canvas->isPolarityInverted(row);
        return branch;
    };
    QJsonObject routing;
    routing["model"] = "nestedSplitSections5";
    QJsonArray branchesArray;
    for (int r : {0, 1, 3, 4}) {
        branchesArray.append(serializeBranch(r));
    }
    routing["branches"] = branchesArray;
    presetObj["routing"] = routing;

    presetObj["nodes"] = nodesArray;
    
    QJsonDocument doc(presetObj);
    QFile file(path);
    if (file.open(QFile::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void MainWindow::loadPresetFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;
    
    m_isLoadingPreset = true;

    // The selected node may belong to the previous pedalboard. Clear its controls
    // before removing canvas nodes so stale plugin parameters cannot remain shown.
    m_parameterControlNode.reset();
    m_parameterControlBindings.clear();
    m_activePluginPresetNodeId.clear();
    m_activePluginPresetName.clear();
    showPluginControls(nullptr);
    
    QJsonObject presetObj = doc.object();
    
    m_canvas->beginRoutingUpdate();
    m_canvas->clearCanvas();
    
    QJsonArray nodesArray = presetObj["nodes"].toArray();
    for (int i = 0; i < nodesArray.size(); ++i) {
        QJsonObject nObj = nodesArray[i].toObject();
        std::string id = nObj["id"].toString().toStdString();
        std::string uri = nObj["uri"].toString().toStdString();
        std::string typeStr = nObj["type"].toString().toStdString();
        int row = nObj["row"].toInt();
        int col = nObj["col"].toInt();
        bool bypassed = nObj["bypassed"].toBool();
        
        std::shared_ptr<AudioNode> node;
        
        if (typeStr == "LV2Plugin") {
            const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
            LILV_FOREACH(plugins, j, plugins) {
                const LilvPlugin* p = lilv_plugins_get(plugins, j);
                const LilvNode* uriNode = lilv_plugin_get_uri(p);
                std::string puri = lilv_node_as_string(uriNode);
                if (puri == uri) {
                    node = std::make_shared<LV2PluginNode>(m_lilvWorld, p);
                    break;
                }
            }
        } else if (typeStr == "CLAPPlugin") {
            std::string path = uri;
            uint32_t idx = 0;
            auto colonPos = path.rfind(':');
            if (colonPos != std::string::npos && colonPos > path.find(".clap")) {
                idx = std::stoul(path.substr(colonPos + 1));
                path = path.substr(0, colonPos);
            }
            node = std::make_shared<CLAPPluginNode>(path, idx);
        } else if (typeStr == "VST3Plugin") {
            if (uri == "builtin:bypass") {
                node = std::make_shared<BypassNode>();
            } else if (uri.find(".clap") != std::string::npos) {
                std::string path = uri;
                uint32_t idx = 0;
                auto colonPos = path.rfind(':');
                if (colonPos != std::string::npos && colonPos > path.find(".clap")) {
                    idx = std::stoul(path.substr(colonPos + 1));
                    path = path.substr(0, colonPos);
                }
                node = std::make_shared<CLAPPluginNode>(path, idx);
            } else {
                node = std::make_shared<VST3PluginNode>(uri);
            }
        }
        
        if (node) {
            node->uniqueId = id;
            node->setBypassed(bypassed);
            
            QString modelFilePath = nObj["model_file_path"].toString();
            if (!modelFilePath.isEmpty()) {
                m_engine.suspendProcessing();
                node->loadModelFile(modelFilePath.toStdString());
                m_engine.resumeProcessing();
            }
            node->setModelDisplayName(nObj["model_display_name"].toString().toStdString());
            node->setModelSourceUrl(nObj["model_source_url"].toString().toStdString());
            
            QJsonObject filePropsObj = nObj["file_properties"].toObject();
            for (auto it = filePropsObj.constBegin(); it != filePropsObj.constEnd(); ++it) {
                std::string u = it.key().toStdString();
                std::string val = it.value().toString().toStdString();
                node->setFileProperty(u, val);
            }
            
            QJsonArray paramsArray = nObj["parameters"].toArray();
            for (int j = 0; j < paramsArray.size(); ++j) {
                QJsonObject pObj = paramsArray[j].toObject();
                node->setParameter(pObj["index"].toInt(), pObj["value"].toDouble());
            }
            
            // Deserialize model variants
            QJsonArray varsArray = nObj["model_variants"].toArray();
            std::vector<AudioNode::ModelVariant> vars;
            for (int j = 0; j < varsArray.size(); ++j) {
                QJsonObject vObj = varsArray[j].toObject();
                AudioNode::ModelVariant var;
                var.name = vObj["name"].toString().toStdString();
                var.url = vObj["url"].toString().toStdString();
                var.localPath = vObj["local_path"].toString().toStdString();
                vars.push_back(var);
            }
            node->setModelVariants(vars);
            
            m_canvas->insertPluginAt(row, col, node);
        }
    }

    const QJsonObject routing = presetObj["routing"].toObject();
    auto restoreBranch = [this](int row, const QJsonObject& branch, bool isLegacy) {
        bool containsPlugins = false;
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) containsPlugins = containsPlugins || static_cast<bool>(m_canvas->getPluginAt(row, c));
        m_canvas->setSplitSectionPresent(row, branch["hasSplitSection"].toBool(branch["enabled"].toBool(false) || containsPlugins));
        int parent = branch["parentRow"].toInt(1);
        if (isLegacy) {
            if (parent == 1) parent = 2;
            else if (parent == 0) parent = 1;
            else if (parent == 2) parent = 3;
        }
        m_canvas->setSplitParentRow(row, parent);
        m_canvas->setSplitMode(row, branch["splitMode"].toString() == "ab"
            ? GridRow::SplitMode::AB : GridRow::SplitMode::Copy);
        m_canvas->setSplitPosition(row, static_cast<float>(branch["splitPosition"].toDouble(0.0)));
        m_canvas->setMainInputEnabled(row, branch["mainInputEnabled"].toBool(true));
        m_canvas->setMainMix(row, static_cast<float>(branch["mainMix"].toDouble(1.0)));
        m_canvas->setBranchName(row, branch["name"].toString());
        m_canvas->setSplitAnchor(row, branch["splitAfterNodeId"].toString().toStdString());
        m_canvas->setMergeAnchor(row, branch["mergeBeforeNodeId"].toString().toStdString());
        m_canvas->setMix(row, static_cast<float>(branch["mix"].toDouble(1.0)));
        m_canvas->setPan(row, static_cast<float>(branch["pan"].toDouble(0.0)));
        m_canvas->setPolarityInverted(row, branch["polarityInverted"].toBool(false));
        m_canvas->setBranchEnabled(row, branch["enabled"].toBool(false));
    };
    if (!routing.isEmpty()) {
        const bool legacyMainOutputEnabled = routing["mainOutputEnabled"].toBool(true);
        m_canvas->setMainOutputEnabled(true);
        if (routing["model"].toString() == "nestedSplitSections5") {
            QJsonArray branches = routing["branches"].toArray();
            int bIdx = 0;
            for (int r : {0, 1, 3, 4}) {
                if (bIdx < branches.size()) {
                    restoreBranch(r, branches[bIdx].toObject(), false);
                    bIdx++;
                }
            }
        } else {
            restoreBranch(1, routing["branchA"].toObject(), true);
            restoreBranch(3, routing["branchB"].toObject(), true);
        }
    } else {
        // Version 1 presets used main-row columns.
        m_canvas->setSplitCol(1, presetObj["branch0SplitCol"].toInt(-1));
        m_canvas->setMergeCol(1, presetObj["branch0MergeCol"].toInt(-1));
        m_canvas->setMix(1, static_cast<float>(presetObj["branch0Mix"].toDouble(1.0)));
        m_canvas->setPan(1, static_cast<float>(presetObj["branch0Pan"].toDouble(0.0)));
        m_canvas->setBranchEnabled(1, presetObj["branch0Enabled"].toBool(m_canvas->isBranchEnabled(1)));
        m_canvas->setSplitSectionPresent(1, m_canvas->isBranchEnabled(1));
        m_canvas->setSplitCol(3, presetObj["branch2SplitCol"].toInt(-1));
        m_canvas->setMergeCol(3, presetObj["branch2MergeCol"].toInt(-1));
        m_canvas->setMix(3, static_cast<float>(presetObj["branch2Mix"].toDouble(1.0)));
        m_canvas->setPan(3, static_cast<float>(presetObj["branch2Pan"].toDouble(0.0)));
        m_canvas->setBranchEnabled(3, presetObj["branch2Enabled"].toBool(m_canvas->isBranchEnabled(3)));
        m_canvas->setSplitSectionPresent(3, m_canvas->isBranchEnabled(3));
    }
    m_canvas->endRoutingUpdate();
    
    // Layout and selection notifications posted while nodes are restored can arrive
    // after this function returns. Keep them from being treated as user edits.
    QTimer::singleShot(0, this, [this]() {
        m_isLoadingPreset = false;
        setUnsavedChanges(false);
    });
}

void MainWindow::onNodeSelected(std::shared_ptr<AudioNode> node) {
    showPluginControls(node);
}

static std::unordered_map<std::string, LV2_URID> s_uiUridMap;
static std::mutex s_uiUridMutex;
static LV2_URID s_uiNextUrid = 1;

static LV2_URID ui_map_uri(LV2_URID_Map_Handle handle, const char* uri) {
    std::lock_guard<std::mutex> lock(s_uiUridMutex);
    auto it = s_uiUridMap.find(uri);
    if (it != s_uiUridMap.end()) return it->second;
    LV2_URID urid = s_uiNextUrid++;
    s_uiUridMap[uri] = urid;
    return urid;
}

static const char* ui_unmap_uri(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
    std::lock_guard<std::mutex> lock(s_uiUridMutex);
    for (const auto& pair : s_uiUridMap) {
        if (pair.second == urid) return pair.first.c_str();
    }
    return nullptr;
}

static qreal x11UiScaleFactor() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return 1.0;

    qreal scale = 1.0;
    XrmInitialize();
    if (const char* resources = XResourceManagerString(display)) {
        XrmDatabase database = XrmGetStringDatabase(resources);
        if (database) {
            char* type = nullptr;
            XrmValue value{};
            if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
                char* end = nullptr;
                const qreal dpi = std::strtod(value.addr, &end);
                if (end != value.addr && dpi > 96.0) scale = dpi / 96.0;
            }
            XrmDestroyDatabase(database);
        }
    }
    XCloseDisplay(display);
    return scale;
}

class ExternalPluginUIWindow : public QObject {
public:
    ExternalPluginUIWindow(LV2PluginNode* node, const LilvUI* ui, bool x11Ui, WId parentWindow, QObject* parent = nullptr)
        : QObject(parent), m_node(node), m_parentWindow(parentWindow) {
        m_serverName = "PedalBoard-external-ui-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!m_server.listen(m_serverName)) return;

        connect(&m_server, &QLocalServer::newConnection, this, [this]() {
            m_socket = m_server.nextPendingConnection();
            if (!m_socket) return;
            connect(m_socket, &QLocalSocket::readyRead, this, &ExternalPluginUIWindow::readMessages);
            sendPortValues();
            m_syncTimer.start(100);
        });
        connect(&m_syncTimer, &QTimer::timeout, this, &ExternalPluginUIWindow::sendPortValues);

        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::ForwardedChannels);
        connect(m_process, &QProcess::finished, this, [this]() {
            m_syncTimer.stop();
            deleteLater();
        });
        m_process->start(
            QCoreApplication::applicationFilePath(),
            {x11Ui ? "--x11-ui-helper" : "--gtk-ui-helper", m_serverName, QString::fromStdString(node->getPluginURI()),
             QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(ui))), QString::number(m_parentWindow)});
        m_valid = m_process->waitForStarted(3000);
    }

    ~ExternalPluginUIWindow() override {
        m_syncTimer.stop();
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            m_process->waitForFinished(1000);
        }
        m_server.close();
        QLocalServer::removeServer(m_serverName);
    }

    bool isValid() const { return m_valid; }

private:
    void sendPortValues() {
        if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState || !m_node) return;
        
        // 1. Send parameter control values
        for (const auto& param : m_node->getControlPorts()) {
            QByteArray message;
            QDataStream stream(&message, QIODevice::WriteOnly);
            stream << quint8('P') << quint32(param.index) << param.value;
            m_socket->write(message);
        }

        // 2. Send file property values
        for (const auto& fp : m_node->getFileProperties()) {
            if (!fp.fileValue.empty()) {
                QByteArray message;
                QDataStream stream(&message, QIODevice::WriteOnly);
                stream << quint8('S') << QString::fromStdString(fp.uri) << QString::fromStdString(fp.fileValue);
                m_socket->write(message);
            }
        }

        m_socket->flush();
    }

    void readMessages() {
        m_incoming += m_socket->readAll();
        QDataStream stream(&m_incoming, QIODevice::ReadOnly);
        while (true) {
            stream.startTransaction();
            quint8 command = 0;
            stream >> command;
            if (command == 'P') {
                quint32 index = 0;
                float value = 0.0f;
                stream >> index >> value;
                if (!stream.commitTransaction()) break;
                m_node->setParameter(index, value);
            } else if (command == 'A') {
                quint32 index = 0;
                quint32 protocol = 0;
                QByteArray buffer;
                stream >> index >> protocol >> buffer;
                if (!stream.commitTransaction()) break;

                if (m_node->handlePortEvent(index, protocol, buffer.constData(), buffer.size())) {
                    MainWindow* mw = qobject_cast<MainWindow*>(parent());
                    if (mw) {
                        mw->setUnsavedChanges(true);
                        mw->saveConfigSettings();
                        mw->showPluginControls(std::shared_ptr<AudioNode>(m_node, [](AudioNode*){}));
                    }
                }
            } else {
                stream.rollbackTransaction();
                break;
            }
        }
        m_incoming.remove(0, static_cast<int>(stream.device()->pos()));
    }

    LV2PluginNode* m_node = nullptr;
    QLocalServer m_server;
    QLocalSocket* m_socket = nullptr;
    QProcess* m_process = nullptr;
    QTimer m_syncTimer;
    QString m_serverName;
    QByteArray m_incoming;
    bool m_valid = false;
    WId m_parentWindow = 0;
};

class VST3PluginUIWindow : public QDialog {
public:
    VST3PluginUIWindow(VST3PluginNode* node, QWidget* parent = nullptr)
        : QDialog(parent), m_node(node), m_plugView(node->getPlugView()), m_attached(false), m_resizable(false), m_x11Container(0), m_dpy(nullptr), m_ownsDisplay(false) {
        
        setWindowTitle(QString::fromStdString(node->getName()) + " - GUI");
        setAttribute(Qt::WA_DeleteOnClose, true);
        setWindowFlag(Qt::Tool, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setWindowModality(Qt::NonModal);
        
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_dpy = nullptr;
        auto* x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11App) {
            m_dpy = x11App->display();
        }
        if (!m_dpy) {
            std::cerr << "VST3Host: WARNING - Could not get Qt's X11 Display, falling back to XOpenDisplay." << std::endl;
            m_dpy = XOpenDisplay(nullptr);
            m_ownsDisplay = true;
        }
        
        Steinberg::ViewRect size;
        if (m_plugView && m_plugView->getSize(&size) == Steinberg::kResultOk) {
            m_nativeWidth = size.right - size.left;
            m_nativeHeight = size.bottom - size.top;
        } else {
            m_nativeWidth = 800;
            m_nativeHeight = 600;
        }

        double ratio = devicePixelRatioF();
        if (ratio <= 0.0) ratio = 1.0;
        int logicalW = std::round(m_nativeWidth / ratio);
        int logicalH = std::round(m_nativeHeight / ratio);
        
        m_resizable = (m_plugView && m_plugView->canResize() == Steinberg::kResultOk);
        if (m_resizable) {
            resize(logicalW, logicalH);
        } else {
            setFixedSize(logicalW, logicalH);
        }

        // Create raw X11 child window as a child of the QDialog's X11 window
        WId dialogWinId = this->winId();
        int screen = DefaultScreen(m_dpy);
        m_x11Container = XCreateSimpleWindow(
            m_dpy,
            (Window)dialogWinId,
            0, 0,
            m_nativeWidth, m_nativeHeight,
            0,
            BlackPixel(m_dpy, screen),
            BlackPixel(m_dpy, screen)
        );

        XSelectInput(m_dpy, m_x11Container, SubstructureNotifyMask | StructureNotifyMask);
        XMapWindow(m_dpy, m_x11Container);
        XFlush(m_dpy);

        // Wrap raw X11 container into Qt's createWindowContainer to prevent backing store overpaint
        QWindow* foreignWin = QWindow::fromWinId(m_x11Container);
        m_containerWidget = QWidget::createWindowContainer(foreignWin, this);
        layout->addWidget(m_containerWidget);
    }
    
    ~VST3PluginUIWindow() override {
        std::cout << "VST3Host: ~VST3PluginUIWindow() destructor called!" << std::endl;
        if (m_plugView) {
            if (m_attached) {
                m_plugView->removed();
            }
            m_node->releasePlugView();
        }
        if (m_dpy && m_x11Container) {
            XDestroyWindow(m_dpy, m_x11Container);
            XFlush(m_dpy);
        }
        if (m_ownsDisplay && m_dpy) {
            XCloseDisplay(m_dpy);
        }
    }
    
protected:
    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        if (m_plugView && m_attached) {
            double ratio = devicePixelRatioF();
            if (ratio <= 0.0) ratio = 1.0;
            
            Steinberg::ViewRect rect;
            rect.left = 0;
            rect.top = 0;
            rect.right = std::round(width() * ratio);
            rect.bottom = std::round(height() * ratio);
            
            m_plugView->onSize(&rect);
        }
    }

    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        if (m_plugView && !m_attached && m_dpy) {
            QTimer::singleShot(100, this, [this]() {
                if (m_plugView && !m_attached && m_dpy) {
                    bool x11Supported = (m_plugView->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) == Steinberg::kResultOk);
                    std::cout << "VST3Host: isPlatformTypeSupported(X11Embed) = " << (x11Supported ? "true" : "false") << std::endl;
                    
                    if (x11Supported) {
                        Steinberg::tresult attachRes = m_plugView->attached((void*)m_x11Container, Steinberg::kPlatformTypeX11EmbedWindowID);
                        std::cout << "VST3Host: attached returned: " << attachRes << std::endl;
                        if (attachRes == Steinberg::kResultOk) {
                            m_attached = true;
                            sendXEmbedEmbeddedNotify(m_x11Container);
                            
                            Steinberg::ViewRect rect;
                            rect.left = 0;
                            rect.top = 0;
                            rect.right = m_nativeWidth;
                            rect.bottom = m_nativeHeight;
                            m_plugView->onSize(&rect);
                        }
                    }
                }
            });
        }
    }
    
private:
    void sendXEmbedEmbeddedNotify(Window containerId) {
        if (!m_dpy) return;
        
        Window root;
        Window parent;
        Window* children = nullptr;
        unsigned int numChildren = 0;
        
        XMapWindow(m_dpy, containerId);
        XMapRaised(m_dpy, containerId);
        
        if (XQueryTree(m_dpy, containerId, &root, &parent, &children, &numChildren) && children) {
            for (unsigned int i = 0; i < numChildren; ++i) {
                Window child = children[i];
                
                XEvent ev;
                std::memset(&ev, 0, sizeof(ev));
                ev.xclient.type = ClientMessage;
                ev.xclient.window = child;
                ev.xclient.message_type = XInternAtom(m_dpy, "_XEMBED", False);
                ev.xclient.format = 32;
                ev.xclient.data.l[0] = CurrentTime;
                ev.xclient.data.l[1] = 0; // XEMBED_EMBEDDED_NOTIFY = 0
                ev.xclient.data.l[2] = 0; // detail = 0
                ev.xclient.data.l[3] = (long)containerId;
                ev.xclient.data.l[4] = 0;
                XSendEvent(m_dpy, child, False, NoEventMask, &ev);
                
                ev.xclient.data.l[1] = 1; // XEMBED_WINDOW_ACTIVATE
                XSendEvent(m_dpy, child, False, NoEventMask, &ev);
                
                ev.xclient.data.l[1] = 4; // XEMBED_FOCUS_IN
                XSendEvent(m_dpy, child, False, NoEventMask, &ev);
                
                XMapWindow(m_dpy, child);
            }
            XFree(children);
        }
        
        XFlush(m_dpy);
    }
    
private:
    VST3PluginNode* m_node;
    Steinberg::IPlugView* m_plugView;
    bool m_attached;
    bool m_resizable;
    int m_nativeWidth;
    int m_nativeHeight;
    Window m_x11Container;
    QWidget* m_containerWidget = nullptr;
    Display* m_dpy;
    bool m_ownsDisplay;
};

class CLAPPluginUIWindow : public QDialog {
public:
    CLAPPluginUIWindow(CLAPPluginNode* node, QWidget* parent = nullptr)
        : QDialog(parent), m_node(node), m_attached(false), m_x11Container(0), m_dpy(nullptr), m_ownsDisplay(false) {
        
        setWindowTitle(QString::fromStdString(node->getName()) + " - GUI");
        setAttribute(Qt::WA_DeleteOnClose, true);
        setWindowFlag(Qt::Tool, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setWindowModality(Qt::NonModal);
        
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_dpy = nullptr;
        auto* x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11App) {
            m_dpy = x11App->display();
        }
        if (!m_dpy) {
            m_dpy = XOpenDisplay(nullptr);
            m_ownsDisplay = true;
        }

        const clap_plugin_t* plugin = node->getClapPlugin();
        const clap_plugin_gui_t* extGui = node->getClapGuiExtension();
        
        uint32_t width = 800;
        uint32_t height = 600;
        if (extGui && plugin) {
            if (extGui->is_api_supported && extGui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false)) {
                if (extGui->create) {
                    extGui->create(plugin, CLAP_WINDOW_API_X11, false);
                }
                if (extGui->get_size) {
                    extGui->get_size(plugin, &width, &height);
                }
            }
        }

        m_nativeWidth = (width > 0) ? width : 800;
        m_nativeHeight = (height > 0) ? height : 600;

        double ratio = devicePixelRatioF();
        if (ratio <= 0.0) ratio = 1.0;
        int logicalW = std::round(m_nativeWidth / ratio);
        int logicalH = std::round(m_nativeHeight / ratio);
        
        resize(logicalW, logicalH);

        WId dialogWinId = this->winId();
        int screen = DefaultScreen(m_dpy);
        m_x11Container = XCreateSimpleWindow(
            m_dpy,
            (Window)dialogWinId,
            0, 0,
            m_nativeWidth, m_nativeHeight,
            0,
            BlackPixel(m_dpy, screen),
            BlackPixel(m_dpy, screen)
        );

        XSelectInput(m_dpy, m_x11Container, SubstructureNotifyMask | StructureNotifyMask);
        XMapWindow(m_dpy, m_x11Container);
        XFlush(m_dpy);

        QWindow* foreignWin = QWindow::fromWinId(m_x11Container);
        m_containerWidget = QWidget::createWindowContainer(foreignWin, this);
        layout->addWidget(m_containerWidget);

        m_node->setResizeCallback([this](uint32_t w, uint32_t h) {
            QMetaObject::invokeMethod(this, [this, w, h]() {
                if (w == 0 || h == 0) return;
                m_nativeWidth = w;
                m_nativeHeight = h;
                double ratio = devicePixelRatioF();
                if (ratio <= 0.0) ratio = 1.0;
                int logicalW = std::round(m_nativeWidth / ratio);
                int logicalH = std::round(m_nativeHeight / ratio);
                setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                setMinimumSize(300, 200);
                resize(logicalW, logicalH);
                updateChildWindows(m_nativeWidth, m_nativeHeight);
            }, Qt::QueuedConnection);
        });
    }

    ~CLAPPluginUIWindow() override {
        m_node->setResizeCallback(nullptr);
        const clap_plugin_t* plugin = m_node->getClapPlugin();
        const clap_plugin_gui_t* extGui = m_node->getClapGuiExtension();
        if (extGui && plugin && m_attached) {
            extGui->hide(plugin);
            extGui->destroy(plugin);
        }
        if (m_dpy && m_x11Container) {
            XDestroyWindow(m_dpy, m_x11Container);
            XFlush(m_dpy);
        }
        if (m_ownsDisplay && m_dpy) {
            XCloseDisplay(m_dpy);
        }
        m_dpy = nullptr;
    }

    void updateChildWindows(uint32_t width, uint32_t height) {
        if (!m_dpy || !m_x11Container) return;
        XMoveResizeWindow(m_dpy, m_x11Container, 0, 0, width, height);

        Window root;
        Window parent;
        Window* children = nullptr;
        unsigned int numChildren = 0;
        if (XQueryTree(m_dpy, m_x11Container, &root, &parent, &children, &numChildren) && children) {
            for (unsigned int i = 0; i < numChildren; ++i) {
                Window child = children[i];
                XMoveResizeWindow(m_dpy, child, 0, 0, width, height);
                XMapWindow(m_dpy, child);
            }
            XFree(children);
        }
        XFlush(m_dpy);
    }

protected:
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        if (!m_attached && m_dpy) {
            const clap_plugin_t* plugin = m_node->getClapPlugin();
            const clap_plugin_gui_t* extGui = m_node->getClapGuiExtension();
            if (extGui && plugin && !m_attached) {
                clap_window_t window{ CLAP_WINDOW_API_X11, { (void*)m_x11Container } };
                if (extGui->set_parent(plugin, &window)) {
                    extGui->show(plugin);
                    m_attached = true;

                    double ratio = devicePixelRatioF();
                    if (ratio <= 0.0) ratio = 1.0;
                    int logicalW = std::round(m_nativeWidth / ratio);
                    int logicalH = std::round(m_nativeHeight / ratio);
                    setMinimumSize(300, 200);
                    resize(logicalW, logicalH);
                    updateChildWindows(m_nativeWidth, m_nativeHeight);
                }
            }
        }
    }

    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        if (m_containerWidget && m_dpy && m_x11Container) {
            double ratio = devicePixelRatioF();
            if (ratio <= 0.0) ratio = 1.0;
            uint32_t physW = std::round(width() * ratio);
            uint32_t physH = std::round(height() * ratio);

            const clap_plugin_t* plugin = m_node->getClapPlugin();
            const clap_plugin_gui_t* extGui = m_node->getClapGuiExtension();
            if (extGui && plugin && m_attached) {
                if (extGui->adjust_size) {
                    extGui->adjust_size(plugin, &physW, &physH);
                }
                if (extGui->set_size) {
                    extGui->set_size(plugin, physW, physH);
                }
            }
            updateChildWindows(physW, physH);
        }
    }

private:
    CLAPPluginNode* m_node = nullptr;
    Display* m_dpy = nullptr;
    Window m_x11Container = 0;
    QWidget* m_containerWidget = nullptr;
    bool m_ownsDisplay = false;
    bool m_attached = false;
    uint32_t m_nativeWidth = 800;
    uint32_t m_nativeHeight = 600;
};

class PluginUIWindow : public QDialog {
public:
    PluginUIWindow(LV2PluginNode* node, const LilvUI* ui, QWidget* parent = nullptr)
        : QDialog(parent), m_node(node) {
        
        setWindowTitle(QString::fromStdString(node->getName()) + " - GUI");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::Tool, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setWindowModality(Qt::NonModal);
        
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        
        m_suilHost = suil_host_new(suil_port_write, nullptr, nullptr, nullptr);
        
        const char* containerType = "http://lv2plug.in/ns/extensions/ui#Qt6UI";
        const char* uiUri = lilv_node_as_uri(lilv_ui_get_uri(ui));
        
        const LilvNodes* classes = lilv_ui_get_classes(ui);
        std::string uiType;
        std::string fallbackType;
        unsigned bestSupport = UINT_MAX;
        LILV_FOREACH(nodes, i, classes) {
            const LilvNode* c = lilv_nodes_get(classes, i);
            std::string classUri = lilv_node_as_uri(c);
            if (fallbackType.empty()) fallbackType = classUri;
            unsigned support = suil_ui_supported(containerType, classUri.c_str());
            if (support > 0 && support < bestSupport) {
                uiType = classUri;
                bestSupport = support;
            }
        }

        if (uiType.empty()) uiType = fallbackType;
        if (uiType.empty()) return;
        const LilvNode* bundleNode = lilv_ui_get_bundle_uri(ui);
        const LilvNode* binaryNode = lilv_ui_get_binary_uri(ui);

        LilvNode* optionalFeature = lilv_new_uri(m_node->getLilvWorld(), LV2_CORE__optionalFeature);
        LilvNode* requiredFeature = lilv_new_uri(m_node->getLilvWorld(), LV2_CORE__requiredFeature);
        LilvNode* extensionData = lilv_new_uri(m_node->getLilvWorld(), LV2_CORE__extensionData);
        LilvNode* resizeFeature = lilv_new_uri(m_node->getLilvWorld(), LV2_UI__resize);
        const bool supportsResize =
            lilv_world_ask(m_node->getLilvWorld(), lilv_ui_get_uri(ui), optionalFeature, resizeFeature) ||
            lilv_world_ask(m_node->getLilvWorld(), lilv_ui_get_uri(ui), requiredFeature, resizeFeature) ||
            lilv_world_ask(m_node->getLilvWorld(), lilv_ui_get_uri(ui), extensionData, resizeFeature);
        lilv_node_free(optionalFeature);
        lilv_node_free(requiredFeature);
        lilv_node_free(extensionData);
        lilv_node_free(resizeFeature);
        
        char* bundlePath = lilv_file_uri_parse(lilv_node_as_uri(bundleNode), nullptr);
        char* binaryPath = binaryNode ? lilv_file_uri_parse(lilv_node_as_uri(binaryNode), nullptr) : nullptr;
        
        // UI instances may retain feature pointers, so these must outlive construction.
        m_uridMap = { nullptr, ui_map_uri };
        m_uiResize = {
            this,
            [](LV2UI_Feature_Handle handle, int width, int height) -> int {
                auto* self = static_cast<PluginUIWindow*>(handle);
                if (self) {
                    self->resizeUi(width, height);
                }
                return 0;
            }
        };
        m_resizeFeature.URI = LV2_UI__resize;
        m_resizeFeature.data = &m_uiResize;
        m_sampleRate = static_cast<float>(m_node->getSampleRate());
        m_blockLength = m_node->getMaxBlockSize();
        m_uiScaleFactor = uiType == LV2_UI__X11UI && supportsResize
            ? static_cast<float>(x11UiScaleFactor())
            : 1.0f;
        m_options[0] = {
            LV2_OPTIONS_INSTANCE,
            0,
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/parameters#sampleRate"),
            sizeof(float),
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Float"),
            &m_sampleRate
        };
        m_options[1] = {
            LV2_OPTIONS_INSTANCE,
            0,
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/core#sampleRate"),
            sizeof(float),
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Float"),
            &m_sampleRate
        };
        m_options[2] = {
            LV2_OPTIONS_INSTANCE,
            0,
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/buf-size#nominalBlockLength"),
            sizeof(int32_t),
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Int"),
            &m_blockLength
        };
        m_options[3] = {
            LV2_OPTIONS_INSTANCE,
            0,
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/buf-size#maxBlockLength"),
            sizeof(int32_t),
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Int"),
            &m_blockLength
        };
        m_options[4] = {
            LV2_OPTIONS_INSTANCE,
            0,
            ui_map_uri(nullptr, LV2_UI__scaleFactor),
            sizeof(float),
            ui_map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Float"),
            &m_uiScaleFactor
        };
        m_options[5] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
        m_optionsFeature.URI = LV2_OPTIONS__options;
        m_optionsFeature.data = m_options;
        m_mapFeature.URI = "http://lv2plug.in/ns/ext/urid#map";
        m_mapFeature.data = &m_uridMap;
        m_unmapFeature.URI = "http://lv2plug.in/ns/ext/urid#unmap";
        m_unmapFeature.data = &m_uridUnmap;

        m_features[0] = &m_mapFeature;
        m_features[1] = &m_unmapFeature;
        m_features[2] = &m_optionsFeature;
        m_features[3] = &m_resizeFeature;
        m_features[4] = nullptr;
        
        const std::string pluginUri = node->getPluginURI();
        
        m_suilInstance = suil_instance_new(
            m_suilHost,
            m_node,
            containerType,
            pluginUri.c_str(),
            uiUri,
            uiType.c_str(),
            bundlePath,
            binaryPath,
            m_features
        );
        
        lilv_free(bundlePath);
        if (binaryPath) lilv_free(binaryPath);
        
        if (m_suilInstance) {
            QWidget* widget = (QWidget*)suil_instance_get_widget(m_suilInstance);
            if (widget) {
                QSize nativeSize = widget->baseSize();
                if (!nativeSize.isValid() || nativeSize.isEmpty()) {
                    nativeSize = widget->minimumSizeHint();
                }
                if (!nativeSize.isValid() || nativeSize.isEmpty()) {
                    nativeSize = widget->sizeHint();
                }
                if (!nativeSize.isValid() || nativeSize.isEmpty()) {
                    nativeSize = QSize(640, 480);
                }

                layout->addWidget(widget);
                widget->show();
                m_uiWidget = widget;
                QTimer::singleShot(0, this, [this, widget, nativeSize]() {
                    widget->setMinimumSize(1, 1);
                    widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                    setMinimumSize(100, 100);
                    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                    resize(nativeSize);
                });
                for (const auto& param : m_node->getControlPorts()) {
                    if (!param.isOutput) {
                        suil_instance_port_event(m_suilInstance, param.index, sizeof(float), 0, &param.value);
                    }
                }
                m_portSyncTimer = new QTimer(this);
                connect(m_portSyncTimer, &QTimer::timeout, this, [this]() {
                    for (const auto& param : m_node->getControlPorts()) {
                        if (!param.isOutput) {
                            suil_instance_port_event(m_suilInstance, param.index, sizeof(float), 0, &param.value);
                        }
                    }
                });
                m_portSyncTimer->start(100);
                m_idleInterface = static_cast<const LV2UI_Idle_Interface*>(
                    suil_instance_extension_data(m_suilInstance, LV2_UI__idleInterface));
                if (m_idleInterface) {
                    m_idleTimer = new QTimer(this);
                    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
                        m_idleInterface->idle(suil_instance_get_handle(m_suilInstance));
                    });
                    m_idleTimer->start(16);
                }
            } else {
                suil_instance_free(m_suilInstance);
                m_suilInstance = nullptr;
            }
        }
        
        if (!m_suilInstance) {
            QLabel* errorLabel = new QLabel("Failed to load plugin GUI.\nUsing generic parameters sidebar instead.", this);
            errorLabel->setAlignment(Qt::AlignCenter);
            errorLabel->setStyleSheet("color: #888888; font-size: 12px; margin: 15px;");
            layout->addWidget(errorLabel);
            resize(320, 100);
        } else {
            layout->activate();
        }
    }
    
    ~PluginUIWindow() override {
        if (m_idleTimer) m_idleTimer->stop();
        if (m_portSyncTimer) m_portSyncTimer->stop();
        if (m_suilInstance) {
            suil_instance_free(m_suilInstance);
        }
        if (m_suilHost) {
            suil_host_free(m_suilHost);
        }
    }

    void resizeUi(int width, int height) {
        QMetaObject::invokeMethod(this, [this, width, height]() {
            if (width <= 0 || height <= 0) return;
            double ratio = devicePixelRatioF();
            if (ratio <= 0.0) ratio = 1.0;
            int logicalW = std::round(width / ratio);
            int logicalH = std::round(height / ratio);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            setMinimumSize(100, 100);
            resize(logicalW, logicalH);
            if (m_uiWidget) {
                m_uiWidget->resize(logicalW, logicalH);
            }
        }, Qt::QueuedConnection);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        if (m_uiWidget) {
            m_uiWidget->resize(width(), height());
        }
        if (m_suilInstance) {
            const LV2UI_Resize* resizeExt = static_cast<const LV2UI_Resize*>(
                suil_instance_extension_data(m_suilInstance, LV2_UI__resize));
            if (resizeExt && resizeExt->ui_resize) {
                double ratio = devicePixelRatioF();
                if (ratio <= 0.0) ratio = 1.0;
                int physW = std::round(width() * ratio);
                int physH = std::round(height() * ratio);
                resizeExt->ui_resize(suil_instance_get_handle(m_suilInstance), physW, physH);
            }
        }
    }

private:
    static void suil_port_write(
        SuilController controller,
        uint32_t       port_index,
        uint32_t       buffer_size,
        uint32_t       protocol,
        const void*    buffer) {
        auto* lv2Node = static_cast<LV2PluginNode*>(controller);
        if (lv2Node && protocol == 0 && buffer_size == sizeof(float)) {
            float val = *static_cast<const float*>(buffer);
            lv2Node->setParameter(port_index, val);
        }
    }

    LV2PluginNode* m_node;
    SuilHost* m_suilHost = nullptr;
    SuilInstance* m_suilInstance = nullptr;
    LV2_URID_Map m_uridMap{};
    LV2_URID_Unmap m_uridUnmap{};
    LV2_Feature m_mapFeature{};
    LV2_Feature m_unmapFeature{};
    LV2UI_Resize m_uiResize{};
    LV2_Feature m_resizeFeature{};
    LV2_Options_Option m_options[6] = {};
    LV2_Feature m_optionsFeature{};
    float m_sampleRate = 48000.0f;
    float m_uiScaleFactor = 1.0f;
    int32_t m_blockLength = 256;
    const LV2_Feature* m_features[5] = {};
    const LV2UI_Idle_Interface* m_idleInterface = nullptr;
    QTimer* m_idleTimer = nullptr;
    QTimer* m_portSyncTimer = nullptr;
    QWidget* m_uiWidget = nullptr;
};

static void clearLayoutContents(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
            delete item;
        } else if (QLayout* childLayout = item->layout()) {
            clearLayoutContents(childLayout);
            delete childLayout;
        } else {
            delete item;
        }
    }
}

void MainWindow::showPluginControls(std::shared_ptr<AudioNode> node) {
    // Clear parameters container
    m_parameterControlBindings.clear();
    m_parameterControlNode = node;
    clearLayoutContents(m_paramLayout);
    bool hasCustomUI = false;
    
    if (!node) {
        m_noParamLabel->show();
        return;
    }

    m_noParamLabel->hide();

    // Preset management bar
    auto* presetRow = new QWidget(m_paramContainer);
        auto* presetLayout = new QHBoxLayout(presetRow);
        presetLayout->setContentsMargins(0, 0, 0, 4);
        presetLayout->setSpacing(4);

        auto* presetCombo = new QComboBox(presetRow);
        presetCombo->setToolTip("Load a preset for this plugin");
        const QString activePreset = node->uniqueId == m_activePluginPresetNodeId ? m_activePluginPresetName : QString{};
        refreshPluginPresetList(node, presetCombo, activePreset);
        presetLayout->addWidget(presetCombo, 1);

        auto* saveButton = new QPushButton("Save", presetRow);
        auto* saveAsButton = new QPushButton("Save As", presetRow);
        auto* renameButton = new QPushButton("Rename", presetRow);
        auto* deleteButton = new QPushButton("Delete", presetRow);
        for (auto* button : {saveButton, saveAsButton, renameButton, deleteButton}) {
            button->setStyleSheet("QPushButton { padding: 4px 6px; }");
            presetLayout->addWidget(button);
        }
        m_paramLayout->addWidget(presetRow);

        connect(presetCombo, &QComboBox::activated, this, [this, node, presetCombo](int index) {
            const QString name = presetCombo->itemData(index).toString();
            if (name.isEmpty()) return;
            if (!loadPluginPreset(node, name)) {
                QMessageBox::warning(this, "Plugin Preset", "Could not load this plugin preset.");
            }
        });
        connect(saveButton, &QPushButton::clicked, this, [this, node, presetCombo]() {
            QString name = presetCombo->currentData().toString();
            if (name.isEmpty()) {
                bool accepted = false;
                name = QInputDialog::getText(this, "Save Plugin Preset", "Preset name:", QLineEdit::Normal, "", &accepted);
                if (!accepted) return;
            }
            if (!savePluginPreset(node, name)) {
                QMessageBox::warning(this, "Plugin Preset", "Could not save this plugin preset.");
                return;
            }
            refreshPluginPresetList(node, presetCombo, name.trimmed());
        });
        connect(saveAsButton, &QPushButton::clicked, this, [this, node, presetCombo]() {
            bool accepted = false;
            const QString name = QInputDialog::getText(this, "Save Plugin Preset As", "Preset name:", QLineEdit::Normal, "", &accepted);
            if (!accepted) return;
            if (!savePluginPreset(node, name)) {
                QMessageBox::warning(this, "Plugin Preset", "Could not save this plugin preset.");
                return;
            }
            refreshPluginPresetList(node, presetCombo, name.trimmed());
        });
        connect(renameButton, &QPushButton::clicked, this, [this, node, presetCombo]() {
            const QString oldName = presetCombo->currentData().toString();
            if (oldName.isEmpty()) return;
            bool accepted = false;
            const QString newName = QInputDialog::getText(
                this, "Rename Plugin Preset", "New name:", QLineEdit::Normal, oldName, &accepted);
            if (!accepted || !isValidPluginPresetName(newName)) return;
            QDir directory(pluginPresetDirectory(*node));
            if (!directory.rename(oldName + ".json", newName.trimmed() + ".json")) {
                QMessageBox::warning(this, "Plugin Preset", "Could not rename this plugin preset.");
                return;
            }
            refreshPluginPresetList(node, presetCombo, newName.trimmed());
        });
        connect(deleteButton, &QPushButton::clicked, this, [this, node, presetCombo]() {
            const QString name = presetCombo->currentData().toString();
            if (name.isEmpty()) return;
            if (QMessageBox::question(this, "Delete Plugin Preset", "Delete '" + name + "'?") != QMessageBox::Yes) return;
            if (!QFile::remove(QDir(pluginPresetDirectory(*node)).filePath(name + ".json"))) {
                QMessageBox::warning(this, "Plugin Preset", "Could not delete this plugin preset.");
                return;
            }
            refreshPluginPresetList(node, presetCombo);
        });

        auto* presetSeparator = new QFrame(m_paramContainer);
        presetSeparator->setFrameShape(QFrame::HLine);
        presetSeparator->setStyleSheet("background-color: #333333; margin-bottom: 4px;");
        m_paramLayout->addWidget(presetSeparator);
    
    // Add "Open Graphical UI..." button if plugin has UIs
    auto* lv2Node = dynamic_cast<LV2PluginNode*>(node.get());
    if (lv2Node) {
        const LilvUIs* uis = lilv_plugin_get_uis(lv2Node->getLilvPlugin());
        const LilvUI* uiToOpen = nullptr;
        unsigned bestSupport = UINT_MAX;
        if (uis) {
            LILV_FOREACH(uis, i, uis) {
                const LilvUI* candidate = lilv_uis_get(uis, i);
                const QString candidateUri = QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(candidate)));
                const bool candidateNeedsInstance = candidateUri.endsWith("-req");
                const bool selectedNeedsInstance = uiToOpen &&
                    QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(uiToOpen))).endsWith("-req");
                if (!uiToOpen || (bestSupport == UINT_MAX && selectedNeedsInstance && !candidateNeedsInstance)) {
                    uiToOpen = candidate;
                }
                const LilvNodes* classes = lilv_ui_get_classes(candidate);
                LILV_FOREACH(nodes, j, classes) {
                    const LilvNode* type = lilv_nodes_get(classes, j);
                    unsigned support = suil_ui_supported(
                        "http://lv2plug.in/ns/extensions/ui#Qt6UI",
                        lilv_node_as_uri(type));
                    if (support > 0 && support < bestSupport) {
                        uiToOpen = candidate;
                        bestSupport = support;
                    }
                }
            }
        }
        if (uiToOpen) {
            bool isGtkUi = false;
            bool isX11Ui = false;
            const LilvNodes* selectedClasses = lilv_ui_get_classes(uiToOpen);
            LILV_FOREACH(nodes, i, selectedClasses) {
                const LilvNode* type = lilv_nodes_get(selectedClasses, i);
                const QString typeUri = QString::fromUtf8(lilv_node_as_uri(type));
                if (typeUri == LV2_UI__GtkUI) {
                    isGtkUi = true;
                } else if (typeUri == LV2_UI__X11UI) {
                    isX11Ui = true;
                }
            }
            
            QPushButton* uiBtn = new QPushButton("Open Graphical UI...", m_paramContainer);
            uiBtn->setStyleSheet(
                "QPushButton { background-color: #00E676; color: black; font-weight: bold; border-radius: 4px; padding: 8px; border: none; }"
                "QPushButton:hover { background-color: #69F0AE; }"
                "QPushButton:pressed { background-color: #00C853; }"
            );
            m_paramLayout->addWidget(uiBtn);
            
            connect(uiBtn, &QPushButton::clicked, this, [this, lv2Node, uiToOpen, isGtkUi, isX11Ui]() {
                if (isGtkUi || isX11Ui) {
                    auto* uiWin = new ExternalPluginUIWindow(lv2Node, uiToOpen, isX11Ui, winId(), this);
                    if (!uiWin->isValid()) {
                        uiWin->deleteLater();
                        QMessageBox::warning(this, "Plugin GUI", "Failed to initialize the external plugin interface.");
                    }
                } else {
                    auto* uiWin = new PluginUIWindow(lv2Node, uiToOpen, this);
                    uiWin->show();
                }
            });
            
            QFrame* uiSeparator = new QFrame(m_paramContainer);
            uiSeparator->setFrameShape(QFrame::HLine);
            uiSeparator->setFrameShadow(QFrame::Sunken);
            uiSeparator->setStyleSheet("background-color: #333333; margin-top: 6px; margin-bottom: 6px;");
            m_paramLayout->addWidget(uiSeparator);
        }
    }
    
    auto* vst3Node = dynamic_cast<VST3PluginNode*>(node.get());
    if (vst3Node && vst3Node->hasEditor()) {
        hasCustomUI = true;
        QPushButton* uiBtn = new QPushButton("Open Graphical UI...", m_paramContainer);
        uiBtn->setStyleSheet(
            "QPushButton { background-color: #00E676; color: black; font-weight: bold; border-radius: 4px; padding: 8px; border: none; }"
            "QPushButton:hover { background-color: #69F0AE; }"
            "QPushButton:pressed { background-color: #00C853; }"
        );
        m_paramLayout->addWidget(uiBtn);
        
        connect(uiBtn, &QPushButton::clicked, this, [this, vst3Node]() {
            auto* uiWin = new VST3PluginUIWindow(vst3Node, this);
            uiWin->show();
        });
        
        QFrame* uiSeparator = new QFrame(m_paramContainer);
        uiSeparator->setFrameShape(QFrame::HLine);
        uiSeparator->setStyleSheet("background-color: #333333; margin-top: 6px; margin-bottom: 6px;");
        m_paramLayout->addWidget(uiSeparator);
    }

    auto* clapNode = dynamic_cast<CLAPPluginNode*>(node.get());
    if (clapNode && clapNode->hasGUI()) {
        hasCustomUI = true;
        QPushButton* uiBtn = new QPushButton("Open Graphical UI...", m_paramContainer);
        uiBtn->setStyleSheet(
            "QPushButton { background-color: #00E676; color: black; font-weight: bold; border-radius: 4px; padding: 8px; border: none; }"
            "QPushButton:hover { background-color: #69F0AE; }"
            "QPushButton:pressed { background-color: #00C853; }"
        );
        m_paramLayout->addWidget(uiBtn);
        
        connect(uiBtn, &QPushButton::clicked, this, [this, clapNode]() {
            auto* uiWin = new CLAPPluginUIWindow(clapNode, this);
            uiWin->show();
        });
        
        QFrame* uiSeparator = new QFrame(m_paramContainer);
        uiSeparator->setFrameShape(QFrame::HLine);
        uiSeparator->setStyleSheet("background-color: #333333; margin-top: 6px; margin-bottom: 6px;");
        m_paramLayout->addWidget(uiSeparator);
    }
    
    bool hasControlPorts = false;
    for (const auto& param : node->getControlPorts()) {
        if (!param.isOutput) {
            hasControlPorts = true;
            break;
        }
    }

    if (!hasControlPorts) {
        if (hasCustomUI) {
            QLabel* infoLabel = new QLabel("All controls are managed directly in the graphical interface.", m_paramContainer);
            infoLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic; margin: 4px;");
            infoLabel->setWordWrap(true);
            m_paramLayout->addWidget(infoLabel);
        } else {
            QLabel* noParamLabel = new QLabel("No parameters available for this plugin.", m_paramContainer);
            noParamLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic; margin: 4px;");
            m_paramLayout->addWidget(noParamLabel);
        }
    }

    // Create controls for parameters
    for (auto& param : node->getControlPorts()) {
        if (param.isOutput) continue; // Skip meter outputs
        
        QWidget* rowWidget = new QWidget(m_paramContainer);
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 4, 0, 4);
        
        QLabel* label = new QLabel(QString::fromStdString(param.name), rowWidget);
        label->setMinimumWidth(80);
        rowLayout->addWidget(label);
        
        uint32_t idx = param.index;
        float min = param.minVal;
        float max = param.maxVal;
        auto makeResettable = [this, idx, defaultValue = param.defaultVal](QWidget* control) {
            control->setProperty("parameterIndex", idx);
            control->setProperty("parameterDefault", defaultValue);
            control->installEventFilter(this);
        };

        if (param.isToggle) {
            auto* toggle = new QCheckBox(rowWidget);
            toggle->setChecked(param.value > (min + max) * 0.5f);
            makeResettable(toggle);
            rowLayout->addWidget(toggle);
            rowLayout->addStretch();
            connect(toggle, &QCheckBox::toggled, this, [this, node, idx, min, max](bool checked) {
                node->setParameter(idx, checked ? max : min);
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [toggle, min, max](float value) {
                if (!toggle->isDown()) {
                    const QSignalBlocker blocker(toggle);
                    toggle->setChecked(value > (min + max) * 0.5f);
                }
            }});
        } else if (param.isEnumeration && !param.scalePoints.empty()) {
            auto* combo = new QComboBox(rowWidget);
            for (const auto& point : param.scalePoints) {
                combo->addItem(QString::fromStdString(point.label), point.value);
            }
            int selected = 0;
            float closestDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < combo->count(); ++i) {
                const float distance = std::abs(combo->itemData(i).toFloat() - param.value);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    selected = i;
                }
            }
            combo->setCurrentIndex(selected);
            makeResettable(combo);
            rowLayout->addWidget(combo, 1);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, node, idx, combo](int selectedIndex) {
                node->setParameter(idx, combo->itemData(selectedIndex).toFloat());
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [combo](float value) {
                if (combo->view()->isVisible()) return;
                int selected = 0;
                float closestDistance = std::numeric_limits<float>::max();
                for (int i = 0; i < combo->count(); ++i) {
                    const float distance = std::abs(combo->itemData(i).toFloat() - value);
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        selected = i;
                    }
                }
                const QSignalBlocker blocker(combo);
                combo->setCurrentIndex(selected);
            }});
        } else if (param.isInteger) {
            auto* spinBox = new QSpinBox(rowWidget);
            spinBox->setRange(qFloor(min), qCeil(max));
            spinBox->setValue(qRound(param.value));
            makeResettable(spinBox);
            rowLayout->addWidget(spinBox, 1);
            connect(spinBox, &QSpinBox::valueChanged, this, [this, node, idx](int value) {
                node->setParameter(idx, static_cast<float>(value));
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [spinBox](float value) {
                if (!spinBox->hasFocus()) {
                    const QSignalBlocker blocker(spinBox);
                    spinBox->setValue(qRound(value));
                }
            }});
        } else {
            auto* slider = new QSlider(Qt::Horizontal, rowWidget);
            slider->setRange(0, 1000);
            const float normalized = max > min ? (param.value - min) / (max - min) : 0.0f;
            slider->setValue(qRound(std::clamp(normalized, 0.0f, 1.0f) * 1000));
            makeResettable(slider);
            rowLayout->addWidget(slider);

            auto* valueLabel = new QLabel(QString::number(param.value, 'f', 2), rowWidget);
            valueLabel->setMinimumWidth(58);
            valueLabel->setCursor(Qt::IBeamCursor);
            valueLabel->setToolTip("Double-click to enter an exact value");
            valueLabel->setProperty("parameterEdit", true);
            valueLabel->setProperty("parameterIndex", idx);
            valueLabel->setProperty("parameterMinimum", min);
            valueLabel->setProperty("parameterMaximum", max);
            valueLabel->installEventFilter(this);
            rowLayout->addWidget(valueLabel);
            connect(slider, &QSlider::valueChanged, this, [this, node, idx, min, max, valueLabel](int value) {
                const float floatVal = min + (value / 1000.0f) * (max - min);
                node->setParameter(idx, floatVal);
                valueLabel->setText(QString::number(floatVal, 'f', 2));
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [slider, valueLabel, min, max](float value) {
                if (slider->isSliderDown()) return;
                const float normalized = max > min ? (value - min) / (max - min) : 0.0f;
                const QSignalBlocker blocker(slider);
                slider->setValue(qRound(std::clamp(normalized, 0.0f, 1.0f) * 1000));
                valueLabel->setText(QString::number(value, 'f', 2));
            }});
        }
        
        m_paramLayout->addWidget(rowWidget);
    }
    
    // Add custom file picker buttons dynamically for any file-loading parameters
    std::vector<AudioNode::FileProperty> fileProps = node ? node->getFileProperties() : std::vector<AudioNode::FileProperty>{};
    QLabel* namFileLabel = nullptr;
    
    if (!fileProps.empty()) {
        QFrame* separator = new QFrame(m_paramContainer);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        separator->setStyleSheet("background-color: #333333; margin-top: 10px; margin-bottom: 10px;");
        m_paramLayout->addWidget(separator);

        for (const auto& fp : fileProps) {
            QFrame* fpFrame = new QFrame(m_paramContainer);
            fpFrame->setStyleSheet("background-color: #252528; border-radius: 6px; padding: 10px; margin-bottom: 8px;");
            QVBoxLayout* fpLayout = new QVBoxLayout(fpFrame);
            fpLayout->setContentsMargins(6, 6, 6, 6);
            fpLayout->setSpacing(4);

            QLabel* titleLabel = new QLabel(QString::fromStdString(fp.label), fpFrame);
            titleLabel->setStyleSheet("font-weight: bold; color: #00B0FF; font-size: 12px;");
            fpLayout->addWidget(titleLabel);

            QLabel* fileLabel = new QLabel(fpFrame);
            fileLabel->setStyleSheet("color: #E0E0E0; font-size: 11px; font-style: italic;");
            std::string currentPath = fp.fileValue;
            if (currentPath.empty()) {
                fileLabel->setText("No file loaded");
            } else {
                size_t slash = currentPath.find_last_of("/\\");
                std::string filename = (slash != std::string::npos) ? currentPath.substr(slash + 1) : currentPath;
                fileLabel->setText("Loaded: " + QString::fromStdString(filename));
            }
            fpLayout->addWidget(fileLabel);

            if (fp.uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model") {
                namFileLabel = fileLabel;
                const std::string& displayName = node->getModelDisplayName();
                if (!currentPath.empty()) {
                    size_t slash = currentPath.find_last_of("/\\");
                    std::string filename = (slash != std::string::npos) ? currentPath.substr(slash + 1) : currentPath;
                    fileLabel->setText("Loaded: " + QString::fromStdString(displayName.empty() ? filename : displayName));
                }
            }

            QHBoxLayout* btnLayout = new QHBoxLayout();
            btnLayout->setSpacing(6);

            QPushButton* loadBtn = new QPushButton("Load File...", fpFrame);
            loadBtn->setStyleSheet(
                "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 6px; border: none; }"
                "QPushButton:hover { background-color: #009688; }"
            );
            btnLayout->addWidget(loadBtn);

            std::string uri = fp.uri;

            QPushButton* browseBtn = nullptr;
            if (uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model") {
                browseBtn = new QPushButton("Browse TONE3000...", fpFrame);
                browseBtn->setStyleSheet(
                    "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; border-radius: 4px; padding: 6px; border: none; }"
                    "QPushButton:hover { background-color: #388E3C; }"
                );
                btnLayout->addWidget(browseBtn);
            }

            fpLayout->addLayout(btnLayout);

            connect(loadBtn, &QPushButton::clicked, this, [this, node, uri, fileLabel]() {
                QString filter = "All Files (*)";
                if (uri.find("model") != std::string::npos) {
                    filter = "Neural Models (*.nam *.nammodel *.json *.aidax *.aidadspmodel);;All Files (*)";
                } else if (uri.find("File") != std::string::npos || uri.find("file") != std::string::npos || uri.find("ir") != std::string::npos) {
                    filter = "Impulse Responses (*.wav *.flac *.ir);;All Files (*)";
                }

                QString filePath = QFileDialog::getOpenFileName(
                    this,
                    "Select File",
                    "",
                    filter
                );
                if (!filePath.isEmpty()) {
                    m_engine.suspendProcessing();
                    node->setFileProperty(uri, filePath.toStdString());
                    m_engine.resumeProcessing();
                    
                    size_t slash = filePath.toStdString().find_last_of("/\\");
                    std::string filename = (slash != std::string::npos) ? filePath.toStdString().substr(slash + 1) : filePath.toStdString();
                    fileLabel->setText("Loaded: " + QString::fromStdString(filename));
                    
                    if (uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model") {
                        node->setModelDisplayName(filename);
                        node->setModelSourceUrl({});
                    }

                    setUnsavedChanges(true);
                    saveConfigSettings();
                }
            });

            if (browseBtn) {
                connect(browseBtn, &QPushButton::clicked, this, [this, node, uri, fileLabel]() {
                    Tone3000Dialog dialog(node.get(), &m_engine, this);
                    if (dialog.exec() == QDialog::Accepted) {
                        std::string filePath = dialog.getDownloadedModelPath();
                        if (!filePath.empty()) {
                            m_engine.suspendProcessing();
                            node->setFileProperty(uri, filePath);
                            m_engine.resumeProcessing();
                            
                            size_t slash = filePath.find_last_of("/\\");
                            std::string filename = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;
                            const QString toneName = dialog.getDownloadedToneName();
                            node->setModelDisplayName((toneName.isEmpty() ? QString::fromStdString(filename) : toneName).toStdString());
                            node->setModelSourceUrl(dialog.getDownloadedToneUrl().toStdString());
                            
                            fileLabel->setText("Loaded: " + QString::fromStdString(node->getModelDisplayName()));
                            setUnsavedChanges(true);
                            saveConfigSettings();
                            
                            showPluginControls(node);
                        }
                    }
                });
            }

            if (uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model" && !currentPath.empty()) {
                auto* modelActions = new QHBoxLayout();
                modelActions->setSpacing(6);

                auto* exportButton = new QPushButton("Export NAM...", fpFrame);
                exportButton->setStyleSheet("QPushButton { padding: 4px; }");
                modelActions->addWidget(exportButton);
                connect(exportButton, &QPushButton::clicked, this, [this, node]() {
                    const QString sourcePath = QString::fromStdString(node->getModelFilePath());
                    if (!QFileInfo(sourcePath).isFile()) {
                        QMessageBox::warning(this, "Export NAM", "The active NAM file is unavailable.");
                        return;
                    }
                    const QString destination = QFileDialog::getSaveFileName(
                        this, "Export NAM", QFileInfo(sourcePath).fileName(), "NAM Models (*.nam);;All Files (*)");
                    if (destination.isEmpty()) return;
                    if (QFileInfo(destination).absoluteFilePath() == QFileInfo(sourcePath).absoluteFilePath()) return;
                    QFile::remove(destination);
                    if (!QFile::copy(sourcePath, destination)) {
                        QMessageBox::warning(this, "Export NAM", "Could not export the NAM file.");
                    }
                });

                const QString sourceUrl = QString::fromStdString(node->getModelSourceUrl());
                if (!sourceUrl.isEmpty()) {
                    auto* sourceButton = new QPushButton("Open on TONE3000", fpFrame);
                    sourceButton->setStyleSheet("QPushButton { padding: 4px; }");
                    modelActions->addWidget(sourceButton);
                    connect(sourceButton, &QPushButton::clicked, this, [sourceUrl] {
                        QDesktopServices::openUrl(QUrl(sourceUrl));
                    });
                }

                fpLayout->addLayout(modelActions);
            }

            m_paramLayout->addWidget(fpFrame);
        }
    }

    // Add variants dropdown combo box if variants list is populated
    if (node && !node->getModelVariants().empty()) {
        QFrame* separator2 = new QFrame(m_paramContainer);
        separator2->setFrameShape(QFrame::HLine);
        separator2->setFrameShadow(QFrame::Sunken);
        separator2->setStyleSheet("background-color: #333333; margin-top: 10px; margin-bottom: 10px;");
        m_paramLayout->addWidget(separator2);

        QLabel* varTitleLabel = new QLabel("Profile Variants:", m_paramContainer);
        varTitleLabel->setStyleSheet("font-weight: bold; color: #00B0FF; margin-bottom: 6px;");
        m_paramLayout->addWidget(varTitleLabel);

        QComboBox* varCombo = new QComboBox(m_paramContainer);
        varCombo->setMinimumWidth(200);

        const auto& vars = node->getModelVariants();
        int activeIdx = -1;
        std::string currentModelPath = node->getModelFilePath();

        for (size_t i = 0; i < vars.size(); ++i) {
            QString nameStr = QString::fromStdString(vars[i].name);
            if (!vars[i].localPath.empty() && QFile::exists(QString::fromStdString(vars[i].localPath))) {
                nameStr += " (Cached)";
            }
            varCombo->addItem(nameStr, static_cast<int>(i));

            if (!vars[i].localPath.empty() && vars[i].localPath == currentModelPath) {
                activeIdx = static_cast<int>(i);
            }
        }

        if (activeIdx != -1) {
            varCombo->setCurrentIndex(activeIdx);
        }

        m_paramLayout->addWidget(varCombo);

        connect(varCombo, &QComboBox::activated, this, [this, node, varCombo, namFileLabel](int index) {
            int variantIdx = varCombo->itemData(index).toInt();
            const auto& vars = node->getModelVariants();
            if (variantIdx >= 0 && variantIdx < (int)vars.size()) {
                auto& selectedVar = vars[variantIdx];
                if (!selectedVar.localPath.empty() && QFile::exists(QString::fromStdString(selectedVar.localPath))) {
                    m_engine.suspendProcessing();
                    node->loadModelFile(selectedVar.localPath);
                    m_engine.resumeProcessing();

                    size_t slash = selectedVar.localPath.find_last_of("/\\");
                    std::string filename = (slash != std::string::npos) ? selectedVar.localPath.substr(slash + 1) : selectedVar.localPath;
                    node->setModelDisplayName(selectedVar.name.empty() ? filename : selectedVar.name);
                    if (namFileLabel) {
                        namFileLabel->setText("Loaded: " + QString::fromStdString(node->getModelDisplayName()));
                    }

                    setUnsavedChanges(true);
                    saveConfigSettings();
                } else {
                    downloadVariant(node, variantIdx, varCombo, namFileLabel);
                }
            }
        });
    }
}

void MainWindow::showBranchControls(int row) {
    showRoutingNodeControls(row, true);
}

void MainWindow::showRoutingNodeControls(int row, bool isSplit) {
    if (row == NodeCanvas::MAIN_ROW || row < 0 || row >= NodeCanvas::NUM_ROWS) {
        showPluginControls(nullptr);
        return;
    }
    m_parameterControlBindings.clear();
    m_parameterControlNode.reset();
    clearLayoutContents(m_paramLayout);
    m_noParamLabel->hide();

    const QString pathName = m_canvas->getBranchName(row);
    QString pathLetter = pathName;
    if (pathLetter.startsWith("Path ")) {
        pathLetter = pathLetter.mid(5);
    }
    const QString sectionType = isSplit ? "Split Section " : "Mixer Section ";
    auto* header = new QLabel(sectionType + pathLetter, m_paramContainer);
    header->setStyleSheet("font-weight: bold; color: #4f8cff; font-size: 14px;");
    m_paramLayout->addWidget(header);

    const int sourceCol = m_canvas->getSplitCol(row);
    const int returnCol = m_canvas->getMergeCol(row);
    const int parentRow = m_canvas->getSplitParentRow(row);
    QString source = parentRow == NodeCanvas::MAIN_ROW ? "System Input" : m_canvas->getBranchName(parentRow) + " input";
    QString destination = parentRow == NodeCanvas::MAIN_ROW ? "System Output" : m_canvas->getBranchName(parentRow) + " return";
    if (sourceCol >= 0) {
        if (auto node = m_canvas->getPluginAt(parentRow, sourceCol)) source = "After " + QString::fromStdString(node->getName());
    }
    if (returnCol >= 0) {
        if (auto node = m_canvas->getPluginAt(parentRow, returnCol)) destination = "Before " + QString::fromStdString(node->getName());
    }
    auto* route = new QLabel(source + "  ->  " + destination, m_paramContainer);
    route->setWordWrap(true);
    route->setStyleSheet("color: #a6adb8; margin-bottom: 6px;");
    m_paramLayout->addWidget(route);

    auto* splitTitle = new QLabel("SPLIT", m_paramContainer);
    splitTitle->setStyleSheet("font-weight: bold; color: #7da6ff; margin-top: 5px;");
    m_paramLayout->addWidget(splitTitle);
    auto* splitForm = new QFormLayout();
    auto* type = new QComboBox(m_paramContainer);
    type->addItem("Copy", static_cast<int>(GridRow::SplitMode::Copy));
    type->addItem("A/B (Equal Power)", static_cast<int>(GridRow::SplitMode::AB));
    type->setCurrentIndex(type->findData(static_cast<int>(m_canvas->getSplitMode(row))));
    splitForm->addRow("Type", type);
    m_paramLayout->addLayout(splitForm);

    auto addSlider = [this](const QString& title, int minimum, int maximum, int value) {
        auto* titleRow = new QHBoxLayout();
        auto* label = new QLabel(title, m_paramContainer);
        auto* valueLabel = new QLabel(m_paramContainer);
        valueLabel->setStyleSheet("color: #35c7ff; font-weight: bold;");
        titleRow->addWidget(label);
        titleRow->addStretch();
        titleRow->addWidget(valueLabel);
        auto* slider = new QSlider(Qt::Horizontal, m_paramContainer);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        m_paramLayout->addLayout(titleRow);
        m_paramLayout->addWidget(slider);
        return std::pair<QSlider*, QLabel*>{slider, valueLabel};
    };
    auto [splitPosition, splitPositionValue] = addSlider("Route A / B", -100, 100,
                                                         qRound(m_canvas->getSplitPosition(row) * 100.0f));
    auto updateSplitPosition = [splitPositionValue](int value) {
        splitPositionValue->setText(value == 0 ? "A = B" : QString("%1 %2").arg(std::abs(value)).arg(value < 0 ? "to A" : "to B"));
    };
    updateSplitPosition(splitPosition->value());
    splitPosition->setEnabled(m_canvas->getSplitMode(row) == GridRow::SplitMode::AB);

    auto* mixerTitle = new QLabel("MIXER", m_paramContainer);
    mixerTitle->setStyleSheet("font-weight: bold; color: #c18cff; margin-top: 9px;");
    m_paramLayout->addWidget(mixerTitle);
    auto [mainLevel, mainLevelValue] = addSlider("Path A Level", 0, 100,
                                                qRound(m_canvas->getMainMix(row) * 100.0f));
    auto updateMainLevel = [mainLevelValue](int value) {
        mainLevelValue->setText(value == 0 ? "-inf dB" : QString("%1 dB").arg(20.0 * std::log10(value / 100.0), 0, 'f', 1));
    };
    updateMainLevel(mainLevel->value());
    auto [level, levelValue] = addSlider(pathName + " Level", 0, 100, qRound(m_canvas->getMix(row) * 100.0f));
    auto updateLevel = [levelValue](int value) {
        levelValue->setText(value == 0 ? "-inf dB" : QString("%1 dB").arg(20.0 * std::log10(value / 100.0), 0, 'f', 1));
    };
    updateLevel(level->value());
    auto [pan, panValue] = addSlider(m_canvas->getBranchOutputChannels(row) >= 2 ? pathName + " Balance" : pathName + " Pan",
                                     -100, 100, qRound(m_canvas->getPan(row) * 100.0f));
    auto updatePan = [panValue](int value) {
        panValue->setText(value == 0 ? "Center" : QString("%1% %2").arg(std::abs(value)).arg(value < 0 ? "Left" : "Right"));
    };
    updatePan(pan->value());
    auto* polarity = new QCheckBox("Invert " + pathName + " polarity", m_paramContainer);
    polarity->setChecked(m_canvas->isPolarityInverted(row));
    m_paramLayout->addWidget(polarity);
    auto* hint = new QLabel("Drag SPLIT to move the source. Drag MIX to move the return. Drag a new Split onto this Split to divide " + pathName + " again.", m_paramContainer);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #8b929d; margin-top: 9px;");
    m_paramLayout->addWidget(hint);

    auto* remove = new QPushButton("Remove Split Section", m_paramContainer);
    remove->setEnabled(true);
    remove->setToolTip("Remove this Split and Mixer along with all plugins on this branch.");
    m_paramLayout->addWidget(remove);

    connect(type, &QComboBox::currentIndexChanged, this, [this, row, type, splitPosition](int) {
        const auto mode = static_cast<GridRow::SplitMode>(type->currentData().toInt());
        m_canvas->setSplitMode(row, mode);
        splitPosition->setEnabled(mode == GridRow::SplitMode::AB);
    });
    connect(splitPosition, &QSlider::valueChanged, this, [this, row, updateSplitPosition](int value) {
        m_canvas->setSplitPosition(row, value / 100.0f);
        updateSplitPosition(value);
    });
    connect(polarity, &QCheckBox::toggled, this, [this, row](bool checked) { m_canvas->setPolarityInverted(row, checked); });
    connect(mainLevel, &QSlider::valueChanged, this, [this, row, updateMainLevel](int value) {
        m_canvas->setMainMix(row, value / 100.0f);
        updateMainLevel(value);
    });
    connect(level, &QSlider::valueChanged, this, [this, row, updateLevel](int value) {
        m_canvas->setMix(row, value / 100.0f);
        updateLevel(value);
    });
    connect(pan, &QSlider::valueChanged, this, [this, row, updatePan](int value) {
        m_canvas->setPan(row, value / 100.0f);
        updatePan(value);
    });
    connect(remove, &QPushButton::clicked, this, [this, row] {
        m_canvas->removeSplitSection(row);
        showPluginControls(nullptr);
    });
    Q_UNUSED(isSplit);
}

void MainWindow::downloadVariant(std::shared_ptr<AudioNode> node, int variantIdx, QPointer<QComboBox> combo, QPointer<QLabel> fileLabel, bool isRedirect) {
    if (variantIdx < 0 || variantIdx >= (int)node->getModelVariants().size()) return;

    if (m_currentDownloadReply) {
        m_currentDownloadReply->abort();
        m_currentDownloadReply->deleteLater();
        m_currentDownloadReply = nullptr;
    }

    auto& vars = node->getModelVariants();
    auto& var = const_cast<AudioNode::ModelVariant&>(vars[variantIdx]);

    QString urlStr = QString::fromStdString(var.url);
    if (urlStr.isEmpty()) return;

    QString safeName = QString::fromStdString(var.name);
    safeName.replace(QRegularExpression("[^a-zA-Z0-9_\\-.]"), "_");
    if (!safeName.endsWith(".nam") && !safeName.endsWith(".json")) {
        safeName += ".nam";
    }

    QString cacheDir = QDir::homePath() + "/.cache/PedalBoard/tone3000";
    QDir().mkpath(cacheDir);
    QString localFilePath = cacheDir + "/" + safeName;

    if (!combo.isNull()) combo->setEnabled(false);
    if (!fileLabel.isNull()) fileLabel->setText("Downloading variant: 0%...");

    QSettings settings("PedalBoard", "PedalBoard");
    QString savedKeyEnc = settings.value("tone3000_api_key", "").toString();
    bool useOfficial = !savedKeyEnc.isEmpty();

    QString finalUrlStr = urlStr;

    QNetworkRequest request((QUrl(finalUrlStr)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    if (useOfficial && !isRedirect && QUrl(finalUrlStr).host().endsWith("tone3000.com")) {
        QString activeKey = deobfuscateKey(savedKeyEnc);
        request.setRawHeader("Authorization", ("Bearer " + activeKey).toUtf8());
    }

    m_currentDownloadReply = m_networkManager->get(request);

    connect(m_currentDownloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total) {
        if (fileLabel.isNull()) return;
        if (total > 0) {
            int progress = static_cast<int>((received * 100) / total);
            fileLabel->setText(QString("Downloading variant: %1%...").arg(progress));
        }
    });

    connect(m_currentDownloadReply, &QNetworkReply::finished, this, [=, this, &var]() {
        if (!m_currentDownloadReply) return;

        // Check for redirection manually to strip auth headers on redirect target
        QVariant redirectUrl = m_currentDownloadReply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectUrl.isValid()) {
            QUrl nextUrl = redirectUrl.toUrl();
            if (nextUrl.isRelative()) {
                nextUrl = m_currentDownloadReply->url().resolved(nextUrl);
            }
            m_currentDownloadReply->deleteLater();
            m_currentDownloadReply = nullptr;
            
            var.url = nextUrl.toString().toStdString();
            downloadVariant(node, variantIdx, combo, fileLabel, true);
            return;
        }

        if (m_currentDownloadReply->error() != QNetworkReply::NoError) {
            if (m_currentDownloadReply->error() != QNetworkReply::OperationCanceledError) {
                if (!fileLabel.isNull()) fileLabel->setText("Download failed!");
                QMessageBox::critical(this, "Download Error", "Failed to download variant: " + m_currentDownloadReply->errorString());
            }
            if (!combo.isNull()) combo->setEnabled(true);
            m_currentDownloadReply->deleteLater();
            m_currentDownloadReply = nullptr;
            return;
        }

        QByteArray data = m_currentDownloadReply->readAll();
        m_currentDownloadReply->deleteLater();
        m_currentDownloadReply = nullptr;

        QFile file(localFilePath);
        if (file.open(QFile::WriteOnly)) {
            file.write(data);
            file.close();

            var.localPath = localFilePath.toStdString();

            m_engine.suspendProcessing();
            node->loadModelFile(var.localPath);
            m_engine.resumeProcessing();

            node->setModelDisplayName(var.name.empty() ? safeName.toStdString() : var.name);
            if (!fileLabel.isNull()) fileLabel->setText("Loaded: " + QString::fromStdString(node->getModelDisplayName()));
            setUnsavedChanges(true);
            saveConfigSettings();

            if (!combo.isNull()) combo->setItemText(variantIdx, QString::fromStdString(var.name) + " (Cached)");
        } else {
            if (!fileLabel.isNull()) fileLabel->setText("Failed to save model file!");
        }

        if (!combo.isNull()) combo->setEnabled(true);
    });
}
