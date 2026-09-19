#include "MainWindow.h"
#include "CredentialStore.h"
#include "../audio/LV2Host.h"
#include "../audio/VST3Host.h"
#include "../audio/CLAPHost.h"
#include "../audio/MissingPluginNode.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "../audio/BypassNode.h"
#include "NodeWidget.h"
#include "Tone3000Dialog.h"
#include "Tone3000ImageLoader.h"
#include "ModelDetailsDialog.h"
#include "InspectorComponents.h"
#include "AboutDialog.h"
#include "PresetBrowser.h"
#include "FootswitchTile.h"
#include "../control/MidiRouter.h"
#include <filesystem>
#include <iostream>
#include <unordered_set>
#include "PortWidget.h"
#include <QSplitter>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMenu>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QToolTip>
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
#include <QShortcut>
#include <QStyle>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QTabWidget>
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
#include <QTabWidget>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
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
#include <QButtonGroup>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <QSet>

class AmpPreviewLabel final : public QLabel {
public:
    explicit AmpPreviewLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setFixedSize(200, 110);
        setAlignment(Qt::AlignCenter);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = rect().adjusted(1, 1, -1, -1);
        const QPixmap image = pixmap(Qt::ReturnByValue);
        if (!image.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(frame, 6, 6);
            painter.setClipPath(clip);
            painter.drawPixmap(rect(), image);
            painter.setClipping(false);
            painter.setPen(QPen(QColor("#4A535D"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(frame, 6, 6);
            return;
        }

        QLinearGradient shell(frame.topLeft(), frame.bottomLeft());
        shell.setColorAt(0.0, QColor("#333840"));
        shell.setColorAt(0.28, QColor("#242930"));
        shell.setColorAt(1.0, QColor("#15191D"));
        painter.setPen(QPen(QColor("#4A535D"), 1));
        painter.setBrush(shell);
        painter.drawRoundedRect(frame, 6, 6);

        painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
        for (int y = 42; y < 96; y += 5) painter.drawLine(10, y, width() - 10, y);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#111419"));
        painter.drawRoundedRect(QRectF(10, 10, width() - 20, 24), 3, 3);
        for (int x = 24; x <= 72; x += 16) {
            painter.setBrush(QColor("#D97B32"));
            painter.drawEllipse(QPointF(x, 22), 2.5, 2.5);
        }

        QFont logoFont = painter.font();
        logoFont.setBold(true);
        logoFont.setPixelSize(15);
        logoFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        painter.setFont(logoFont);
        painter.setPen(QColor("#CDD3DA"));
        painter.drawText(QRectF(90, 10, 96, 24), Qt::AlignCenter, "NAM");
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
    int audioInputs = 2;
    int audioOutputs = 2;
    int controlPorts = 0;
    QString version;
    QString description;
    QStringList features;
    QString path;
    bool hasNativeGUI = false;
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

static std::vector<PickerPluginInfo> buildPickerInfos(const std::vector<MainWindow::PluginInfo>& available) {
    std::vector<PickerPluginInfo> plugins;
    plugins.reserve(available.size());
    for (const auto& info : available) {
        QString name = QString::fromStdString(info.name);
        QString category = QString::fromStdString(info.category);
        QString brand = QString::fromStdString(info.brand);
        QString uri = QString::fromStdString(info.uri);
        QString format = info.isLV2 ? "LV2" : (info.uri == "builtin:bypass" ? "Built-in" : (info.uri.find(".clap") != std::string::npos ? "CLAP" : "VST3"));
        QString searchable = (name + " " + category + " " + brand + " " + uri + " " + format).toLower();

        QStringList featureList;
        for (const auto& feat : info.features) {
            featureList.push_back(QString::fromStdString(feat));
        }

        plugins.push_back({
            name,
            uri,
            category,
            brand,
            info.thumbnailPath,
            format,
            searchable,
            info.audioInputs,
            info.audioOutputs,
            info.controlPorts,
            QString::fromStdString(info.version),
            QString::fromStdString(info.description),
            featureList,
            QString::fromStdString(info.path),
            info.hasNativeGUI
        });
    }
    return plugins;
}

class PluginPickerDialog final : public QDialog {
public:
    PluginPickerDialog(
        const std::vector<PickerPluginInfo>& plugins,
        QSet<QString>* favorites,
        std::function<void()> favoritesChanged,
        QWidget* parent = nullptr)
        : QDialog(parent), m_plugins(plugins), m_favorites(favorites), m_favoritesChanged(std::move(favoritesChanged)) {
        setWindowTitle("Plugin Browser & Selector");
        setModal(true);
        resize(840, 560);
        setStyleSheet(
            "QDialog { background: #16161a; color: #e9e9ee; }"
            "QLineEdit, QComboBox, QListWidget { background: #202026; border: 1px solid #33333d; border-radius: 5px; color: #ececf0; padding: 6px 10px; font-size: 12px; }"
            "QListWidget::item { border: none; padding: 0; }"
            "QListWidget::item:selected { background: transparent; }"
            "QToolButton { border: none; color: #ffc857; font-size: 16px; padding: 4px; }"
            "QPushButton { background: #00a8e8; border: none; border-radius: 5px; color: white; font-weight: bold; padding: 8px 16px; font-size: 12px; }"
            "QPushButton:hover { background: #27b9f0; }");

        // Pre-load and scale all thumbnails to cache
        for (const auto& plugin : m_plugins) {
            if (!plugin.thumbnailPath.isEmpty() && QFileInfo::exists(plugin.thumbnailPath)) {
                QPixmap thumbnail(plugin.thumbnailPath);
                m_thumbnailCache.insert(plugin.thumbnailPath, thumbnail.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(16, 16, 16, 16);
        mainLayout->setSpacing(12);

        // Filter Bar (Search + Format Filter + Category Filter)
        auto* filterRow = new QHBoxLayout();
        filterRow->setSpacing(8);

        m_search = new QLineEdit(this);
        m_search->setPlaceholderText("Search plugins by name, brand, category, or URI...");

        m_formatFilter = new QComboBox(this);
        m_formatFilter->setMinimumWidth(120);
        m_formatFilter->addItems({"All Formats", "LV2", "CLAP", "VST3"});

        m_category = new QComboBox(this);
        m_category->setMinimumWidth(140);

        m_resultCount = new QLabel(this);
        m_resultCount->setStyleSheet("color: #00b0ff; font-size: 11px; font-weight: bold; padding-left: 4px;");

        filterRow->addWidget(m_search, 1);
        filterRow->addWidget(m_formatFilter);
        filterRow->addWidget(m_category);
        filterRow->addWidget(m_resultCount);
        mainLayout->addLayout(filterRow);

        // Split Body: Left List + Right Details Pane
        auto* splitLayout = new QHBoxLayout();
        splitLayout->setSpacing(14);

        // Left Container: Plugin List
        m_list = new QListWidget(this);
        m_list->setSpacing(4);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        splitLayout->addWidget(m_list, 5);

        // Right Container: Enhanced Details Pane
        m_detailsPane = new QWidget(this);
        m_detailsPane->setStyleSheet("QWidget#detailsPane { background: #1f1f26; border: 1px solid #2e2e38; border-radius: 8px; }");
        m_detailsPane->setObjectName("detailsPane");
        
        auto* detailsLayout = new QVBoxLayout(m_detailsPane);
        detailsLayout->setContentsMargins(16, 16, 16, 16);
        detailsLayout->setSpacing(12);

        auto* headerLayout = new QHBoxLayout();
        headerLayout->setSpacing(12);

        m_detailIconLabel = new QLabel(m_detailsPane);
        m_detailIconLabel->setFixedSize(48, 48);
        m_detailIconLabel->setAlignment(Qt::AlignCenter);
        headerLayout->addWidget(m_detailIconLabel);

        auto* titleBox = new QVBoxLayout();
        titleBox->setSpacing(2);
        m_detailNameLabel = new QLabel("Select a plugin", m_detailsPane);
        m_detailNameLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #ffffff; border: none;");
        m_detailBrandLabel = new QLabel("", m_detailsPane);
        m_detailBrandLabel->setStyleSheet("font-size: 11px; color: #8a8a98; border: none;");
        titleBox->addWidget(m_detailNameLabel);
        titleBox->addWidget(m_detailBrandLabel);
        headerLayout->addLayout(titleBox, 1);
        detailsLayout->addLayout(headerLayout);

        // Badges Row
        auto* badgeRow = new QHBoxLayout();
        badgeRow->setSpacing(6);
        m_detailFormatBadge = new QLabel(m_detailsPane);
        m_detailFormatBadge->setStyleSheet("background: #00b0ff; color: #000; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        m_detailCategoryBadge = new QLabel(m_detailsPane);
        m_detailCategoryBadge->setStyleSheet("background: #2e303c; color: #e0e0e0; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        m_detailAudioBadge = new QLabel(m_detailsPane);
        m_detailAudioBadge->setStyleSheet("background: #1c2b36; color: #38c5ff; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        m_detailGuiBadge = new QLabel(m_detailsPane);
        m_detailGuiBadge->setStyleSheet("background: #1b3022; color: #4caf50; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");

        badgeRow->addWidget(m_detailFormatBadge);
        badgeRow->addWidget(m_detailCategoryBadge);
        badgeRow->addWidget(m_detailAudioBadge);
        badgeRow->addWidget(m_detailGuiBadge);
        badgeRow->addStretch();
        detailsLayout->addLayout(badgeRow);

        // Graphical Preview Card (Modgui / Thumbnail Skin Preview)
        m_detailPreviewLabel = new QLabel(m_detailsPane);
        m_detailPreviewLabel->setObjectName("detailPreview");
        m_detailPreviewLabel->setFixedHeight(140);
        m_detailPreviewLabel->setAlignment(Qt::AlignCenter);
        m_detailPreviewLabel->setStyleSheet("QLabel#detailPreview { background: #141419; border: 1px solid #2a2a35; border-radius: 6px; padding: 4px; }");
        detailsLayout->addWidget(m_detailPreviewLabel);

        auto* divider = new QFrame(m_detailsPane);
        divider->setFrameShape(QFrame::HLine);
        divider->setStyleSheet("color: #2e2e38;");
        detailsLayout->addWidget(divider);

        // Metadata Fields
        auto* formLayout = new QFormLayout();
        formLayout->setSpacing(8);
        formLayout->setLabelAlignment(Qt::AlignLeft);

        m_detailUriLabel = new QLabel(m_detailsPane);
        m_detailUriLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_detailUriLabel->setStyleSheet("font-size: 11px; color: #7b93a4; border: none;");
        m_detailUriLabel->setWordWrap(true);

        m_detailPathLabel = new QLabel(m_detailsPane);
        m_detailPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_detailPathLabel->setStyleSheet("font-size: 11px; color: #7b93a4; border: none;");
        m_detailPathLabel->setWordWrap(true);

        m_detailParamsLabel = new QLabel(m_detailsPane);
        m_detailParamsLabel->setStyleSheet("font-size: 11px; color: #e0e0e0; border: none;");

        m_detailTagsLabel = new QLabel(m_detailsPane);
        m_detailTagsLabel->setStyleSheet("font-size: 11px; color: #00b0ff; border: none;");
        m_detailTagsLabel->setWordWrap(true);

        formLayout->addRow("<b style='color:#a0a0b0;'>URI / Identifier:</b>", m_detailUriLabel);
        formLayout->addRow("<b style='color:#a0a0b0;'>Path / Location:</b>", m_detailPathLabel);
        formLayout->addRow("<b style='color:#a0a0b0;'>Control Parameters:</b>", m_detailParamsLabel);
        formLayout->addRow("<b style='color:#a0a0b0;'>Features & Tags:</b>", m_detailTagsLabel);
        detailsLayout->addLayout(formLayout);

        detailsLayout->addStretch();

        // Details Footer Action Row
        auto* detailActions = new QHBoxLayout();
        m_detailFavoriteBtn = new QToolButton(m_detailsPane);
        m_detailFavoriteBtn->setText("☆ Favorite");
        m_detailFavoriteBtn->setStyleSheet(
            "QToolButton { background: #262730; color: #ffc857; font-weight: bold; border-radius: 4px; padding: 6px 10px; font-size: 11px; border: 1px solid #3d3e4d; }"
            "QToolButton:hover { background: #323440; }"
        );
        detailActions->addWidget(m_detailFavoriteBtn);
        detailActions->addStretch();
        detailsLayout->addLayout(detailActions);

        splitLayout->addWidget(m_detailsPane, 4);
        mainLayout->addLayout(splitLayout, 1);

        // Bottom Actions Bar
        auto* actions = new QHBoxLayout();
        auto* hint = new QLabel("Tip: Double-click a plugin to quickly add it to your board.", this);
        hint->setStyleSheet("color: #7d7d8a; font-size: 11px;");
        auto* cancel = new QPushButton("Cancel", this);
        cancel->setStyleSheet("QPushButton { background: #2c2c34; } QPushButton:hover { background: #3a3a44; }");
        auto* add = new QPushButton("Add Plugin to Board", this);
        add->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        actions->addWidget(hint, 1);
        actions->addWidget(cancel);
        actions->addWidget(add);
        mainLayout->addLayout(actions);

        // Populate Category Filter Combo (Filter out format strings)
        QSet<QString> categories;
        for (const auto& plugin : m_plugins) {
            if (!plugin.category.isEmpty() && !plugin.category.contains("Plugin")) {
                categories.insert(plugin.category);
            }
        }
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
        connect(m_formatFilter, &QComboBox::currentTextChanged, this, [this] { refreshResults(); });
        connect(m_category, &QComboBox::currentTextChanged, this, [this] { refreshResults(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(add, &QPushButton::clicked, this, [this] { acceptSelection(); });
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { acceptSelection(); });
        connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
            if (current) {
                updateDetailsPane(current->data(Qt::UserRole).toString());
            }
        });

        refreshResults();
        m_search->setFocus();
    }

    QString selectedUri() const { return m_selectedUri; }

private:
    void updateDetailsPane(const QString& uri) {
        auto it = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const PickerPluginInfo& p) {
            return p.uri == uri;
        });
        if (it == m_plugins.end()) return;

        const PickerPluginInfo& plugin = *it;
        const bool isFav = m_favorites->contains(plugin.uri);

        m_detailNameLabel->setText(plugin.name);
        m_detailBrandLabel->setText(plugin.brand.isEmpty() ? "Unknown Vendor" : plugin.brand);

        // Format Badge Styling
        if (plugin.format == "LV2") {
            m_detailFormatBadge->setText("LV2");
            m_detailFormatBadge->setStyleSheet("background: #00B0FF; color: #000; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        } else if (plugin.format == "CLAP") {
            m_detailFormatBadge->setText("CLAP");
            m_detailFormatBadge->setStyleSheet("background: #AB47BC; color: #FFF; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        } else {
            m_detailFormatBadge->setText("VST3");
            m_detailFormatBadge->setStyleSheet("background: #FFA726; color: #000; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        }

        m_detailCategoryBadge->setText(plugin.category.toUpper());
        m_detailUriLabel->setText(plugin.uri);
        m_detailPathLabel->setText(plugin.path.isEmpty() ? (plugin.thumbnailPath.isEmpty() ? "Standard Plugin Bundle" : plugin.thumbnailPath) : plugin.path);

        // Audio I/O Layout Badge
        if (plugin.audioInputs == 2 && plugin.audioOutputs == 2) {
            m_detailAudioBadge->setText("Stereo (2x2)");
        } else if (plugin.audioInputs == 1 && plugin.audioOutputs == 1) {
            m_detailAudioBadge->setText("Mono (1x1)");
        } else {
            m_detailAudioBadge->setText(QString("Audio (%1 In / %2 Out)").arg(plugin.audioInputs).arg(plugin.audioOutputs));
        }

        // GUI Badge
        if (plugin.hasNativeGUI) {
            m_detailGuiBadge->setText("Native UI");
            m_detailGuiBadge->setStyleSheet("background: #1b3022; color: #4caf50; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        } else {
            m_detailGuiBadge->setText("Parameters");
            m_detailGuiBadge->setStyleSheet("background: #252830; color: #90a0b0; font-weight: bold; border-radius: 3px; padding: 2px 6px; font-size: 10px;");
        }

        // Parameter Ports & Features
        if (plugin.controlPorts > 0) {
            m_detailParamsLabel->setText(QString("%1 Parameters").arg(plugin.controlPorts));
        } else {
            m_detailParamsLabel->setText("Dynamic / Standard Ports");
        }

        if (!plugin.features.isEmpty()) {
            m_detailTagsLabel->setText(plugin.features.join(", "));
        } else {
            m_detailTagsLabel->setText("Standard " + plugin.format + " Effect");
        }

        m_currentThumbnailPath = plugin.thumbnailPath;
        if (!plugin.thumbnailPath.isEmpty() && QFileInfo::exists(plugin.thumbnailPath)) {
            QPixmap fullPixmap(plugin.thumbnailPath);
            if (!fullPixmap.isNull()) {
                m_detailPreviewLabel->setPixmap(fullPixmap.scaled(340, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_detailPreviewLabel->show();
            } else {
                m_detailPreviewLabel->hide();
            }
        } else {
            m_detailPreviewLabel->hide();
        }

        const bool hasThumbnail = !plugin.thumbnailPath.isEmpty() && m_thumbnailCache.contains(plugin.thumbnailPath);
        if (hasThumbnail) {
            m_detailIconLabel->setPixmap(m_thumbnailCache.value(plugin.thumbnailPath).scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_detailIconLabel->setText(pluginCategoryGlyph(plugin.category));
            m_detailIconLabel->setStyleSheet("background: #123348; color: #38c5ff; border-radius: 6px; font-weight: bold; font-size: 12px;");
        }

        m_detailFavoriteBtn->setText(isFav ? "★ Favorited" : "☆ Add Favorite");
        m_detailFavoriteBtn->disconnect();
        connect(m_detailFavoriteBtn, &QToolButton::clicked, this, [this, plugin] {
            if (m_favorites->contains(plugin.uri)) m_favorites->remove(plugin.uri);
            else m_favorites->insert(plugin.uri);
            m_favoritesChanged();
            updateDetailsPane(plugin.uri);
            if (m_category->currentText() == "Favorites") {
                refreshResults();
            }
        });
    }

    void refreshResults() {
        const QString query = m_search->text().trimmed().toLower();
        const QString formatFilter = m_formatFilter->currentText();
        const QString category = m_category->currentText();
        std::vector<PickerPluginInfo> results;
        for (const auto& plugin : m_plugins) {
            const bool favorite = m_favorites->contains(plugin.uri);
            if (formatFilter != "All Formats" && plugin.format != formatFilter) continue;
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
            row->setStyleSheet("QWidget { background: #202026; border: 1px solid #2e2e38; border-radius: 5px; } QWidget:hover { background: #282832; border-color: #00a8e8; }");
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
            metadataLabel->setStyleSheet("font-size: 10px; color: #8a8a98; border: none;");
            rowLayout->addWidget(metadataLabel);

            auto* favorite = new QToolButton(row);
            favorite->setText(m_favorites->contains(plugin.uri) ? "★" : "☆");
            favorite->setToolTip("Toggle favorite");
            favorite->setFixedSize(28, 28);
            connect(favorite, &QToolButton::clicked, this, [this, favorite, plugin] {
                if (m_favorites->contains(plugin.uri)) {
                    m_favorites->remove(plugin.uri);
                    favorite->setText("☆");
                } else {
                    m_favorites->insert(plugin.uri);
                    favorite->setText("★");
                }
                m_favoritesChanged();
                updateDetailsPane(plugin.uri);
                if (m_category->currentText() == "Favorites") {
                    refreshResults();
                }
            });
            rowLayout->addWidget(favorite);
            m_list->setItemWidget(item, row);
        }
        if (m_list->count()) {
            m_list->setCurrentRow(0);
            updateDetailsPane(m_list->item(0)->data(Qt::UserRole).toString());
        }
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
    QComboBox* m_formatFilter = nullptr;
    QComboBox* m_category = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_resultCount = nullptr;
    QTimer* m_searchTimer = nullptr;
    QHash<QString, QPixmap> m_thumbnailCache;
    QString m_selectedUri;
    QString m_currentThumbnailPath;

    // Enhanced Details Pane Widgets
    QWidget* m_detailsPane = nullptr;
    QLabel* m_detailIconLabel = nullptr;
    QLabel* m_detailNameLabel = nullptr;
    QLabel* m_detailBrandLabel = nullptr;
    QLabel* m_detailFormatBadge = nullptr;
    QLabel* m_detailCategoryBadge = nullptr;
    QLabel* m_detailAudioBadge = nullptr;
    QLabel* m_detailGuiBadge = nullptr;
    QLabel* m_detailPreviewLabel = nullptr;
    QLabel* m_detailUriLabel = nullptr;
    QLabel* m_detailPathLabel = nullptr;
    QLabel* m_detailParamsLabel = nullptr;
    QLabel* m_detailTagsLabel = nullptr;
    QToolButton* m_detailFavoriteBtn = nullptr;
};
}

#include <QSettings>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("RigRoom - Guitar Multieffects host");
    resize(1200, 800);
    
    m_networkManager = new QNetworkAccessManager(this);
    m_toneImageLoader = new Tone3000ImageLoader(this);
    setupRigController();
    
    // Create configs dir and automatically migrate legacy PedalBoard settings & presets
    QString newConfigDir = QDir::homePath() + "/.config/RigRoom";
    QString oldConfigDir = QDir::homePath() + "/.config/PedalBoard";
    QDir().mkpath(newConfigDir + "/presets");

    if (QDir(oldConfigDir).exists()) {
        if (!QFile::exists(newConfigDir + "/config.json") && QFile::exists(oldConfigDir + "/config.json")) {
            QFile::copy(oldConfigDir + "/config.json", newConfigDir + "/config.json");
        }
        if (!QFile::exists(newConfigDir + "/plugin-favorites.json") && QFile::exists(oldConfigDir + "/plugin-favorites.json")) {
            QFile::copy(oldConfigDir + "/plugin-favorites.json", newConfigDir + "/plugin-favorites.json");
        }
        QDir oldPresetsDir(oldConfigDir + "/presets");
        if (oldPresetsDir.exists()) {
            for (const QString& fileName : oldPresetsDir.entryList(QStringList() << "*.json", QDir::Files)) {
                if (!QFile::exists(newConfigDir + "/presets/" + fileName)) {
                    QFile::copy(oldPresetsDir.filePath(fileName), newConfigDir + "/presets/" + fileName);
                }
            }
        }
    }
    loadFavoritePlugins();
    
    // Initialize audio engine
    if (!m_engine.init("RigRoom")) {
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
    
    QFile configFile(QDir::homePath() + "/.config/RigRoom/config.json");
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
            m_audioConfigured = obj["audioConfigured"].toBool(false);
            if (obj.contains("midi")) m_midiConfig = GlobalMidiConfig::fromJson(obj["midi"].toObject());

            if (obj.contains("customLV2Paths")) {
                m_customLV2Paths.clear();
                for (const auto& val : obj["customLV2Paths"].toArray()) m_customLV2Paths.append(val.toString());
            }
            if (obj.contains("customVST3Paths")) {
                m_customVST3Paths.clear();
                for (const auto& val : obj["customVST3Paths"].toArray()) m_customVST3Paths.append(val.toString());
            }
            if (obj.contains("customCLAPPaths")) {
                m_customCLAPPaths.clear();
                for (const auto& val : obj["customCLAPPaths"].toArray()) m_customCLAPPaths.append(val.toString());
            }
        }
        configFile.close();
    }
    m_engine.setInputGain(savedInputGain);
    m_engine.setOutputGain(savedOutputGain);
    if (m_audioConfigured && !savedHwInputLeft.empty()) {
        m_engine.setHardwareInputPorts(savedHwInputLeft, savedHwInputRight, savedHwInputStereo);
    }
    if (m_audioConfigured && !savedHwOutputLeft.empty()) {
        m_engine.setHardwareOutputPorts(savedHwOutputLeft, savedHwOutputRight, savedHwOutputStereo);
    }
    
    // JACK must be active before asking it to restore the saved buffer size.
    m_engine.start();
    if (!m_midiConfig.inputPort.empty()) m_engine.setMidiInputPort(m_midiConfig.inputPort);
    m_engine.setBufferSize(savedBufferSize);

    // Scan plugins
    scanPlugins();
    
    // Setup UI
    setupUI();
    setupMidi();
    QSettings windowSettings("RigRoom", "RigRoom");
    const QByteArray windowGeometry = windowSettings.value("main_window_geometry").toByteArray();
    if (!windowGeometry.isEmpty()) restoreGeometry(windowGeometry);
    const QList<int> workspaceSizes = windowSettings.value("main_workspace_vertical_splitter").value<QList<int>>();
    QTimer::singleShot(0, this, [this, workspaceSizes]() {
        constexpr int minimumCanvasHeight = 260;
        constexpr int minimumInspectorHeight = 180;
        constexpr int defaultInspectorHeight = 260;
        const int workspaceHeight = std::max(1, m_workspaceSplitter->height());
        const int maximumInspectorHeight = std::max(minimumInspectorHeight,
                                                    workspaceHeight - minimumCanvasHeight);

        int requestedInspectorHeight = defaultInspectorHeight;
        if (workspaceSizes.size() == m_workspaceSplitter->count()) {
            const int savedTotal = std::accumulate(workspaceSizes.cbegin(), workspaceSizes.cend(), 0,
                                                   [](int total, int size) { return total + std::max(0, size); });
            if (savedTotal > 0) {
                requestedInspectorHeight = qRound(static_cast<double>(workspaceSizes.constLast())
                                                   / savedTotal * workspaceHeight);
            }
        }

        const int inspectorHeight = std::clamp(requestedInspectorHeight, minimumInspectorHeight,
                                               maximumInspectorHeight);
        m_workspaceSplitter->setSizes({std::max(minimumCanvasHeight, workspaceHeight - inspectorHeight),
                                       inspectorHeight});
    });
    m_canvas->setSystemChannelModes(m_engine.isHardwareInputStereo(), m_engine.isHardwareOutputStereo());
    m_canvas->applyRoutingChange(true);
    if (!m_audioConfigured) {
        QTimer::singleShot(0, this, [this]() {
            m_statusLabel->setText("Choose audio ports in Settings before enabling sound.");
        });
    }
    
    // Connect canvas signals
    connect(m_canvas, &NodeCanvas::editPluginUI, this, &MainWindow::onPluginDoubleClicked);
    connect(m_canvas, &NodeCanvas::nodeSelected, this, &MainWindow::onNodeSelected);
    connect(m_canvas, &NodeCanvas::nodeBypassToggled, this, &MainWindow::onNodeBypassToggled);
    connect(m_canvas, &NodeCanvas::plusButtonClicked, this, &MainWindow::onPlusButtonClicked);
    connect(m_canvas, &NodeCanvas::nodeContextMenuRequested, this, &MainWindow::onNodeContextMenuRequested);
    connect(m_canvas, &NodeCanvas::routingNodeSelected, this, &MainWindow::showRoutingNodeControls);
    connect(m_canvas, &NodeCanvas::canvasAboutToBeCleared, this, &MainWindow::closeAllPluginUIs);
    connect(m_canvas, &NodeCanvas::nodeAboutToBeRemoved, this, &MainWindow::closePluginUIForNode);
    connect(m_canvas, &NodeCanvas::routingChanged, this, [this]() {
        updateSlotControls();
        refreshSceneMarkers();
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
    int lastSlot = -1;
    QFile configFileCheck(QDir::homePath() + "/.config/RigRoom/config.json");
    if (configFileCheck.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFileCheck.readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("defaultTrackSlots")) {
                m_globalDefaultSlots = std::clamp(obj["defaultTrackSlots"].toInt(6), 4, 12);
            }
            if (obj.contains("lastPreset")) {
                lastPresetName = obj["lastPreset"].toString();
            }
            lastSlot = obj["lastSlot"].toInt(-1);
        }
        configFileCheck.close();
    }
    
    // The name wins over the slot number, in case the library was rearranged.
    int startupSlot = lastPresetName.isEmpty() ? -1 : m_presetLibrary.slotOfName(lastPresetName);
    if (startupSlot < 0 && !lastPresetName.isEmpty() && m_presetLibrary.isOccupied(lastSlot)) {
        startupSlot = lastSlot;
    }
    if (startupSlot >= 0) {
        loadSlot(startupSlot);
    } else {
        m_currentSlot = -1;
        m_currentPresetName.clear();
        setUnsavedChanges(false);
    }
}

MainWindow::~MainWindow() {
    if (m_currentDownloadReply) {
        m_currentDownloadReply->abort();
        m_currentDownloadReply->deleteLater();
    }
    // LV2 node destructors release Lilv instances, which must happen before their world is freed.
    m_engine.stop();
    delete m_canvas;
    m_canvas = nullptr;
    m_engine.clearGraph();
    m_engine.rebuildGraph();
    if (m_lilvWorld) {
        lilv_world_free(m_lilvWorld);
        m_lilvWorld = nullptr;
    }
    for (LilvWorld* world : m_retiredLilvWorlds) {
        lilv_world_free(world);
    }
    m_retiredLilvWorlds.clear();
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
    
    QLabel* logo = new QLabel("RIGROOM", this);
    logo->setStyleSheet("font-size: 20px; font-weight: bold; color: #00B0FF; letter-spacing: 2px;");
    topBar->addWidget(logo);
    
    // Preset actions live in the preset panel below; they are created here so
    // setUnsavedChanges() can restyle the Save button from the start.
    m_savePresetButton = new QPushButton("Save", this);
    m_savePresetButton->setToolTip("Save current preset (Ctrl+S)");
    m_savePresetButton->setCursor(Qt::PointingHandCursor);
    m_savePresetButton->setStyleSheet(
        "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 5px 10px; font-size: 11px; border: none; }"
        "QPushButton:hover { background-color: #009688; }"
    );
    connect(m_savePresetButton, &QPushButton::clicked, this, &MainWindow::onSavePreset);

    m_presetMenu = new QMenu(this);
    m_presetMenu->setStyleSheet(
        "QMenu { background-color: #1E1E22; color: #E0E0E0; border: 1px solid #333333; }"
        "QMenu::item:selected { background-color: #007ACC; color: white; }"
        "QMenu::item:disabled { color: #555555; }");
    QAction* newAct = m_presetMenu->addAction("New Preset");
    newAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    QAction* saveAsAct = m_presetMenu->addAction("Save As...");
    saveAsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_presetMenu->addSeparator();
    QAction* renameAct = m_presetMenu->addAction("Rename...");
    QAction* duplicateAct = m_presetMenu->addAction("Duplicate to Next Free Slot");
    QAction* deleteAct = m_presetMenu->addAction("Delete...");
    m_presetMenu->addSeparator();
    QAction* midiAct = m_presetMenu->addAction("MIDI Assignments...");
    connect(midiAct, &QAction::triggered, this, [this]() { showMidiAssignmentsDialog(); });
    QAction* browseAct = m_presetMenu->addAction("All Banks...");
    browseAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    // Shortcuts are handled by window-level QShortcuts; these only display them.
    for (QAction* act : {newAct, saveAsAct, browseAct}) act->setShortcutContext(Qt::WidgetShortcut);
    connect(newAct, &QAction::triggered, this, &MainWindow::onNewPreset);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSavePresetAs);
    connect(renameAct, &QAction::triggered, this, &MainWindow::onRenamePreset);
    connect(duplicateAct, &QAction::triggered, this, [this]() {
        if (m_currentSlot >= 0) { duplicatePresetInSlot(m_currentSlot); rebuildSlotButtons(); }
    });
    connect(deleteAct, &QAction::triggered, this, &MainWindow::onDeletePreset);
    connect(browseAct, &QAction::triggered, this, &MainWindow::onPresetButtonClicked);
    connect(m_presetMenu, &QMenu::aboutToShow, this, [this, renameAct, duplicateAct, deleteAct]() {
        const bool saved = m_currentSlot >= 0;
        renameAct->setEnabled(saved);
        duplicateAct->setEnabled(saved);
        deleteAct->setEnabled(saved);
    });

    // Canvas width belongs with the canvas: this group goes into its zoom box.
    m_columnsGroup = new QWidget(this);
    auto* columnsLayout = new QHBoxLayout(m_columnsGroup);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setSpacing(4);
    QLabel* slotTitleLabel = new QLabel("Columns", m_columnsGroup);
    slotTitleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    slotTitleLabel->setStyleSheet("color: #AAAAAA; font-weight: bold; font-size: 10px;");
    columnsLayout->addWidget(slotTitleLabel);

    m_slotMinusBtn = new QToolButton(m_columnsGroup);
    m_slotMinusBtn->setText("−");
    m_slotMinusBtn->setToolTip("Remove empty last column (Ctrl+-)");
    m_slotMinusBtn->setFixedSize(22, 22);
    m_slotMinusBtn->setCursor(Qt::PointingHandCursor);
    m_slotMinusBtn->setStyleSheet(
        "QToolButton { background-color: #262830; color: #E0E0E0; font-size: 10px; border: 1px solid #363842; border-radius: 4px; }"
        "QToolButton:hover { background-color: #363844; color: white; border-color: #00B0FF; }"
        "QToolButton:disabled { color: #555555; background-color: #1A1A1C; border-color: #252528; }"
    );
    connect(m_slotMinusBtn, &QToolButton::clicked, this, &MainWindow::onSlotMinusClicked);
    columnsLayout->addWidget(m_slotMinusBtn);

    m_slotCountLabel = new QLabel("6", m_columnsGroup);
    m_slotCountLabel->setAlignment(Qt::AlignCenter);
    m_slotCountLabel->setMinimumWidth(m_slotCountLabel->fontMetrics().horizontalAdvance("12") + 8);
    m_slotCountLabel->setStyleSheet("font-weight: bold; color: #00B0FF; padding: 0 2px; font-size: 11px;");
    columnsLayout->addWidget(m_slotCountLabel);

    m_slotPlusBtn = new QToolButton(m_columnsGroup);
    m_slotPlusBtn->setText("+");
    m_slotPlusBtn->setToolTip("Add a column (Ctrl+=)");
    m_slotPlusBtn->setFixedSize(22, 22);
    m_slotPlusBtn->setCursor(Qt::PointingHandCursor);
    m_slotPlusBtn->setStyleSheet(m_slotMinusBtn->styleSheet());
    connect(m_slotPlusBtn, &QToolButton::clicked, this, &MainWindow::onSlotPlusClicked);
    columnsLayout->addWidget(m_slotPlusBtn);

    // Global Keyboard Shortcuts
    auto* zoomResetSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(zoomResetSc, &QShortcut::activated, this, [this]() {
        if (m_canvas) m_canvas->resetZoom();
    });

    // Global Keyboard Shortcuts
    auto* saveSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(saveSc, &QShortcut::activated, this, &MainWindow::onSavePreset);

    auto* newSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
    connect(newSc, &QShortcut::activated, this, &MainWindow::onNewPreset);

    auto* prevSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this);
    connect(prevSc, &QShortcut::activated, this, &MainWindow::onPrevPreset);

    auto* nextSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this);
    connect(nextSc, &QShortcut::activated, this, &MainWindow::onNextPreset);

    auto* prevBankSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp), this);
    connect(prevBankSc, &QShortcut::activated, this, &MainWindow::onPrevBank);

    auto* nextBankSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageDown), this);
    connect(nextBankSc, &QShortcut::activated, this, &MainWindow::onNextBank);

    auto* browseSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), this);
    connect(browseSc, &QShortcut::activated, this, &MainWindow::onPresetButtonClicked);

    auto* saveAsSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this);
    connect(saveAsSc, &QShortcut::activated, this, &MainWindow::onSavePresetAs);
    
    topBar->addStretch();
    
    // Setup Settings Dialog
    m_settingsDialog = new QDialog(this);
    m_settingsDialog->setWindowTitle("Application Settings");
    m_settingsDialog->setMinimumWidth(860);
    m_settingsDialog->setMinimumHeight(640);
    m_settingsDialog->resize(920, 860);
    m_settingsDialog->setStyleSheet(styleSheet());
    
    QVBoxLayout* dialogLayout = new QVBoxLayout(m_settingsDialog);
    dialogLayout->setContentsMargins(15, 15, 15, 15);
    dialogLayout->setSpacing(12);

    QTabWidget* mainSettingsTab = new QTabWidget(m_settingsDialog);
    mainSettingsTab->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #282832; background: #121216; border-radius: 6px; padding: 6px; }"
        "QTabBar::tab { background: #1a1a20; color: #A0A0B0; padding: 8px 16px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; font-weight: bold; font-size: 12px; }"
        "QTabBar::tab:selected { background: #00B0FF; color: white; }"
    );

    // TAB 1: Audio Configuration
    QWidget* audioTab = new QWidget();
    QVBoxLayout* audioTabLayout = new QVBoxLayout(audioTab);
    audioTabLayout->setContentsMargins(10, 10, 10, 10);
    
    QGroupBox* ioBox = new QGroupBox("Audio & Hardware Configuration", audioTab);
    ioBox->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333338; border-radius: 6px; margin-top: 10px; padding: 15px; background: #1a1a1f; }"
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
    
    auto* slotMinusSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(slotMinusSc, &QShortcut::activated, this, &MainWindow::onSlotMinusClicked);

    auto* slotPlusSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    connect(slotPlusSc, &QShortcut::activated, this, &MainWindow::onSlotPlusClicked);

    QComboBox* defaultTrackSlotsCombo = new QComboBox(m_settingsDialog);
    defaultTrackSlotsCombo->addItems({"4", "6", "8", "10", "12"});
    int defaultIdx = defaultTrackSlotsCombo->findText(QString::number(m_globalDefaultSlots));
    if (defaultIdx != -1) defaultTrackSlotsCombo->setCurrentIndex(defaultIdx);
    connect(defaultTrackSlotsCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        m_globalDefaultSlots = std::clamp(text.toInt(), 4, 12);
        saveConfigSettings();
    });
    
    formLayout->addRow("Input Mode:", m_hwInputModeCombo);
    formLayout->addRow("Input Device:", m_hwInputCombo);
    formLayout->addRow("Output Mode:", m_hwOutputModeCombo);
    formLayout->addRow("Output Device:", m_hwOutputCombo);
    formLayout->addRow("Buffer Size:", m_bufferSizeCombo);
    formLayout->addRow("Default Columns:", defaultTrackSlotsCombo);

    // Preset library: banks of 4 (A-D). Presets stay pinned to their bank and
    // letter; the bank count can't drop below the highest bank in use.
    QSpinBox* numBanksSpin = new QSpinBox(m_settingsDialog);
    numBanksSpin->setRange(1, 128);
    numBanksSpin->setToolTip("Number of preset banks (4 presets each, A-D). "
                             "Can't be set below the highest bank that holds a preset.");
    numBanksSpin->setKeyboardTracking(false);
    auto syncLibraryLayoutControls = [this, numBanksSpin]() {
        QSignalBlocker blocker(numBanksSpin);
        numBanksSpin->setMinimum(m_presetLibrary.minBanks());
        numBanksSpin->setValue(m_presetLibrary.numBanks());
    };
    connect(numBanksSpin, &QSpinBox::valueChanged, this, [this](int banks) {
        m_presetLibrary.setNumBanks(banks);
        m_presetLibrary.save();
        rebuildSlotButtons();
    });
    // The minimum depends on where presets are; refresh it each time Settings opens.
    connect(m_settingsDialog, &QDialog::finished, this, syncLibraryLayoutControls);
    QTimer::singleShot(0, this, syncLibraryLayoutControls);
    formLayout->addRow("Preset Banks:", numBanksSpin);
    
    audioTabLayout->addWidget(ioBox);
    audioTabLayout->addStretch();
    int audioTabIdx = mainSettingsTab->addTab(audioTab, "Audio");
    m_settingsTabs = mainSettingsTab;
    m_midiTabIndex = mainSettingsTab->addTab(buildMidiSettingsTab(), "MIDI");
    mainSettingsTab->setTabIcon(audioTabIdx, style()->standardIcon(QStyle::SP_MediaVolume));

    // TAB 2: Plugins & Formats
    QWidget* pluginsTab = new QWidget();
    QVBoxLayout* pluginsLayout = new QVBoxLayout(pluginsTab);
    pluginsLayout->setContentsMargins(10, 10, 10, 10);
    pluginsLayout->setSpacing(12);

    QGroupBox* pathsBox = new QGroupBox("Custom Plugin Search Directories", pluginsTab);
    pathsBox->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333338; border-radius: 6px; margin-top: 10px; padding: 12px; background: #1a1a1f; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
    );
    QVBoxLayout* pathsLayout = new QVBoxLayout(pathsBox);
    pathsLayout->setSpacing(8);

    QTabWidget* formatPathsTab = new QTabWidget(pathsBox);
    formatPathsTab->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #282832; background: #141418; border-radius: 4px; }"
        "QTabBar::tab { background: #222228; color: #A0A0B0; padding: 6px 14px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QTabBar::tab:selected { background: #00B0FF; color: white; }"
    );

    auto createPathPage = [this](QStringList& pathList) -> QWidget* {
        QWidget* page = new QWidget();
        QHBoxLayout* pageLayout = new QHBoxLayout(page);
        pageLayout->setContentsMargins(8, 8, 8, 8);
        
        QListWidget* listWidget = new QListWidget(page);
        listWidget->setStyleSheet("QListWidget { background: #1c1c22; border: 1px solid #2d2d38; border-radius: 4px; color: #ECECF0; font-size: 11px; }");
        for (const auto& path : pathList) {
            listWidget->addItem(path);
        }

        QVBoxLayout* btnCol = new QVBoxLayout();
        btnCol->setSpacing(6);

        QPushButton* addBtn = new QPushButton("Add Folder", page);
        addBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        addBtn->setStyleSheet("QPushButton { background: #282834; color: #00B0FF; font-weight: bold; border: 1px solid #00B0FF; border-radius: 4px; padding: 6px 12px; font-size: 11px; } QPushButton:hover { background: #00B0FF; color: white; }");
        
        QPushButton* removeBtn = new QPushButton("Remove", page);
        removeBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
        removeBtn->setStyleSheet("QPushButton { background: #352528; color: #FF6B6B; font-weight: bold; border: 1px solid #4a3034; border-radius: 4px; padding: 6px 12px; font-size: 11px; } QPushButton:hover { background: #4a2d32; color: #FF8787; border-color: #FF5252; }");

        btnCol->addWidget(addBtn);
        btnCol->addWidget(removeBtn);
        btnCol->addStretch();

        pageLayout->addWidget(listWidget, 1);
        pageLayout->addLayout(btnCol);

        connect(addBtn, &QPushButton::clicked, this, [this, page, listWidget, &pathList]() {
            QString dir = QFileDialog::getExistingDirectory(page, "Select Plugin Directory", QDir::homePath());
            if (!dir.isEmpty() && !pathList.contains(dir)) {
                pathList.append(dir);
                listWidget->addItem(dir);
                saveConfigSettings();
            }
        });

        connect(removeBtn, &QPushButton::clicked, this, [this, listWidget, &pathList]() {
            int row = listWidget->currentRow();
            if (row >= 0) {
                QString path = listWidget->item(row)->text();
                pathList.removeAll(path);
                delete listWidget->takeItem(row);
                saveConfigSettings();
            }
        });

        return page;
    };

    formatPathsTab->addTab(createPathPage(m_customLV2Paths), "LV2 Search Paths");
    formatPathsTab->addTab(createPathPage(m_customVST3Paths), "VST3 Search Paths");
    formatPathsTab->addTab(createPathPage(m_customCLAPPaths), "CLAP Search Paths");

    pathsLayout->addWidget(formatPathsTab);
    pluginsLayout->addWidget(pathsBox);

    QHBoxLayout* rescanLayout = new QHBoxLayout();
    QPushButton* rescanBtn = new QPushButton("Rescan Plugins Now", pluginsTab);
    rescanBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setCursor(Qt::PointingHandCursor);
    rescanBtn->setStyleSheet(
        "QPushButton { background: #00B0FF; color: white; font-weight: bold; border-radius: 4px; padding: 8px 16px; font-size: 12px; }"
        "QPushButton:hover { background: #0091EA; }"
    );
    rescanLayout->addWidget(rescanBtn);
    rescanLayout->addStretch();
    pluginsLayout->addLayout(rescanLayout);

    connect(rescanBtn, &QPushButton::clicked, this, [this, rescanBtn]() {
        QSet<QString> previousUris;
        for (const auto& plugin : m_availablePlugins) {
            previousUris.insert(QString::fromStdString(plugin.uri));
        }

        rescanBtn->setText("⏳ Scanning...");
        rescanBtn->setEnabled(false);
        qApp->processEvents();
        
        scanPlugins();

        int newLV2 = 0, newCLAP = 0, newVST3 = 0;
        for (const auto& plugin : m_availablePlugins) {
            const QString uri = QString::fromStdString(plugin.uri);
            if (previousUris.contains(uri)) continue;

            if (plugin.isLV2) {
                ++newLV2;
            } else if (uri.contains(".clap")) {
                ++newCLAP;
            } else {
                ++newVST3;
            }
        }
        const int newCount = newLV2 + newCLAP + newVST3;

        rescanBtn->setText("Plugins Rescanned!");
        rescanBtn->setEnabled(true);
        QTimer::singleShot(1500, this, [rescanBtn]() {
            rescanBtn->setText("Rescan Plugins Now");
        });

        auto* scanSummary = new QMessageBox(QMessageBox::Information, "Plugin Rescan", {}, QMessageBox::Ok, this);
        scanSummary->setAttribute(Qt::WA_DeleteOnClose);
        if (newCount == 0) {
            scanSummary->setText(QString("Scan complete. No new plugins found. %1 plugins are currently available.")
                .arg(m_availablePlugins.size()));
        } else {
            scanSummary->setText(QString("Scan complete: %1 new plugin%2 found.")
                .arg(newCount)
                .arg(newCount == 1 ? "" : "s"));
            QStringList formatCounts;
            if (newLV2 > 0) formatCounts << QString("LV2: %1").arg(newLV2);
            if (newCLAP > 0) formatCounts << QString("CLAP: %1").arg(newCLAP);
            if (newVST3 > 0) formatCounts << QString("VST3: %1").arg(newVST3);
            scanSummary->setInformativeText(formatCounts.join("  ·  "));
        }
        scanSummary->show();
    });

    int pluginsTabIdx = mainSettingsTab->addTab(pluginsTab, "Plugins & Formats");
    mainSettingsTab->setTabIcon(pluginsTabIdx, style()->standardIcon(QStyle::SP_FileDialogDetailedView));

    // TAB 3: TONE3000 Integration
    QWidget* toneTab = new QWidget();
    QVBoxLayout* toneTabLayout = new QVBoxLayout(toneTab);
    toneTabLayout->setContentsMargins(10, 10, 10, 10);
    
    QGroupBox* toneBox = new QGroupBox("TONE3000 Integration", toneTab);
    toneBox->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333338; border-radius: 6px; margin-top: 10px; padding: 15px; background: #1a1a1f; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
    );
    QVBoxLayout* toneLayout = new QVBoxLayout(toneBox);
    toneLayout->setSpacing(10);

    QLabel* toneStatusLabel = new QLabel(toneBox);
    toneStatusLabel->setStyleSheet("font-size: 11px; font-weight: bold; border: none; background: transparent;");

    QHBoxLayout* keyInputLayout = new QHBoxLayout();
    keyInputLayout->setSpacing(6);

    QLineEdit* apiKeyEdit = new QLineEdit(m_settingsDialog);
    apiKeyEdit->setPlaceholderText("Paste t3k_cs_... Secret Key or Legacy API Key");
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setStyleSheet("QLineEdit { background: #222228; border: 1px solid #363642; border-radius: 4px; color: #ECECF0; padding: 6px; font-size: 11px; }");

    QToolButton* toggleEyeBtn = new QToolButton(m_settingsDialog);
    toggleEyeBtn->setText("Hide");
    toggleEyeBtn->setToolTip("Show / Hide API Key");
    toggleEyeBtn->setFixedSize(28, 28);
    toggleEyeBtn->setCursor(Qt::PointingHandCursor);
    toggleEyeBtn->setStyleSheet(
        "QToolButton { background-color: #282832; color: #E0E0E0; border: 1px solid #383848; border-radius: 4px; font-size: 12px; }"
        "QToolButton:hover { background-color: #343442; color: white; border-color: #00B0FF; }"
    );
    connect(toggleEyeBtn, &QToolButton::clicked, apiKeyEdit, [apiKeyEdit, toggleEyeBtn]() {
        if (apiKeyEdit->echoMode() == QLineEdit::Password) {
            apiKeyEdit->setEchoMode(QLineEdit::Normal);
            toggleEyeBtn->setText("Show");
        } else {
            apiKeyEdit->setEchoMode(QLineEdit::Password);
            toggleEyeBtn->setText("Hide");
        }
    });

    QPushButton* clearKeyBtn = new QPushButton("Clear", m_settingsDialog);
    clearKeyBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    clearKeyBtn->setToolTip("Remove stored API key");
    clearKeyBtn->setCursor(Qt::PointingHandCursor);
    clearKeyBtn->setStyleSheet(
        "QPushButton { background: #352528; color: #FF6B6B; font-weight: bold; border: 1px solid #4a3034; border-radius: 4px; padding: 5px 10px; font-size: 11px; }"
        "QPushButton:hover { background: #4a2d32; color: #FF8787; border-color: #FF5252; }"
        "QPushButton:disabled { color: #555555; background: #1c1c20; border-color: #28282c; }"
    );

    keyInputLayout->addWidget(apiKeyEdit, 1);
    keyInputLayout->addWidget(toggleEyeBtn);
    keyInputLayout->addWidget(clearKeyBtn);

    auto updateKeyStatus = [apiKeyEdit, toneStatusLabel, clearKeyBtn]() {
        QString text = apiKeyEdit->text().trimmed();
        if (text.isEmpty()) {
            toneStatusLabel->setText("Secret Key Required: Enter your Secret Key (t3k_cs_...) to enable online searches");
            toneStatusLabel->setStyleSheet("color: #FFB74D; font-size: 11px; font-weight: bold; border: none;");
            clearKeyBtn->setEnabled(false);
        } else {
            toneStatusLabel->setText("Mode: Secret Key Configured — Full TONE3000 API Access");
            toneStatusLabel->setStyleSheet("color: #4CAF50; font-size: 11px; font-weight: bold; border: none;");
            clearKeyBtn->setEnabled(true);
        }
    };

    m_apiKeyEdit = apiKeyEdit;
    m_updateKeyStatusFunc = updateKeyStatus;

    // Load existing saved value
    {
        apiKeyEdit->setText(CredentialStore::tone3000ApiKey());
    }
    updateKeyStatus();

    connect(apiKeyEdit, &QLineEdit::textChanged, this, [this, updateKeyStatus](const QString& text) {
        QString error;
        if (text.trimmed().isEmpty()) CredentialStore::clearTone3000ApiKey(&error);
        else CredentialStore::setTone3000ApiKey(text.trimmed(), &error);
        if (!error.isEmpty()) m_statusLabel->setText("TONE3000 key will be kept for this session only: " + error);
        updateKeyStatus();
    });

    connect(clearKeyBtn, &QPushButton::clicked, this, [apiKeyEdit]() {
        apiKeyEdit->clear();
        CredentialStore::clearTone3000ApiKey();
    });

    QLabel* helpLabel = new QLabel(
        "<b>Which key is needed?</b> Copy your <b>Secret Key</b> (starts with <code>t3k_cs_...</code>) from your TONE3000 account.<br>"
        "<span style='color:#a0a4b8;'>Path: <b>tone3000.com</b> → <b>Settings</b> → <b>API & Developer Keys</b> → <b>Secret Key</b></span><br>"
        "<a href='https://tone3000.com/settings' style='color:#00B0FF; font-weight:bold;'>Open tone3000.com/settings in browser</a>", toneBox);
    helpLabel->setStyleSheet("font-size: 11px; color: #CCCCCC; border: none; padding-top: 4px;");
    helpLabel->setOpenExternalLinks(true);

    toneLayout->addWidget(toneStatusLabel);
    toneLayout->addLayout(keyInputLayout);
    toneLayout->addWidget(helpLabel);
    toneTabLayout->addWidget(toneBox);
    toneTabLayout->addStretch();
    int toneTabIdx = mainSettingsTab->addTab(toneTab, "TONE3000");
    mainSettingsTab->setTabIcon(toneTabIdx, style()->standardIcon(QStyle::SP_DriveNetIcon));

    AboutWidget* aboutWidget = new AboutWidget(m_settingsDialog);
    mainSettingsTab->addTab(aboutWidget, "ℹ️ About");

    dialogLayout->addWidget(mainSettingsTab);
    
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
        m_inputClipHoldTicks = 0;
        inputPopover->showBelow(m_inputGainLabel);
    });
    
    topBar->addSpacing(15);
    
    // Top bar Out controls
    m_outputGainLabel = new QToolButton(this);
    m_outputGainLabel->setText(QString("Master Out  %1 dB").arg(m_engine.getOutputGainDB(), 0, 'f', 1));
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
            m_outputGainLabel->setText(QString("Master Out  %1 dB").arg(value, 0, 'f', 1));
        },
        [this] { saveConfigSettings(); }, this);
    m_outputPopupMeter = outputPopover->meter();
    connect(m_outputGainLabel, &QToolButton::clicked, this, [outputPopover, this] {
        m_outputClipHoldTicks = 0;
        outputPopover->showBelow(m_outputGainLabel);
    });
    
    topBar->addSpacing(15);
    
    // Settings Button
    QPushButton* settingsBtn = new QPushButton("Settings", this);
    settingsBtn->setToolTip("Open audio input/output and buffer settings");
    connect(settingsBtn, &QPushButton::clicked, this, [this]() { openSettings(); });
    topBar->addWidget(settingsBtn);

    topBar->addSpacing(15);

    mainLayout->addWidget(topBarWidget);

    // --- PRESET PANEL: Bank | Presets (A-D) | Loaded preset | Scenes ---
    // Laid out like a floor unit: bank up/down chooses which presets the A-D
    // switches show, a switch loads one, and scenes vary the loaded preset.
    auto* panel = new QFrame(central);
    panel->setObjectName("presetPanel");
    panel->setStyleSheet(
        "QFrame#presetPanel { background-color: #17171A; border: 1px solid #2A2A30; border-radius: 8px; }"
        "QFrame#presetPanel QLabel { background: transparent; border: none; }");
    auto* panelLayout = new QHBoxLayout(panel);
    panelLayout->setContentsMargins(10, 6, 10, 8);
    panelLayout->setSpacing(12);

    auto makeCaption = [panel](const QString& text) {
        auto* caption = new QLabel(text, panel);
        caption->setStyleSheet("color: #7A7A86; font-weight: bold; font-size: 9px; letter-spacing: 1px;");
        return caption;
    };
    auto makeSection = [panel](QBoxLayout*& body, const QString& caption, auto makeCaptionFn) {
        auto* section = new QWidget(panel);
        auto* v = new QVBoxLayout(section);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        v->addWidget(makeCaptionFn(caption));
        body = new QHBoxLayout();
        body->setSpacing(4);
        v->addLayout(body);
        return section;
    };
    auto makeDivider = [panel]() {
        auto* divider = new QFrame(panel);
        divider->setFrameShape(QFrame::VLine);
        divider->setStyleSheet("QFrame { color: #2A2A30; }");
        return divider;
    };
    const QString smallBtnStyle =
        "QToolButton { background-color: #242528; color: #00B0FF; font-size: 11px; border: 1px solid #333438; border-radius: 4px; }"
        "QToolButton:hover { background-color: #303236; color: white; }";

    // Bank
    QBoxLayout* bankBody = nullptr;
    QWidget* bankSection = makeSection(bankBody, "BANK", makeCaption);
    m_bankPrevBtn = new QToolButton(bankSection);
    m_bankPrevBtn->setText("◀");
    m_bankPrevBtn->setToolTip("Show previous bank (Ctrl+Shift+PageUp). Nothing loads until you pick a preset.");
    m_bankPrevBtn->setFixedSize(22, 44);
    m_bankPrevBtn->setCursor(Qt::PointingHandCursor);
    m_bankPrevBtn->setStyleSheet(smallBtnStyle);
    connect(m_bankPrevBtn, &QToolButton::clicked, this, [this]() { m_rig->stepBank(-1); });
    bankBody->addWidget(m_bankPrevBtn);

    auto* bankMiddle = new QVBoxLayout();
    bankMiddle->setSpacing(2);
    m_bankLabel = new QLabel(bankSection);
    m_bankLabel->setAlignment(Qt::AlignCenter);
    m_bankLabel->setFixedWidth(118);
    m_bankLabel->setFixedHeight(24);
    m_bankLabel->installEventFilter(this);
    bankMiddle->addWidget(m_bankLabel);
    m_slotGridBtn = new QToolButton(bankSection);
    m_slotGridBtn->setText("▦  All banks");
    m_slotGridBtn->setToolTip("Grid of all banks (Ctrl+P): move, swap, rename and browse presets");
    m_slotGridBtn->setFixedWidth(118);
    m_slotGridBtn->setFixedHeight(18);
    m_slotGridBtn->setCursor(Qt::PointingHandCursor);
    m_slotGridBtn->setStyleSheet(
        "QToolButton { background: transparent; color: #8A8A96; font-size: 10px; border: none; }"
        "QToolButton:hover { color: #00B0FF; }");
    connect(m_slotGridBtn, &QToolButton::clicked, this, &MainWindow::onPresetButtonClicked);
    bankMiddle->addWidget(m_slotGridBtn);
    bankBody->addLayout(bankMiddle);

    m_bankNextBtn = new QToolButton(bankSection);
    m_bankNextBtn->setText("▶");
    m_bankNextBtn->setToolTip("Show next bank (Ctrl+Shift+PageDown). Nothing loads until you pick a preset.");
    m_bankNextBtn->setFixedSize(22, 44);
    m_bankNextBtn->setCursor(Qt::PointingHandCursor);
    m_bankNextBtn->setStyleSheet(smallBtnStyle);
    connect(m_bankNextBtn, &QToolButton::clicked, this, [this]() { m_rig->stepBank(1); });
    bankBody->addWidget(m_bankNextBtn);
    panelLayout->addWidget(bankSection);
    panelLayout->addWidget(makeDivider());

    // Presets in the shown bank
    QBoxLayout* slotBody = nullptr;
    QWidget* slotSection = makeSection(slotBody, "PRESETS", makeCaption);
    m_slotBarLayout = static_cast<QHBoxLayout*>(slotBody);
    panelLayout->addWidget(slotSection, 0);
    panelLayout->addWidget(makeDivider());

    // Scenes of the loaded preset
    auto* sceneSection = new QWidget(panel);
    auto* sceneSectionLayout = new QVBoxLayout(sceneSection);
    sceneSectionLayout->setContentsMargins(0, 0, 0, 0);
    sceneSectionLayout->setSpacing(3);
    auto* sceneCaption = makeCaption("SCENES");
    sceneCaption->setToolTip("Scenes switch blocks on/off, per-scene parameters and scene level without reloading plugins.\n"
                             "Alt+1..8 selects a scene, Alt+Left/Right steps. Double-click to rename, right-click for more.");
    sceneSectionLayout->addWidget(sceneCaption);
    m_sceneBar = new QWidget(sceneSection);
    m_sceneBarLayout = new QHBoxLayout(m_sceneBar);
    m_sceneBarLayout->setContentsMargins(0, 0, 0, 0);
    m_sceneBarLayout->setSpacing(4);
    sceneSectionLayout->addWidget(m_sceneBar);
    panelLayout->addWidget(sceneSection, 0);
    panelLayout->addStretch(1);

    // Preset actions: out of the way at the right edge, but always visible.
    auto* actionsSection = new QWidget(panel);
    auto* actionsLayout = new QVBoxLayout(actionsSection);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(3);
    actionsLayout->addWidget(makeCaption("PRESET"));
    auto* actionButtons = new QHBoxLayout();
    actionButtons->setSpacing(4);
    m_savePresetButton->setParent(actionsSection);
    m_savePresetButton->setFixedHeight(44);
    actionButtons->addWidget(m_savePresetButton);
    auto* moreBtn = new QToolButton(actionsSection);
    moreBtn->setText("⋯");
    moreBtn->setToolTip("New, Save As, Rename, Duplicate, Delete, All banks");
    moreBtn->setFixedSize(32, 44);
    moreBtn->setCursor(Qt::PointingHandCursor);
    moreBtn->setStyleSheet(
        "QToolButton { background-color: #2A2A30; color: #E0E0E0; font-weight: bold; font-size: 15px; border-radius: 5px; border: 1px solid #35363C; }"
        "QToolButton:hover { background-color: #3A3A42; }");
    connect(moreBtn, &QToolButton::clicked, this, [this, moreBtn]() {
        m_presetMenu->exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height())));
    });
    actionButtons->addWidget(moreBtn);
    actionsLayout->addLayout(actionButtons);
    panelLayout->addWidget(actionsSection, 0);

    mainLayout->addWidget(panel);

    for (int i = 0; i < SceneModel::kMaxScenes; ++i) {
        auto* sceneSc = new QShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + i)), this);
        connect(sceneSc, &QShortcut::activated, this, [this, i]() { m_rig->selectScene(i); });
    }
    auto* prevSceneSc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    connect(prevSceneSc, &QShortcut::activated, this, [this]() { m_rig->stepScene(-1); });
    auto* nextSceneSc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    connect(nextSceneSc, &QShortcut::activated, this, [this]() { m_rig->stepScene(1); });
    rebuildSceneBar();
    
    // --- WORKSPACE SPLITTER ---
    QSplitter* midSplitter = m_workspaceSplitter = new QSplitter(Qt::Vertical, this);
    
    // Center: Node Graph Canvas
    m_canvas = new NodeCanvas(&m_engine, this);
    m_canvas->setMinimumHeight(260);
    m_canvas->addZoomOverlayWidget(m_columnsGroup);
    {
        QSettings canvasSettings("RigRoom", "RigRoom");
        m_canvas->setAutoFit(canvasSettings.value("canvas_auto_fit", false).toBool());
        connect(m_canvas, &NodeCanvas::autoFitChanged, this, [](bool enabled) {
            QSettings("RigRoom", "RigRoom").setValue("canvas_auto_fit", enabled);
        });
    }
    m_presetNameLabel = m_canvas->infoOverlay();
    m_presetNameLabel->setCursor(Qt::PointingHandCursor);
    m_presetNameLabel->installEventFilter(this);
    midSplitter->addWidget(m_canvas);
    midSplitter->setStretchFactor(0, 1);
    
    // Bottom inspector: responsive control deck
    m_paramContainer = new QWidget(this);
    m_paramContainer->setMinimumHeight(180);
    // The scroll area owns overflow; inspector content must not resize the workspace splitter.
    m_paramContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_paramContainer->setObjectName("inspectorDeck");
    m_paramContainer->setStyleSheet(
        "QWidget#inspectorDeck { background:#18191D; border-top:1px solid #30323A; }"
        "QWidget#inspectorDeck QCheckBox { color:#CDD3DC; spacing:7px; }"
        "QWidget#inspectorDeck QPushButton:focus, QWidget#inspectorDeck QComboBox:focus, QWidget#inspectorDeck QSpinBox:focus { border:1px solid #73C6F1; }"
    );
    
    QVBoxLayout* rightLayout = new QVBoxLayout(m_paramContainer);
    rightLayout->setContentsMargins(12, 10, 12, 10);
    rightLayout->setSpacing(8);
    
    m_noParamLabel = new QLabel("Select an effect node\nto show parameters", this);
    m_noParamLabel->setAlignment(Qt::AlignCenter);
    m_noParamLabel->setStyleSheet("color: #666666; font-style: italic;");
    rightLayout->addWidget(m_noParamLabel);
    
    auto* paramScroll = m_paramScroll = new QScrollArea(m_paramContainer);
    paramScroll->setWidgetResizable(true);
    paramScroll->setFrameShape(QFrame::NoFrame);
    paramScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    paramScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    paramScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* paramContent = new QWidget(paramScroll);
    auto* paramContentLayout = new QHBoxLayout(paramContent);
    paramContentLayout->setContentsMargins(0, 0, 0, 0);
    paramContentLayout->setSpacing(0);
    auto* paramShelf = new QWidget(paramContent);
    paramShelf->setObjectName("paramShelf");
    paramShelf->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    paramShelf->setStyleSheet("QWidget#paramShelf { background: transparent; }");
    m_paramLayout = new QVBoxLayout(paramShelf);
    m_paramLayout->setContentsMargins(0, 0, 0, 0);
    m_paramLayout->setSpacing(10);
    m_paramLayout->setAlignment(Qt::AlignTop);
    paramContentLayout->addWidget(paramShelf, 1);
    paramScroll->setWidget(paramContent);
    rightLayout->addWidget(paramScroll, 1);
    
    midSplitter->addWidget(m_paramContainer);
    midSplitter->setStretchFactor(1, 0);
    
    mainLayout->addWidget(midSplitter);
    
    // --- STATUS BAR ---
    QWidget* statusBarContainer = new QWidget(this);
    statusBarContainer->setFixedHeight(24);
    statusBarContainer->setStyleSheet("background-color: #18181B; border-top: 1px solid #29292D;");
    QHBoxLayout* statusLayout = new QHBoxLayout(statusBarContainer);
    statusLayout->setContentsMargins(8, 0, 8, 0);

    m_statusLabel = new QLabel("Ready | JACK Latency: " + QString::number(currentSize * 1000.0 / m_engine.getSampleRate(), 'f', 2) + " ms", this);
    m_statusLabel->setStyleSheet("color: #888888; font-size: 11px;");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    m_midiIndicator = new QToolButton(this);
    m_midiIndicator->setText(QString::fromUtf8("● MIDI"));
    m_midiIndicator->setCursor(Qt::PointingHandCursor);
    m_midiIndicator->setToolTip("MIDI input activity. Click for MIDI settings.");
    m_midiIndicator->setStyleSheet(
        "QToolButton { background-color: #263238; color: #546E7A; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
        "QToolButton:hover { background-color: #37474F; }");
    connect(m_midiIndicator, &QToolButton::clicked, this, [this]() { openSettings(m_midiTabIndex); });
    statusLayout->addWidget(m_midiIndicator);
    statusLayout->addSpacing(8);

    m_dspCpuButton = new QToolButton(this);
    m_dspCpuButton->setText("DSP 0%");
    m_dspCpuButton->setToolTip("DSP CPU Load. Click to reset peak load memory.");
    m_dspCpuButton->setCursor(Qt::PointingHandCursor);
    m_dspCpuButton->setStyleSheet(
        "QToolButton { background-color: #263238; color: #80CBC4; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
        "QToolButton:hover { background-color: #37474F; }"
    );
    connect(m_dspCpuButton, &QToolButton::clicked, this, [this]() {
        m_peakCpuLoad = m_smoothedCpuLoad;
        m_engine.resetMaxProcessDurationUsec();
    });
    statusLayout->addWidget(m_dspCpuButton);
    statusLayout->addSpacing(8);

    m_xrunButton = new QToolButton(this);
    m_xrunButton->setText("XRuns: 0");
    m_xrunButton->setToolTip("Click to reset XRun dropout counter");
    m_xrunButton->setCursor(Qt::PointingHandCursor);
    m_xrunButton->setStyleSheet(
        "QToolButton { background-color: #263238; color: #80CBC4; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
        "QToolButton:hover { background-color: #37474F; }"
    );
    connect(m_xrunButton, &QToolButton::clicked, this, [this]() {
        m_engine.resetXRunCount();
        m_xrunButton->setText("XRuns: 0");
        m_xrunButton->setStyleSheet(
            "QToolButton { background-color: #263238; color: #80CBC4; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
            "QToolButton:hover { background-color: #37474F; }"
        );
    });
    statusLayout->addWidget(m_xrunButton);

    mainLayout->addWidget(statusBarContainer);
}

void MainWindow::scanPlugins() {
    m_availablePlugins.clear();

    if (m_lilvWorld) {
        const auto nodes = m_engine.getNodes();
        const bool hasLiveLV2Node = std::any_of(nodes.begin(), nodes.end(), [](const auto& node) {
            return node && node->getType() == NodeType::LV2Plugin;
        });
        if (hasLiveLV2Node) {
            // Live LV2 nodes retain pointers owned by the world they were created from.
            m_retiredLilvWorlds.push_back(m_lilvWorld);
        } else {
            lilv_world_free(m_lilvWorld);
        }
        m_lilvWorld = nullptr;
    }

    // Scan LV2 plugins
    QStringList defaultLv2Paths = {
        "/usr/lib/lv2",
        "/usr/lib64/lv2",
        "/usr/local/lib/lv2",
        "/app/extensions/Plugins/lv2",
        QDir::homePath() + "/.lv2"
    };
    QStringList allLv2Paths = defaultLv2Paths + m_customLV2Paths;
    allLv2Paths.removeDuplicates();
    QString lv2PathEnv = allLv2Paths.join(":");
    qputenv("LV2_PATH", lv2PathEnv.toUtf8());

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

        // Extract LV2 port details & bundle path
        LilvNode* audioPortClass = lilv_new_uri(m_lilvWorld, LILV_URI_AUDIO_PORT);
        LilvNode* inputPortClass = lilv_new_uri(m_lilvWorld, LILV_URI_INPUT_PORT);
        LilvNode* outputPortClass = lilv_new_uri(m_lilvWorld, LILV_URI_OUTPUT_PORT);
        LilvNode* controlPortClass = lilv_new_uri(m_lilvWorld, LILV_URI_CONTROL_PORT);

        uint32_t numPorts = lilv_plugin_get_num_ports(p);
        info.audioInputs = 0;
        info.audioOutputs = 0;
        info.controlPorts = 0;
        for (uint32_t portIdx = 0; portIdx < numPorts; ++portIdx) {
            const LilvPort* port = lilv_plugin_get_port_by_index(p, portIdx);
            if (lilv_port_is_a(p, port, audioPortClass)) {
                if (lilv_port_is_a(p, port, inputPortClass)) info.audioInputs++;
                else if (lilv_port_is_a(p, port, outputPortClass)) info.audioOutputs++;
            } else if (lilv_port_is_a(p, port, controlPortClass)) {
                info.controlPorts++;
            }
        }

        const LilvNode* bundleUri = lilv_plugin_get_bundle_uri(p);
        if (bundleUri) {
            if (char* bundlePath = lilv_file_uri_parse(lilv_node_as_uri(bundleUri), nullptr)) {
                info.path = bundlePath;
                lilv_free(bundlePath);
            }
        }

        // Fallback: resolve modgui thumbnail from bundle directory if not set by Lilv property
        if (info.thumbnailPath.isEmpty() && !info.path.empty()) {
            QString pathStr = QString::fromStdString(info.path);
            if (pathStr.endsWith('/')) pathStr.chop(1);
            QDir modguiDir(pathStr + "/modgui");
            if (modguiDir.exists()) {
                QStringList filters = {"*thumb*.png", "*Thumb*.png", "*screenshot*.png", "*icon*.png", "*.png", "*.jpg"};
                for (const auto& filter : filters) {
                    QStringList files = modguiDir.entryList({filter}, QDir::Files);
                    if (!files.isEmpty()) {
                        info.thumbnailPath = modguiDir.filePath(files.first());
                        break;
                    }
                }
            }
        }

        const LilvUIs* uis = lilv_plugin_get_uis(p);
        info.hasNativeGUI = (uis && lilv_uis_size(uis) > 0);

        lilv_node_free(audioPortClass);
        lilv_node_free(inputPortClass);
        lilv_node_free(outputPortClass);
        lilv_node_free(controlPortClass);
        
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
    
    // Scan VST3 plugins
    std::vector<std::string> vst3Dirs = {
        "/usr/lib/vst3",
        "/usr/lib64/vst3",
        "/usr/local/lib/vst3",
        "/app/extensions/Plugins/vst3",
        (QDir::homePath() + "/.vst3").toStdString()
    };
    for (const auto& p : m_customVST3Paths) {
        if (!p.isEmpty()) vst3Dirs.push_back(p.toStdString());
    }
    
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
                    std::string category = "Utilities";
                    std::string lowerName = QString::fromStdString(name).toLower().toStdString();
                    if (lowerName.find("delay") != std::string::npos) category = "Delays";
                    else if (lowerName.find("reverb") != std::string::npos) category = "Reverbs";
                    else if (lowerName.find("amp") != std::string::npos || lowerName.find("gx") != std::string::npos || lowerName.find("guitarix") != std::string::npos) category = "Amplifiers";
                    else if (lowerName.find("dist") != std::string::npos || lowerName.find("fuzz") != std::string::npos || lowerName.find("drive") != std::string::npos) category = "Distortions";
                    else if (lowerName.find("eq") != std::string::npos || lowerName.find("filter") != std::string::npos) category = "EQ & Filters";
                    PluginInfo vstInfo = { name, entry.path().string(), category, "", "", false, 2, 2, 0, "", "", {}, entry.path().string(), true };
                    m_availablePlugins.push_back(vstInfo);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning VST3 directory " << dirPath << ": " << e.what() << std::endl;
        }
    }
    
    // Scan CLAP plugins
    std::vector<std::string> customClapDirs;
    for (const auto& p : m_customCLAPPaths) {
        if (!p.isEmpty()) customClapDirs.push_back(p.toStdString());
    }
    auto clapPlugins = CLAPPluginNode::scanStandardPaths(customClapDirs);
    for (const auto& clapDesc : clapPlugins) {
        std::string uri = clapDesc.pluginPath + ":" + std::to_string(clapDesc.pluginIndex);
        std::string category = "Utilities";
        if (!clapDesc.features.empty()) {
            std::string feat = clapDesc.features[0];
            if (feat.find("distortion") != std::string::npos || feat.find("fuzz") != std::string::npos || feat.find("overdrive") != std::string::npos) category = "Distortions";
            else if (feat.find("delay") != std::string::npos || feat.find("reverb") != std::string::npos) category = "Delays & Reverbs";
            else if (feat.find("filter") != std::string::npos || feat.find("equalizer") != std::string::npos) category = "EQ & Filters";
            else if (feat.find("modulation") != std::string::npos || feat.find("chorus") != std::string::npos || feat.find("flanger") != std::string::npos || feat.find("phaser") != std::string::npos) category = "Modulations";
        }
        PluginInfo clapInfo = {
            clapDesc.name,
            uri,
            category,
            clapDesc.vendor,
            "",
            false,
            2, 2, 0,
            clapDesc.version,
            clapDesc.description,
            clapDesc.features,
            clapDesc.pluginPath,
            true
        };
        m_availablePlugins.push_back(clapInfo);
    }
}

QString MainWindow::presetsDirPath() const {
    return QDir::homePath() + "/.config/RigRoom/presets";
}

void MainWindow::refreshPresetList() {
    QDir().mkpath(presetsDirPath());
    m_presetLibrary.setPaths(presetsDirPath(), QDir::homePath() + "/.config/RigRoom/library.json");
    m_presetLibrary.load();
    m_currentSlot = m_currentPresetName.isEmpty() ? -1 : m_presetLibrary.slotOfName(m_currentPresetName);
    if (m_currentSlot >= 0) m_viewBank = m_presetLibrary.bankOf(m_currentSlot);
    rebuildSlotButtons();
}


void MainWindow::loadFavoritePlugins() {
    QFile file(QDir::homePath() + "/.config/RigRoom/plugin-favorites.json");
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
    QFile file(QDir::homePath() + "/.config/RigRoom/plugin-favorites.json");
    if (file.open(QFile::WriteOnly | QFile::Truncate)) {
        file.write(QJsonDocument(favorites).toJson());
    }
}

QString MainWindow::pluginPresetDirectory(const AudioNode& node) const {
    const QByteArray identifier = QByteArray::fromStdString(node.getPluginURI().empty() ? node.getName() : node.getPluginURI());
    const QString pluginId = QString::fromLatin1(QCryptographicHash::hash(identifier, QCryptographicHash::Sha256).toHex());
    return QDir::homePath() + "/.config/RigRoom/plugin-presets/" + pluginId;
}

bool MainWindow::isValidPluginPresetName(const QString& name) const {
    return !name.trimmed().isEmpty() && !name.contains('/') && !name.contains('\\');
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
    }
    const auto& variants = node->getModelVariants();
    const bool activeModelIsVariant = std::any_of(variants.begin(), variants.end(),
        [&modelPath](const AudioNode::ModelVariant& variant) {
            return variant.localPath == modelPath.toStdString();
        });
    QJsonArray modelVariants;
    if (activeModelIsVariant) {
        for (const auto& variant : variants) {
            QJsonObject value{
                {"name", QString::fromStdString(variant.name)},
                {"url", QString::fromStdString(variant.url)}
            };
            if (!modelFileName.isEmpty() && variant.localPath == modelPath.toStdString()) {
                value["localFile"] = modelFileName;
            }
            modelVariants.append(value);
        }
    }
    preset["modelVariants"] = modelVariants;
    preset["modelSourceUrl"] = activeModelIsVariant || variants.empty()
        ? QString::fromStdString(node->getModelSourceUrl()) : QString();

    const auto& meta = node->getModelMetadata();
    preset["modelMetadata"] = activeModelIsVariant || variants.empty() ? QJsonObject{
        {"toneId", QString::fromStdString(meta.toneId)},
        {"toneTitle", QString::fromStdString(meta.toneTitle)},
        {"toneSlug", QString::fromStdString(meta.toneSlug)},
        {"author", QString::fromStdString(meta.author)},
        {"modeledBy", QString::fromStdString(meta.modeledBy)},
        {"gearType", QString::fromStdString(meta.gearType)},
        {"gearMake", QString::fromStdString(meta.gearMake)},
        {"gearModel", QString::fromStdString(meta.gearModel)},
        {"imageUrl", QString::fromStdString(meta.imageUrl)},
        {"tags", QString::fromStdString(meta.tags)},
        {"description", QString::fromStdString(meta.description)},
        {"version", QString::fromStdString(meta.version)},
        {"architecture", QString::fromStdString(meta.architecture)},
        {"loudness", meta.loudness},
        {"sampleRate", meta.sampleRate}
    } : QJsonObject{};

    QFile file(QDir(directoryPath).filePath(name.trimmed() + ".json"));
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) return false;
    file.write(QJsonDocument(preset).toJson());
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
    bool hasSavedActiveVariant = false;
    const QJsonArray savedVariants = preset.value("modelVariants").toArray();
    if (preset.contains("modelVariants")) {
        std::vector<AudioNode::ModelVariant> modelVariants;
        for (const QJsonValue& value : savedVariants) {
            const QJsonObject savedVariant = value.toObject();
            AudioNode::ModelVariant variant;
            variant.name = savedVariant.value("name").toString().toStdString();
            variant.url = savedVariant.value("url").toString().toStdString();
            const QString localFile = savedVariant.value("localFile").toString();
            if (!localFile.isEmpty() && !localFile.contains('/') && !localFile.contains('\\')) {
                const QString localPath = QDir(pluginPresetDirectory(*node)).filePath(localFile);
                if (QFileInfo(localPath).isFile()) {
                    variant.localPath = localPath.toStdString();
                    hasSavedActiveVariant = localFile == modelFile;
                }
            }
            modelVariants.push_back(std::move(variant));
        }
        // A TONE3000 variant list is meaningful only when one of its files is
        // the model copied into this preset. Older malformed presets lack this link.
        node->setModelVariants(hasSavedActiveVariant || savedVariants.isEmpty()
            ? modelVariants : std::vector<AudioNode::ModelVariant>{});
    }
    const bool hasUnlinkedVariants = !savedVariants.isEmpty() && !hasSavedActiveVariant;
    if (!hasUnlinkedVariants && preset.contains("modelMetadata") && preset.value("modelMetadata").isObject()) {
        const QJsonObject metaObj = preset.value("modelMetadata").toObject();
        AudioNode::ModelMetadata meta;
        meta.toneId = metaObj.value("toneId").toString().toStdString();
        meta.toneTitle = metaObj.value("toneTitle").toString().toStdString();
        meta.toneSlug = metaObj.value("toneSlug").toString().toStdString();
        meta.author = metaObj.value("author").toString().toStdString();
        meta.modeledBy = metaObj.value("modeledBy").toString().toStdString();
        meta.gearType = metaObj.value("gearType").toString().toStdString();
        meta.gearMake = metaObj.value("gearMake").toString().toStdString();
        meta.gearModel = metaObj.value("gearModel").toString().toStdString();
        meta.imageUrl = metaObj.value("imageUrl").toString().toStdString();
        meta.tags = metaObj.value("tags").toString().toStdString();
        meta.description = metaObj.value("description").toString().toStdString();
        meta.version = metaObj.value("version").toString().toStdString();
        meta.architecture = metaObj.value("architecture").toString().toStdString();
        meta.loudness = metaObj.value("loudness").toDouble();
        meta.sampleRate = metaObj.value("sampleRate").toDouble();
        node->setModelMetadata(meta);
    } else if (hasUnlinkedVariants) {
        node->setModelMetadata({});
        node->setModelSourceUrl({});
    }
    if (!loadedModelPath.isEmpty()) {
        if (displayName.isEmpty()) displayName = preset.value("modelName").toString();
        if (displayName.isEmpty() && node->getModelVariants().size() == 1) {
            displayName = QString::fromStdString(node->getModelVariants().front().name);
        }
        node->setModelDisplayName((displayName.isEmpty() ? QFileInfo(loadedModelPath).fileName() : displayName).toStdString());
    }
    syncParameterControls();
    setUnsavedChanges(true);
    QTimer::singleShot(0, this, [this, node]() {
        if (m_parameterControlNode == node) showPluginControls(node);
    });
    return true;
}

void MainWindow::onPlusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol) {
    Q_UNUSED(screenPos);
    std::vector<PickerPluginInfo> plugins = buildPickerInfos(m_availablePlugins);
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
    menu.addSeparator();
    const MidiAssignment* midiOnOff = m_presetMidi.find(node->uniqueId, MidiAssignment::Target::Bypass);
    QAction* midiLearnAct = menu.addAction(midiOnOff ? QString("MIDI On/Off: CC %1 - Learn Again...").arg(midiOnOff->cc)
                                                     : QString("MIDI Learn On/Off..."));
    QAction* midiRemoveAct = midiOnOff ? menu.addAction(QString("Remove MIDI On/Off (CC %1)").arg(midiOnOff->cc)) : nullptr;
    menu.addSeparator();
    QAction* removeAct = menu.addAction("Remove Effect");
    QAction* replaceAct = menu.addAction("Replace Effect");
    
    QAction* selected = menu.exec(screenPos);
    if (!selected) return;
    
    if (selected == midiLearnAct) {
        learnBlockMidi(node);
    } else if (midiRemoveAct && selected == midiRemoveAct) {
        m_presetMidi.removeFor(node->uniqueId, MidiAssignment::Target::Bypass);
        setUnsavedChanges(true);
        refreshSceneMarkers();
    } else if (selected == bypassAct) {
        node->setBypassed(!node->isBypassed());
        m_canvas->updateLayout();
        emit m_canvas->nodeBypassToggled(node);
    } else if (selected == removeAct) {
        m_canvas->removePluginAt(row, col);
        showPluginControls(nullptr);
    } else if (selected == replaceAct) {
        std::vector<PickerPluginInfo> plugins = buildPickerInfos(m_availablePlugins);
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
    
    QString currentPreset = m_currentPresetName.isEmpty() ? QString("Untitled") : m_currentPresetName;
    if (m_currentSlot >= 0) currentPreset = m_presetLibrary.slotLabel(m_currentSlot) + " " + currentPreset;
    
    QString title = "RigRoom - Guitar Multieffects host [" + currentPreset + (m_unsavedChanges ? " *" : "") + "]";
    setWindowTitle(title);

    if (m_unsavedChanges) {
        if (m_savePresetButton && (!m_saveFeedbackTimer || !m_saveFeedbackTimer->isActive())) {
            m_savePresetButton->setText("Save *");
            m_savePresetButton->setStyleSheet(
                "QPushButton { background-color: #F57C00; color: white; font-weight: bold; border-radius: 4px; padding: 5px 10px; font-size: 11px; border: none; }"
                "QPushButton:hover { background-color: #FF9800; }"
            );
        }
    } else {
        if (m_savePresetButton && (!m_saveFeedbackTimer || !m_saveFeedbackTimer->isActive())) {
            m_savePresetButton->setText("Save");
            m_savePresetButton->setStyleSheet(
                "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 5px 10px; font-size: 11px; border: none; }"
                "QPushButton:hover { background-color: #009688; }"
            );
        }
    }
    rebuildSlotButtons();
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
        QSettings windowSettings("RigRoom", "RigRoom");
        windowSettings.setValue("main_window_geometry", saveGeometry());
        windowSettings.setValue("main_workspace_vertical_splitter", QVariant::fromValue(m_workspaceSplitter->sizes()));
        saveConfigSettings();
        closeAllPluginUIs();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == m_bankLabel) {
            renameViewBank();
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick && watched == m_presetNameLabel) {
        startPresetRename();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && watched == m_presetNameLabel) {
        if (m_currentSlot >= 0) setViewBank(m_presetLibrary.bankOf(m_currentSlot));
        return true;
    }
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
            const bool integerValue = watched->property("parameterInteger").toBool();
            const double value = QInputDialog::getDouble(
                this, "Set Parameter", "Value:", currentValue, minimum, maximum,
                integerValue ? 0 : 4, &accepted);
            if (accepted) {
                m_parameterControlNode->setParameter(
                    index, integerValue ? static_cast<float>(qRound(value)) : static_cast<float>(value));
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
    const QString presetName = m_currentPresetName;
    if (presetName.isEmpty()) {
        onSavePresetAs();
        return;
    }
    
    QString fullPath = presetsDirPath() + "/" + presetName + ".json";
    savePresetToFile(fullPath);
    if (m_presetLibrary.slotOfName(presetName) < 0) {
        refreshPresetList();
    }
    setUnsavedChanges(false);
    triggerSaveFeedback();
    saveConfigSettings();
}

void MainWindow::onNewPreset() {
    if (!promptUnsavedChanges()) return;

    m_isLoadingPreset = true;
    m_parameterControlNode.reset();
    m_parameterControlBindings.clear();
    showPluginControls(nullptr);
    m_canvas->clearCanvas();
    m_canvas->setNumCols(m_globalDefaultSlots);
    updateSlotControls();
    m_currentSlot = -1;
    m_currentPresetName.clear();
    m_presetMidi.clear();
    resetScenesFromBoard();
    m_isLoadingPreset = false;
    setUnsavedChanges(true);
    saveConfigSettings();
}

void MainWindow::onSlotMinusClicked() {
    if (!m_canvas) return;
    int curr = m_canvas->getNumCols();
    int highest = m_canvas->getHighestOccupiedCol();
    int minAllowed = std::max(4, highest + 1);
    if (curr > minAllowed) {
        m_canvas->setNumCols(curr - 1);
        updateSlotControls();
        setUnsavedChanges(true);
    }
}

void MainWindow::onSlotPlusClicked() {
    if (!m_canvas) return;
    int curr = m_canvas->getNumCols();
    if (curr < 12) {
        m_canvas->setNumCols(curr + 1);
        updateSlotControls();
        setUnsavedChanges(true);
    }
}

void MainWindow::updateSlotControls() {
    if (!m_canvas || !m_slotMinusBtn || !m_slotPlusBtn || !m_slotCountLabel) return;
    int curr = m_canvas->getNumCols();
    int highest = m_canvas->getHighestOccupiedCol();
    int minAllowed = std::max(4, highest + 1);
    m_slotCountLabel->setText(QString::number(curr));
    m_slotMinusBtn->setEnabled(curr > minAllowed);
    m_slotPlusBtn->setEnabled(curr < 12);
}

namespace {
QString sanitizePresetName(QString name) {
    name = name.trimmed();
    name.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "");
    return name.trimmed();
}
}

int MainWindow::promptTargetSlot(int defaultSlot, const QString& presetName) {
    QDialog dialog(this);
    dialog.setWindowTitle("Choose Preset Slot");
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(QString("Slot for \"%1\":").arg(presetName), &dialog);
    auto* combo = new QComboBox(&dialog);
    for (int slot = 0; slot < m_presetLibrary.slotCount(); ++slot) {
        const QString name = m_presetLibrary.nameAt(slot);
        combo->addItem(m_presetLibrary.slotLabel(slot) + "   " + (name.isEmpty() ? QString::fromUtf8("— empty —") : name), slot);
    }
    combo->setCurrentIndex(std::max(0, defaultSlot));
    combo->setMaxVisibleItems(16);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(label);
    layout->addWidget(combo);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return -1;
    return combo->currentData().toInt();
}

bool MainWindow::savePresetToSlot(int targetSlot, const QString& suggestedName) {
    bool ok = false;
    const QString presetName = sanitizePresetName(QInputDialog::getText(
        this, "Save Preset As", "Enter preset name:", QLineEdit::Normal, suggestedName, &ok));
    if (!ok || presetName.isEmpty()) return false;

    const QString fileName = presetName + ".json";
    const QString fullPath = presetsDirPath() + "/" + fileName;
    const int existingSlot = m_presetLibrary.slotOf(fileName);

    if (targetSlot < 0) {
        const int defaultSlot = existingSlot >= 0 ? existingSlot : m_presetLibrary.firstFreeSlot();
        targetSlot = promptTargetSlot(defaultSlot, presetName);
        if (targetSlot < 0) return false;
    }

    if (QFile::exists(fullPath)) {
        const auto reply = QMessageBox::question(this, "Overwrite Preset?",
            "A preset named \"" + presetName + "\" already exists. Overwrite?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) return false;
    }

    const QString displaced = m_presetLibrary.presetAt(targetSlot);
    if (!displaced.isEmpty() && displaced != fileName) {
        const auto reply = QMessageBox::question(this, "Slot In Use",
            QString("Slot %1 holds \"%2\". Move it to the next free slot?")
                .arg(m_presetLibrary.slotLabel(targetSlot), m_presetLibrary.nameAt(targetSlot)),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) return false;
    }

    savePresetToFile(fullPath);
    m_presetLibrary.reconcileWithDisk();
    if (!displaced.isEmpty() && displaced != fileName) {
        // The displaced preset keeps its file; it simply moves to a free slot.
        m_presetLibrary.clear(targetSlot);
        m_presetLibrary.assign(targetSlot, fileName);
        const int free = m_presetLibrary.firstFreeSlot(targetSlot);
        if (free >= 0) m_presetLibrary.assign(free, displaced);
    } else {
        m_presetLibrary.assign(targetSlot, fileName);
    }
    m_presetLibrary.save();

    m_currentPresetName = presetName;
    m_currentSlot = m_presetLibrary.slotOf(fileName);
    setUnsavedChanges(false);
    triggerSaveFeedback();
    saveConfigSettings();
    return true;
}

void MainWindow::onSavePresetAs() {
    savePresetToSlot(-1, m_currentPresetName);
}

void MainWindow::renamePresetInSlot(int slot, const QString& requestedName) {
    const QString oldPresetName = m_presetLibrary.nameAt(slot);
    if (oldPresetName.isEmpty()) return;

    QString newPresetName;
    if (requestedName.isNull()) {
        bool ok = false;
        newPresetName = sanitizePresetName(QInputDialog::getText(
            this, "Rename Preset", "Enter new name for \"" + oldPresetName + "\":",
            QLineEdit::Normal, oldPresetName, &ok));
        if (!ok) return;
    } else {
        newPresetName = sanitizePresetName(requestedName);
    }
    if (newPresetName.isEmpty() || newPresetName == oldPresetName) return;

    const QString oldPath = presetsDirPath() + "/" + oldPresetName + ".json";
    const QString newPath = presetsDirPath() + "/" + newPresetName + ".json";
    if (QFile::exists(newPath)) {
        QMessageBox::critical(this, "Error", "A preset named \"" + newPresetName + "\" already exists.");
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        QMessageBox::critical(this, "Error", "Failed to rename the preset file.");
        return;
    }
    m_presetLibrary.renameFile(oldPresetName + ".json", newPresetName + ".json");
    m_presetLibrary.save();
    if (m_currentPresetName == oldPresetName) {
        m_currentPresetName = newPresetName;
    }
    refreshPresetList();
    setUnsavedChanges(m_unsavedChanges);
    saveConfigSettings();
}

void MainWindow::onRenamePreset() {
    if (m_currentSlot >= 0) renamePresetInSlot(m_currentSlot);
}

void MainWindow::duplicatePresetInSlot(int slot) {
    const QString name = m_presetLibrary.nameAt(slot);
    if (name.isEmpty()) return;
    QString copyName = name + " copy";
    for (int n = 2; QFile::exists(presetsDirPath() + "/" + copyName + ".json"); ++n) {
        copyName = QString("%1 copy %2").arg(name).arg(n);
    }
    const int target = m_presetLibrary.firstFreeSlot(slot);
    if (target < 0) {
        QMessageBox::warning(this, "No Free Slots", "All preset slots are in use.");
        return;
    }
    if (!QFile::copy(m_presetLibrary.pathAt(slot), presetsDirPath() + "/" + copyName + ".json")) {
        QMessageBox::critical(this, "Error", "Failed to copy the preset file.");
        return;
    }
    m_presetLibrary.assign(target, copyName + ".json");
    m_presetLibrary.save();
    if (m_statusLabel) {
        m_statusLabel->setText(QString("Duplicated '%1' to %2").arg(name, m_presetLibrary.slotLabel(target)));
    }
}

void MainWindow::deletePresetInSlot(int slot) {
    const QString presetName = m_presetLibrary.nameAt(slot);
    if (presetName.isEmpty()) return;

    const auto reply = QMessageBox::question(this, "Delete Preset",
        "Are you sure you want to delete the preset \"" + presetName + "\"?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    if (!QFile::remove(m_presetLibrary.pathAt(slot))) {
        QMessageBox::critical(this, "Error", "Failed to delete the preset file.");
        return;
    }
    m_presetLibrary.clear(slot);
    m_presetLibrary.save();

    if (slot != m_currentSlot) {
        rebuildSlotButtons();
        return;
    }

    setUnsavedChanges(false);
    m_currentSlot = -1;
    m_currentPresetName.clear();
    const int next = m_presetLibrary.nextOccupied(slot, 1);
    if (next >= 0) {
        loadSlot(next);
    } else {
        m_canvas->clearCanvas();
        m_canvas->updateLayout();
        setUnsavedChanges(false);
        saveConfigSettings();
    }
}

void MainWindow::onDeletePreset() {
    if (m_currentSlot >= 0) deletePresetInSlot(m_currentSlot);
}

bool MainWindow::loadSlot(int slot, bool remote) {
    if (!m_presetLibrary.isOccupied(slot)) return false;
    // Selecting the preset that is already loaded is not a preset change, so
    // it must not ask about unsaved edits (use Reload to discard them).
    if (slot == m_currentSlot && m_presetLibrary.nameAt(slot) == m_currentPresetName) {
        setViewBank(m_presetLibrary.bankOf(slot));
        return true;
    }
    QString discardedNote;
    if (remote) {
        // A modal prompt would stall a live switch; say what was dropped instead.
        if (m_unsavedChanges) {
            discardedNote = QString("Unsaved edits to %1 discarded. ")
                .arg(m_currentSlot >= 0 ? m_presetLibrary.slotLabel(m_currentSlot) : QString("Untitled"));
            m_unsavedChanges = false;
        }
    } else if (!promptUnsavedChanges()) {
        return false;
    }

    const QString path = m_presetLibrary.pathAt(slot);
    if (!QFile::exists(path)) {
        refreshPresetList();
        return false;
    }
    loadPresetFromFile(path);
    m_currentSlot = slot;
    m_currentPresetName = m_presetLibrary.nameAt(slot);
    m_viewBank = m_presetLibrary.bankOf(slot);
    setUnsavedChanges(false);
    saveConfigSettings();
    if (remote && m_statusLabel) {
        m_statusLabel->setText(discardedNote + QString("MIDI: loaded %1 %2")
            .arg(m_presetLibrary.slotLabel(slot), m_currentPresetName));
    }
    emit m_rig->slotChanged(slot);
    return true;
}

void MainWindow::onPresetButtonClicked() {
    if (m_presetLibrary.reconcileWithDisk()) m_presetLibrary.save();
    PresetBrowser browser(m_presetLibrary, m_currentSlot, this);
    connect(&browser, &PresetBrowser::slotActivated, this, [this](int slot) {
        // Defer so the browser closes before a possible unsaved-changes prompt.
        QTimer::singleShot(0, this, [this, slot]() { loadSlot(slot); });
    });
    connect(&browser, &PresetBrowser::slotsSwapped, this, [this](int from, int to) {
        m_presetLibrary.swap(from, to);
        m_presetLibrary.save();
        if (!m_currentPresetName.isEmpty()) m_currentSlot = m_presetLibrary.slotOfName(m_currentPresetName);
        setUnsavedChanges(m_unsavedChanges);
        saveConfigSettings();
    });
    connect(&browser, &PresetBrowser::saveCurrentToSlotRequested, this, [this](int slot) {
        QTimer::singleShot(0, this, [this, slot]() { savePresetToSlot(slot, m_currentPresetName); });
    });
    // Run dialog-based actions after the popup has closed, then reopen the grid.
    auto thenReopen = [this](std::function<void()> action) {
        QTimer::singleShot(0, this, [this, action]() {
            action();
            onPresetButtonClicked();
        });
    };
    connect(&browser, &PresetBrowser::renameRequested, this, [this, thenReopen](int slot) {
        thenReopen([this, slot]() { renamePresetInSlot(slot); });
    });
    connect(&browser, &PresetBrowser::duplicateRequested, this, [this, thenReopen](int slot) {
        thenReopen([this, slot]() { duplicatePresetInSlot(slot); rebuildSlotButtons(); });
    });
    connect(&browser, &PresetBrowser::deleteRequested, this, [this, thenReopen](int slot) {
        thenReopen([this, slot]() { deletePresetInSlot(slot); });
    });
    connect(&browser, &PresetBrowser::bankRenameRequested, this, [this, thenReopen](int bank) {
        thenReopen([this, bank]() {
            const int previous = m_viewBank;
            m_viewBank = bank;
            renameViewBank();
            m_viewBank = previous;
            rebuildSlotButtons();
        });
    });

    QWidget* anchorWidget = m_bankLabel ? static_cast<QWidget*>(m_bankLabel) : this;
    const QPoint anchor = anchorWidget->mapToGlobal(QPoint(0, anchorWidget->height() + 4));
    browser.move(anchor);
    browser.exec();
    rebuildSlotButtons();
}

void MainWindow::setupRigController() {
    m_rig = new RigController(this);
    RigController::Backend backend;
    backend.loadSlot = [this](int slot, bool remote) { return loadSlot(slot, remote); };
    backend.currentSlot = [this]() { return m_currentSlot; };
    backend.nextOccupiedSlot = [this](int from, int dir) { return m_presetLibrary.nextOccupied(from, dir); };
    backend.viewBank = [this]() { return m_viewBank; };
    backend.setViewBank = [this](int bank) { setViewBank(bank); };
    backend.bankCount = [this]() { return m_presetLibrary.numBanks(); };
    backend.slotFor = [this](int bank, int idx) {
        return (idx >= 0 && idx < m_presetLibrary.slotsPerBank()) ? m_presetLibrary.slotFor(bank, idx) : -1;
    };
    backend.selectScene = [this](int scene) { selectScene(scene); };
    backend.activeScene = [this]() { return m_scenes.activeIndex(); };
    backend.sceneCount = [this]() { return m_scenes.count(); };
    backend.toggleBlock = [this](const std::string& nodeId) {
        auto node = findNodeById(nodeId);
        if (!node) return false;
        node->setBypassed(!node->isBypassed());
        if (m_canvas) m_canvas->viewport()->update();
        onNodeBypassToggled(node);
        return true;
    };
    backend.setBlockEnabled = [this](const std::string& nodeId, bool enabled) {
        auto node = findNodeById(nodeId);
        if (!node) return false;
        if (node->isBypassed() == !enabled) return true;
        node->setBypassed(!enabled);
        if (m_canvas) m_canvas->viewport()->update();
        onNodeBypassToggled(node);
        return true;
    };
    backend.setParam = [this](const std::string& nodeId, uint32_t index, float normalized) {
        auto node = findNodeById(nodeId);
        if (!node) return false;
        for (const auto& port : node->getControlPorts()) {
            if (port.index != index || port.isOutput) continue;
            float value = port.minVal + std::clamp(normalized, 0.0f, 1.0f) * (port.maxVal - port.minVal);
            if (port.isInteger || port.isToggle) value = std::round(value);
            // A performance control (expression pedal): the knob follows via
            // syncParameterControls, but the preset is not marked edited.
            node->setParameter(index, value);
            return true;
        }
        return false;
    };
    m_rig->setBackend(std::move(backend));
}

void MainWindow::onPrevPreset() { m_rig->stepPreset(-1); }
void MainWindow::onNextPreset() { m_rig->stepPreset(1); }
void MainWindow::onPrevBank() { m_rig->stepBank(-1); }
void MainWindow::onNextBank() { m_rig->stepBank(1); }


SceneModel::BoardState MainWindow::captureBoardState() const {
    SceneModel::BoardState state;
    if (!m_canvas) return state;
    for (int r = 0; r < NodeCanvas::NUM_ROWS; ++r) {
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
            auto node = m_canvas->getPluginAt(r, c);
            if (!node || node->uniqueId.empty()) continue;
            state.bypass[node->uniqueId] = node->isBypassed();
            auto& params = state.params[node->uniqueId];
            for (const auto& port : node->getControlPorts()) {
                if (!port.isOutput) params[port.index] = port.value;
            }
        }
    }
    return state;
}

std::shared_ptr<AudioNode> MainWindow::findNodeById(const std::string& id) const {
    if (!m_canvas || id.empty()) return nullptr;
    for (int r = 0; r < NodeCanvas::NUM_ROWS; ++r) {
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
            auto node = m_canvas->getPluginAt(r, c);
            if (node && node->uniqueId == id) return node;
        }
    }
    return nullptr;
}

void MainWindow::resetScenesFromBoard() {
    m_scenes.reset(captureBoardState());
    m_engine.setSceneOutputLevel(0.0f);
    rebuildSceneBar();
}

void MainWindow::applySceneChanges(const SceneModel::Changes& changes) {
    // Only atomics and control values change here; the graph is untouched, so
    // there is no dropout and delay/reverb tails keep ringing.
    for (const auto& [nodeId, bypassed] : changes.bypass) {
        if (auto node = findNodeById(nodeId)) node->setBypassed(bypassed);
    }
    for (const auto& [nodeId, index, value] : changes.params) {
        if (auto node = findNodeById(nodeId)) node->setParameter(index, value);
    }
    m_engine.setSceneOutputLevel(changes.levelDb);
    if (m_canvas) m_canvas->viewport()->update();
    syncParameterControls();
}

void MainWindow::selectScene(int index) {
    if (index < 0 || index >= m_scenes.count()) return;
    if (index == m_scenes.activeIndex()) return;
    applySceneChanges(m_scenes.switchTo(index, captureBoardState()));
    rebuildSceneBar();
    emit m_rig->sceneChanged(index);
    // Refresh the Inspector so the scene level knob follows the new scene.
    if (m_parameterControlNode && m_parameterControlNode->getType() == NodeType::SystemOutput) {
        showPluginControls(m_parameterControlNode);
    }
    setUnsavedChanges(m_unsavedChanges);
    if (m_statusLabel) {
        m_statusLabel->setText(QString("Scene %1: %2").arg(index + 1).arg(m_scenes.active().name));
    }
}

void MainWindow::rebuildSceneBar() {
    if (!m_sceneBarLayout) return;
    while (QLayoutItem* item = m_sceneBarLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_scenes.count(); ++i) {
        const auto& scene = m_scenes.scene(i);
        auto* tile = new FootswitchTile(m_sceneBar);
        tile->setKey(QString::number(i + 1));
        tile->setName(scene.name);
        tile->setAccent(QColor(scene.color.isEmpty() ? SceneModel::defaultColor(i) : scene.color));
        tile->setState(i == m_scenes.activeIndex() ? FootswitchTile::State::Active : FootswitchTile::State::Normal);
        tile->setFixedWidth(128);
        tile->setToolTip(QString("Scene %1: %2 (Alt+%1)\nDouble-click to rename · right-click for more").arg(i + 1).arg(scene.name));
        tile->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tile, &FootswitchTile::clicked, this, [this, i]() { m_rig->selectScene(i); });
        connect(tile, &FootswitchTile::doubleClicked, this, [this, i, tile]() { startSceneRename(i, tile); });
        connect(tile, &QWidget::customContextMenuRequested, this, [this, i, tile](const QPoint& pos) {
            showSceneMenu(i, tile->mapToGlobal(pos));
        });
        m_sceneBarLayout->addWidget(tile);
    }

    if (m_scenes.count() < SceneModel::kMaxScenes) {
        auto* addBtn = new QToolButton(m_sceneBar);
        addBtn->setText("+");
        addBtn->setToolTip("Add a scene (copies the current one)");
        addBtn->setFixedSize(28, 44);
        addBtn->setCursor(Qt::PointingHandCursor);
        addBtn->setStyleSheet(
            "QToolButton { background-color: #242528; color: #E0E0E0; font-size: 12px; border: 1px solid #333438; border-radius: 4px; }"
            "QToolButton:hover { background-color: #303236; color: white; }");
        connect(addBtn, &QToolButton::clicked, this, [this]() {
            const int index = m_scenes.addScene(captureBoardState());
            if (index < 0) return;
            setUnsavedChanges(true);
            selectScene(index);
            rebuildSceneBar();
        });
        m_sceneBarLayout->addWidget(addBtn);
    }
    m_sceneBarLayout->addStretch();
    refreshSceneMarkers();
    updateCanvasInfo();
}

void MainWindow::showSceneMenu(int index, const QPoint& globalPos) {
    if (index < 0 || index >= m_scenes.count()) return;
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1E1E22; color: #E0E0E0; border: 1px solid #333333; }"
        "QMenu::item:selected { background-color: #007ACC; color: white; }"
        "QMenu::item:disabled { color: #555555; }");
    QAction* renameAct = menu.addAction("Rename...");
    // Colour submenu: named swatches from the curated palette, current one checked.
    QMenu* colorMenu = menu.addMenu("Color");
    const QString currentColor = m_scenes.scene(index).color.toUpper();
    for (const auto& c : SceneModel::palette()) {
        QPixmap swatch(14, 14);
        swatch.fill(Qt::transparent);
        {
            QPainter painter(&swatch);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QColor(255, 255, 255, 60));
            painter.setBrush(QColor(c.hex));
            painter.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
        }
        QAction* act = colorMenu->addAction(QIcon(swatch), c.name);
        act->setCheckable(true);
        act->setChecked(currentColor == QString(c.hex).toUpper());
        act->setData(QString(c.hex));
    }
    menu.addSeparator();
    QAction* dupAct = menu.addAction("Duplicate");
    dupAct->setEnabled(m_scenes.count() < SceneModel::kMaxScenes);
    QAction* overwriteAct = menu.addAction("Store current board here");
    overwriteAct->setEnabled(index != m_scenes.activeIndex());
    menu.addSeparator();
    QAction* deleteAct = menu.addAction("Delete");
    deleteAct->setEnabled(m_scenes.count() > 1);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == renameAct) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename Scene", "Scene name:",
            QLineEdit::Normal, m_scenes.scene(index).name, &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        m_scenes.renameScene(index, name);
    } else if (chosen->parent() == colorMenu) {
        m_scenes.setSceneColor(index, chosen->data().toString());
    } else if (chosen == dupAct) {
        if (m_scenes.duplicateScene(index, captureBoardState()) < 0) return;
    } else if (chosen == overwriteAct) {
        m_scenes.overwriteScene(index, captureBoardState());
    } else if (chosen == deleteAct) {
        const int oldActive = m_scenes.activeIndex();
        if (!m_scenes.removeScene(index)) return;
        if (index == oldActive) {
            // The board still shows the deleted scene; move it to the new active one.
            applySceneChanges(m_scenes.changesTo(m_scenes.activeIndex(), captureBoardState()));
        }
    }
    setUnsavedChanges(true);
    rebuildSceneBar();
}

void MainWindow::toggleParamSceneControl(const std::shared_ptr<AudioNode>& node, uint32_t index) {
    if (!node || node->uniqueId.empty()) return;
    if (m_scenes.isAssigned(node->uniqueId, index)) {
        m_scenes.unassignParam(node->uniqueId, index);
    } else {
        float value = 0.0f;
        for (const auto& port : node->getControlPorts()) {
            if (port.index == index) { value = port.value; break; }
        }
        m_scenes.assignParam(node->uniqueId, index, value);
    }
    setUnsavedChanges(true);
    refreshSceneMarkers();
    showPluginControls(node);
}

void MainWindow::setViewBank(int bank) {
    const int count = std::max(1, m_presetLibrary.numBanks());
    m_viewBank = ((bank % count) + count) % count;
    rebuildSlotButtons();
}

void MainWindow::rebuildSlotButtons() {
    if (!m_slotBarLayout || !m_bankLabel) return;
    const int perBank = m_presetLibrary.slotsPerBank();
    if (m_viewBank >= m_presetLibrary.numBanks()) m_viewBank = 0;

    // Bank label: orange when showing a bank other than the loaded preset's.
    const bool loadedHere = m_currentSlot >= 0 && m_presetLibrary.bankOf(m_currentSlot) == m_viewBank;
    const bool showingOther = m_currentSlot >= 0 && !loadedHere;
    const QString bankText = m_presetLibrary.bankName(m_viewBank).isEmpty()
        ? m_presetLibrary.bankLabel(m_viewBank) + QString::fromUtf8("  ✎")
        : m_presetLibrary.bankLabel(m_viewBank);
    m_bankLabel->setText(QFontMetrics(m_bankLabel->font()).elidedText(bankText, Qt::ElideRight, 104));
    m_bankLabel->setStyleSheet(QString(
        "QLabel { background-color: #1C1C20; color: %1; font-weight: bold; font-size: 12px;"
        " border: 1px solid %2; border-radius: 4px; padding: 3px 8px; }")
        .arg(showingOther ? "#FFB74D" : "#E0E0E0", showingOther ? "#FF9800" : "#333438"));
    QString bankTip = QString("Bank %1. Double-click to %2 it (e.g. a band, set or style); the number stays.")
        .arg(m_viewBank + 1, 2, 10, QChar('0'))
        .arg(m_presetLibrary.bankName(m_viewBank).isEmpty() ? "name" : "rename");
    if (showingOther) {
        bankTip.prepend(QString("Loaded: %1 %2\n")
            .arg(m_presetLibrary.slotLabel(m_currentSlot), m_currentPresetName));
    }
    m_bankLabel->setToolTip(bankTip);

    // Recreate the tiles only when the count changes; otherwise update them.
    if (static_cast<int>(m_slotButtons.size()) != perBank) {
        for (FootswitchTile* tile : m_slotButtons) tile->deleteLater();
        m_slotButtons.clear();
        QWidget* parent = m_bankLabel->parentWidget()->parentWidget();
        for (int i = 0; i < perBank; ++i) {
            auto* tile = new FootswitchTile(parent);
            tile->setFixedWidth(140);
            tile->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tile, &FootswitchTile::clicked, this, [this, i]() { onSlotButtonClicked(i); });
            connect(tile, &FootswitchTile::doubleClicked, this, [this, i, tile]() {
                const int slot = m_presetLibrary.slotFor(m_viewBank, i);
                if (m_presetLibrary.isOccupied(slot)) startSlotRename(slot, tile);
            });
            connect(tile, &QWidget::customContextMenuRequested, this, [this, i, tile](const QPoint& pos) {
                showSlotTileMenu(m_presetLibrary.slotFor(m_viewBank, i), tile, tile->mapToGlobal(pos));
            });
            m_slotBarLayout->addWidget(tile);
            m_slotButtons.push_back(tile);
        }
    }

    for (int i = 0; i < perBank; ++i) {
        FootswitchTile* tile = m_slotButtons[i];
        const int slot = m_presetLibrary.slotFor(m_viewBank, i);
        const QString name = m_presetLibrary.nameAt(slot);
        const bool isCurrent = slot == m_currentSlot && !name.isEmpty();
        tile->setKey(QString(QChar('A' + i)));
        tile->setName(name.isEmpty() ? QString::fromUtf8("—") : name);
        tile->setState(name.isEmpty() ? FootswitchTile::State::Empty
                       : !isCurrent ? FootswitchTile::State::Normal
                       : m_unsavedChanges ? FootswitchTile::State::Unsaved
                       : FootswitchTile::State::Active);

        QString tip = m_presetLibrary.slotLabel(slot) + "  " + (name.isEmpty() ? QString("(empty)") : name);
        if (!name.isEmpty()) {
            const QStringList scenes = m_presetLibrary.sceneNamesAt(slot);
            if (!scenes.isEmpty()) {
                QStringList numbered;
                for (int k = 0; k < scenes.size(); ++k) numbered << QString("%1 %2").arg(k + 1).arg(scenes[k]);
                tip += "\nScenes: " + numbered.join(QString::fromUtf8(" · "));
            }
            tip += isCurrent ? "\nLoaded" : "\nClick to load";
        } else {
            tip += "\nClick to save the current board here";
        }
        tile->setToolTip(tip);
    }

    updateCanvasInfo();
}

void MainWindow::onSlotButtonClicked(int indexInBank) {
    const int slot = m_presetLibrary.slotFor(m_viewBank, indexInBank);
    if (m_presetLibrary.isOccupied(slot)) {
        m_rig->selectInBank(indexInBank);
    } else {
        savePresetToSlot(slot, m_currentPresetName);
    }
}

void MainWindow::renameViewBank() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Name Bank",
        QString("Name for bank %1 (leave empty to clear):").arg(m_viewBank + 1, 2, 10, QChar('0')),
        QLineEdit::Normal, m_presetLibrary.bankName(m_viewBank), &ok);
    if (!ok) return;
    m_presetLibrary.setBankName(m_viewBank, name);
    m_presetLibrary.save();
    rebuildSlotButtons();
}

void MainWindow::refreshSceneMarkers() {
    if (!m_canvas) return;
    const auto ids = m_scenes.sceneControlledBlocks(captureBoardState());
    m_canvas->setSceneMarkedNodes(std::unordered_set<std::string>(ids.begin(), ids.end()));
    const auto midiIds = m_presetMidi.nodeIds();
    m_canvas->setMidiMarkedNodes(std::unordered_set<std::string>(midiIds.begin(), midiIds.end()));
}

void MainWindow::updateCanvasInfo() {
    if (!m_canvas) return;
    const QString name = m_currentPresetName.isEmpty() ? QString("Untitled") : m_currentPresetName;
    const QString slot = m_currentSlot >= 0 ? m_presetLibrary.slotLabel(m_currentSlot) : QString("—");
    const auto& scene = m_scenes.active();
    const QString sceneColor = scene.color.isEmpty() ? SceneModel::defaultColor(m_scenes.activeIndex()) : scene.color;
    const QString dot = m_unsavedChanges ? QString::fromUtf8("&nbsp;<span style='color:#FF9800;'>●</span>") : QString();
    m_canvas->setInfoOverlayText(QString(
        "<div style='font-size:11px; font-weight:bold; letter-spacing:1px;'>"
        "<span style='color:#00B0FF;'>%1</span>"
        "<span style='color:#55555D;'>&nbsp;&nbsp;·&nbsp;&nbsp;</span>"
        "<span style='color:%2;'>%3&nbsp;&nbsp;%4</span></div>"
        "<div style='font-size:20px; font-weight:bold; color:#F2F2F5;'>%5%6</div>")
        .arg(slot, sceneColor)
        .arg(m_scenes.activeIndex() + 1)
        .arg(scene.name.toHtmlEscaped(), name.toHtmlEscaped(), dot));
    m_presetNameLabel->setToolTip(name + (m_unsavedChanges ? "\nUnsaved changes" : "")
        + "\nClick to show its bank · double-click to rename");
}

void MainWindow::showSlotTileMenu(int slot, QWidget* tile, const QPoint& globalPos) {
    if (!m_presetLibrary.isValidSlot(slot)) return;
    QMenu menu(this);
    menu.setStyleSheet(m_presetMenu->styleSheet());
    const QString label = m_presetLibrary.slotLabel(slot);
    if (m_presetLibrary.isOccupied(slot)) {
        const bool loaded = slot == m_currentSlot;
        QAction* loadAct = menu.addAction(loaded ? QString("Reload %1").arg(label) : QString("Load %1").arg(label));
        QAction* storeAct = menu.addAction(QString("Save Current Board to %1").arg(label));
        storeAct->setEnabled(!loaded || m_unsavedChanges);
        menu.addSeparator();
        QAction* renameAct = menu.addAction("Rename...");
        QAction* dupAct = menu.addAction("Duplicate to Next Free Slot");
        QAction* deleteAct = menu.addAction("Delete...");
        menu.addSeparator();
        QAction* gridAct = menu.addAction("All Banks...");
        QAction* chosen = menu.exec(globalPos);
        if (!chosen) return;
        if (chosen == loadAct) {
            if (loaded && m_unsavedChanges) {
                if (QMessageBox::question(this, "Reload Preset", "Discard unsaved changes and reload?") != QMessageBox::Yes) return;
                m_unsavedChanges = false;
                m_currentSlot = -1; // force reload
            }
            loadSlot(slot);
        } else if (chosen == storeAct) {
            if (loaded) {
                onSavePreset();
            } else if (QMessageBox::question(this, "Replace Preset",
                           QString("Replace \"%1\" in %2 with the current board?").arg(m_presetLibrary.nameAt(slot), label))
                       == QMessageBox::Yes) {
                const QString name = m_presetLibrary.nameAt(slot);
                savePresetToFile(m_presetLibrary.pathAt(slot));
                m_currentSlot = slot;
                m_currentPresetName = name;
                setUnsavedChanges(false);
                triggerSaveFeedback();
                saveConfigSettings();
            }
        } else if (chosen == renameAct) {
            startSlotRename(slot, tile);
        } else if (chosen == dupAct) {
            duplicatePresetInSlot(slot);
            rebuildSlotButtons();
        } else if (chosen == deleteAct) {
            deletePresetInSlot(slot);
        } else if (chosen == gridAct) {
            onPresetButtonClicked();
        }
    } else {
        QAction* saveAct = menu.addAction(QString("Save Current Board to %1...").arg(label));
        if (menu.exec(globalPos) == saveAct) savePresetToSlot(slot, m_currentPresetName);
    }
}

void MainWindow::startSlotRename(int slot, QWidget* tile) {
    if (!tile || !m_presetLibrary.isOccupied(slot)) return;
    auto* editor = new QLineEdit(tile); // child of the tile: always drawn on top of it
    editor->setText(m_presetLibrary.nameAt(slot));
    editor->setGeometry(tile->rect().adjusted(4, tile->height() / 2 - 2, -4, -3));
    editor->setStyleSheet("QLineEdit { background-color: #1E1E22; color: white; border: 1px solid #00B0FF; border-radius: 4px; padding: 2px 6px; font-size: 11px; font-weight: bold; }");
    editor->selectAll();
    editor->show();
    editor->setFocus();
    auto done = std::make_shared<bool>(false);
    auto finish = [this, editor, slot, done](bool accept) {
        if (*done) return;
        *done = true;
        const QString name = editor->text();
        editor->deleteLater();
        if (accept) QTimer::singleShot(0, this, [this, slot, name]() { renamePresetInSlot(slot, name); });
    };
    connect(editor, &QLineEdit::editingFinished, this, [finish]() { finish(true); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), editor, nullptr, nullptr, Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, this, [finish]() { finish(false); });
}

void MainWindow::startPresetRename() {
    if (!m_presetNameLabel) return;
    if (m_currentSlot < 0) {
        // Nothing to rename yet: naming an unsaved board means saving it.
        onSavePresetAs();
        return;
    }
    const int slot = m_currentSlot;
    // The editor is a child of the label so nothing (e.g. the label being
    // raised on a refresh) can cover it while typing.
    QLabel* label = m_presetNameLabel;
    label->setMinimumWidth(std::max(label->width(), 280));
    label->adjustSize();
    auto* editor = new QLineEdit(label);
    editor->setText(m_currentPresetName);
    editor->setGeometry(6, label->height() - 36, label->width() - 12, 30);
    editor->setStyleSheet("QLineEdit { background-color: #1E1E22; color: white; border: 1px solid #00B0FF; border-radius: 4px; padding: 1px 6px; font-size: 16px; font-weight: bold; }");
    editor->selectAll();
    editor->show();
    editor->setFocus();
    auto done = std::make_shared<bool>(false);
    auto finish = [this, editor, slot, done](bool accept) {
        if (*done) return;
        *done = true;
        const QString name = editor->text();
        editor->deleteLater();
        if (m_presetNameLabel) m_presetNameLabel->setMinimumWidth(0);
        // Defer: renaming refreshes the panel, and a failure opens a dialog.
        QTimer::singleShot(0, this, [this, slot, name, accept]() {
            if (accept) renamePresetInSlot(slot, name);
            updateCanvasInfo();
        });
    };
    connect(editor, &QLineEdit::editingFinished, this, [finish]() { finish(true); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), editor, nullptr, nullptr, Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, this, [finish]() { finish(false); });
}

void MainWindow::startSceneRename(int index, QWidget* tile) {
    if (!tile || index < 0 || index >= m_scenes.count()) return;
    auto* editor = new QLineEdit(tile); // child of the tile: always drawn on top of it
    editor->setText(m_scenes.scene(index).name);
    // Cover the name line of the tile; the number stays visible above it.
    editor->setGeometry(tile->rect().adjusted(4, tile->height() / 2 - 2, -4, -3));
    editor->setStyleSheet("QLineEdit { background-color: #1E1E22; color: white; border: 1px solid #00B0FF; border-radius: 4px; padding: 2px 6px; font-size: 11px; font-weight: bold; }");
    editor->selectAll();
    editor->show();
    editor->setFocus();
    auto committed = std::make_shared<bool>(false);
    auto finish = [this, editor, index, committed](bool accept) {
        if (*committed) return;
        *committed = true;
        const QString name = editor->text().trimmed();
        editor->deleteLater();
        if (accept && !name.isEmpty() && index < m_scenes.count() && name != m_scenes.scene(index).name) {
            m_scenes.renameScene(index, name);
            setUnsavedChanges(true);
        }
        // Rebuild after this event finishes; the editor's button is recreated.
        QTimer::singleShot(0, this, [this]() { rebuildSceneBar(); });
    };
    connect(editor, &QLineEdit::returnPressed, this, [finish]() { finish(true); });
    connect(editor, &QLineEdit::editingFinished, this, [finish]() { finish(true); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), editor, nullptr, nullptr, Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, this, [finish]() { finish(false); });
}

void MainWindow::openSettings(int tabIndex) {
    if (m_apiKeyEdit) {
        m_apiKeyEdit->setText(CredentialStore::tone3000ApiKey());
        if (m_updateKeyStatusFunc) m_updateKeyStatusFunc();
    }
    if (m_settingsTabs && tabIndex >= 0) m_settingsTabs->setCurrentIndex(tabIndex);
    m_settingsDialog->exec();
    if (m_midi) m_midi->cancelLearn();
}

void MainWindow::setupMidi() {
    m_midi = new MidiRouter(m_engine, this);
    m_midi->setConfig(&m_midiConfig);
    m_midi->setPresetMap(&m_presetMidi);
    m_midi->setActionHandler([this](const MidiAction& action) { applyMidiAction(action); });
    // Always drain: the input may also be wired by hand (qjackctl, Helvum).
    m_midi->setActive(true);

    m_midiIndicatorTimer = new QTimer(this);
    m_midiIndicatorTimer->setSingleShot(true);
    m_midiIndicatorTimer->setInterval(160);
    const QString idleStyle = m_midiIndicator ? m_midiIndicator->styleSheet() : QString();
    connect(m_midiIndicatorTimer, &QTimer::timeout, this, [this, idleStyle]() {
        if (m_midiIndicator) m_midiIndicator->setStyleSheet(idleStyle);
    });
    connect(m_midi, &MidiRouter::activity, this, [this](const MidiMessage& msg) {
        const QString text = msg.describe();
        if (m_midiIndicator) {
            if (!m_midiIndicatorTimer->isActive()) {
                m_midiIndicator->setStyleSheet(
                    "QToolButton { background-color: #263238; color: #CE93D8; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
                    "QToolButton:hover { background-color: #37474F; }");
            }
            m_midiIndicator->setToolTip("Last MIDI: " + text + "\nClick for MIDI settings.");
        }
        m_midiIndicatorTimer->start();
        if (m_midiLastMessageSink) m_midiLastMessageSink(text);
    });
}

void MainWindow::setMidiInput(const QString& port) {
    m_midiConfig.inputPort = port.toStdString();
    m_engine.setMidiInputPort(m_midiConfig.inputPort);
    saveConfigSettings();
}

void MainWindow::applyMidiAction(const MidiAction& action) {
    auto status = [this](const QString& text) { if (m_statusLabel) m_statusLabel->setText("MIDI: " + text); };
    switch (action.kind) {
    case MidiAction::Kind::SelectSlot:
        if (!m_presetLibrary.isOccupied(action.value)) {
            status(m_presetLibrary.isValidSlot(action.value)
                ? QString("slot %1 is empty").arg(m_presetLibrary.slotLabel(action.value))
                : QString("no slot for program %1").arg(action.value));
            return;
        }
        m_rig->selectSlot(action.value, true);
        break;
    case MidiAction::Kind::StepPreset:
        m_rig->stepPreset(action.value, true);
        break;
    case MidiAction::Kind::StepBank:
        m_rig->stepBank(action.value);
        break;
    case MidiAction::Kind::SelectInBank: {
        if (action.value >= m_presetLibrary.slotsPerBank()) return;
        const int slot = m_presetLibrary.slotFor(m_viewBank, action.value);
        if (!m_presetLibrary.isOccupied(slot)) {
            status(QString("slot %1 is empty").arg(m_presetLibrary.slotLabel(slot)));
            return;
        }
        m_rig->selectInBank(action.value, true);
        break;
    }
    case MidiAction::Kind::SelectScene:
        if (action.value < m_scenes.count()) m_rig->selectScene(action.value);
        else status(QString("preset has no scene %1").arg(action.value + 1));
        break;
    case MidiAction::Kind::StepScene:
        m_rig->stepScene(action.value);
        break;
    case MidiAction::Kind::ToggleBlock:
        m_rig->toggleBlock(action.nodeId);
        break;
    case MidiAction::Kind::SetBlockEnabled:
        m_rig->setBlockEnabled(action.nodeId, action.enabled);
        break;
    case MidiAction::Kind::SetParam:
        m_rig->setParam(action.nodeId, action.paramIndex, action.normalized);
        break;
    }
}

bool MainWindow::rejectGlobalMidiConflict(int cc) {
    QString reason;
    if (m_midiConfig.bankSelect && (cc == 0 || cc == 32)) {
        reason = QString("CC %1 is Bank Select, used with Program Change.").arg(cc);
    } else if (const auto command = m_midiConfig.commandForCC(cc)) {
        reason = QString("CC %1 is used for \"%2\" in Settings > MIDI.").arg(cc).arg(midiCommandName(*command));
    }
    if (reason.isEmpty()) return false;
    QMessageBox::warning(this, "MIDI Learn",
        reason + "\nUse another control, or change the global mapping in Settings > MIDI.");
    return true;
}

void MainWindow::startMidiLearn(const QString& what, std::function<void(const MidiMessage&)> onMessage) {
    if (!m_midi) return;
    if (m_midiLearnBubble) m_midiLearnBubble->deleteLater();

    // Small non-modal hint at the top of the canvas; the router hands the next
    // CC/PC to us instead of acting on it.
    auto* bubble = new QFrame(centralWidget());
    m_midiLearnBubble = bubble;
    bubble->setObjectName("midiLearnBubble");
    bubble->setStyleSheet(
        "QFrame#midiLearnBubble { background-color: #2A1F33; border: 1px solid #CE93D8; border-radius: 8px; }"
        "QLabel { color: #F3E5F5; font-size: 12px; background: transparent; border: none; }"
        "QPushButton { background-color: #3A2A45; color: #E0E0E0; border: 1px solid #6A4A7A; border-radius: 4px; padding: 3px 10px; }"
        "QPushButton:hover { background-color: #4A3558; }");
    auto* layout = new QHBoxLayout(bubble);
    layout->setContentsMargins(12, 8, 8, 8);
    auto* label = new QLabel(QString("<b>MIDI Learn</b> &nbsp;%1: move a pedal or press a switch on your controller… (Esc to cancel)")
                             .arg(what.toHtmlEscaped()), bubble);
    layout->addWidget(label);
    auto* cancel = new QPushButton("Cancel", bubble);
    layout->addWidget(cancel);
    bubble->adjustSize();
    const QPoint anchor = m_canvas ? m_canvas->mapTo(centralWidget(), QPoint(m_canvas->width() / 2, 12)) : QPoint(width() / 2, 120);
    bubble->move(anchor.x() - bubble->width() / 2, anchor.y());
    bubble->show();
    bubble->raise();

    QPointer<QFrame> guard(bubble);
    auto close = [this, guard]() {
        if (guard) guard->deleteLater();
        if (m_midiLearnBubble == guard) m_midiLearnBubble = nullptr;
    };
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), bubble);
    escape->setContext(Qt::WindowShortcut);
    connect(escape, &QShortcut::activated, this, [this]() { m_midi->cancelLearn(); });
    connect(cancel, &QPushButton::clicked, this, [this]() { m_midi->cancelLearn(); });
    connect(m_midi, &MidiRouter::learnCancelled, bubble, [close]() { close(); });
    // Give up after a while so a forgotten learn doesn't swallow a later message.
    QTimer::singleShot(30000, bubble, [this]() { m_midi->cancelLearn(); });

    m_midi->startLearn([close, onMessage](const MidiMessage& msg) {
        close();
        onMessage(msg);
    });
}

void MainWindow::learnBlockMidi(const std::shared_ptr<AudioNode>& node) {
    if (!node) return;
    const std::string nodeId = node->uniqueId;
    const QString name = QString::fromStdString(node->getName());
    startMidiLearn(name + " on/off", [this, nodeId, name](const MidiMessage& msg) {
        if (!msg.isCC()) {
            QMessageBox::information(this, "MIDI Learn",
                QString("Received %1. Blocks are switched with a CC (a footswitch in CC mode).").arg(msg.describe()));
            return;
        }
        if (rejectGlobalMidiConflict(msg.data1)) return;
        MidiAssignment assignment;
        if (const auto* existing = m_presetMidi.find(nodeId, MidiAssignment::Target::Bypass)) assignment = *existing;
        assignment.cc = msg.data1;
        assignment.nodeId = nodeId;
        assignment.target = MidiAssignment::Target::Bypass;
        m_presetMidi.set(assignment);
        setUnsavedChanges(true);
        refreshSceneMarkers();
        if (m_statusLabel) {
            m_statusLabel->setText(QString("MIDI: CC %1 switches %2 (%3). Change the mode in Preset > MIDI Assignments.")
                .arg(msg.data1).arg(name, assignment.mode == MidiAssignment::Mode::Toggle ? "toggle on press" : "follow value"));
        }
    });
}

void MainWindow::learnParamMidi(const std::shared_ptr<AudioNode>& node, uint32_t paramIndex) {
    if (!node) return;
    QString paramName = QString::number(paramIndex);
    for (const auto& port : node->getControlPorts()) {
        if (port.index == paramIndex) { paramName = QString::fromStdString(port.name); break; }
    }
    const std::string nodeId = node->uniqueId;
    const QString what = QString::fromStdString(node->getName()) + " / " + paramName;
    startMidiLearn(what, [this, node, nodeId, paramIndex, what](const MidiMessage& msg) {
        if (!msg.isCC()) {
            QMessageBox::information(this, "MIDI Learn",
                QString("Received %1. Parameters follow a CC (expression pedal or knob).").arg(msg.describe()));
            return;
        }
        if (rejectGlobalMidiConflict(msg.data1)) return;
        MidiAssignment assignment;
        if (const auto* existing = m_presetMidi.find(nodeId, MidiAssignment::Target::Param, paramIndex)) assignment = *existing;
        assignment.cc = msg.data1;
        assignment.nodeId = nodeId;
        assignment.target = MidiAssignment::Target::Param;
        assignment.paramIndex = paramIndex;
        m_presetMidi.set(assignment);
        setUnsavedChanges(true);
        refreshSceneMarkers();
        if (m_parameterControlNode == node) showPluginControls(node);
        if (m_statusLabel) m_statusLabel->setText(QString("MIDI: CC %1 controls %2").arg(msg.data1).arg(what));
    });
}

void MainWindow::showMidiAssignmentsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("MIDI Assignments");
    dialog.setMinimumSize(720, 360);
    dialog.setStyleSheet(styleSheet());
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        "Controls learned for this preset. Add more by right-clicking a block (on/off) or a knob in the Inspector "
        "and choosing MIDI Learn. <b>Toggle on press</b> suits momentary footswitches; <b>Follow value</b> suits "
        "switches that send 127 for on and 0 for off. Range limits how far a pedal moves a parameter.", &dialog);
    intro->setWordWrap(true);
    intro->setStyleSheet("color: #AAB3C0; font-size: 11px;");
    layout->addWidget(intro);

    PresetMidiMap edited = m_presetMidi;
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"CC", "Block", "Controls", "Behaviour", ""});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table, 1);

    std::function<void()> rebuild;
    rebuild = [this, table, &edited, &rebuild]() {
        auto& list = edited.assignments();
        table->setRowCount(static_cast<int>(list.size()));
        for (int row = 0; row < static_cast<int>(list.size()); ++row) {
            MidiAssignment& a = list[row];
            auto node = findNodeById(a.nodeId);
            auto* ccSpin = new QSpinBox(table);
            ccSpin->setRange(0, 127);
            ccSpin->setValue(a.cc);
            ccSpin->setPrefix("CC ");
            QObject::connect(ccSpin, &QSpinBox::valueChanged, table, [&a](int v) { a.cc = v; });
            table->setCellWidget(row, 0, ccSpin);
            table->setItem(row, 1, new QTableWidgetItem(node ? QString::fromStdString(node->getName()) : QString("(missing block)")));

            QString target = "On / Off";
            if (a.target == MidiAssignment::Target::Param) {
                target = QString("Parameter %1").arg(a.paramIndex);
                if (node) {
                    for (const auto& port : node->getControlPorts()) {
                        if (port.index == a.paramIndex) { target = QString::fromStdString(port.name); break; }
                    }
                }
            }
            table->setItem(row, 2, new QTableWidgetItem(target));

            auto* behaviour = new QWidget(table);
            auto* bl = new QHBoxLayout(behaviour);
            bl->setContentsMargins(4, 0, 4, 0);
            if (a.target == MidiAssignment::Target::Bypass) {
                auto* mode = new QComboBox(behaviour);
                mode->addItems({"Toggle on press", "Follow value (127 on / 0 off)"});
                mode->setCurrentIndex(a.mode == MidiAssignment::Mode::Toggle ? 0 : 1);
                QObject::connect(mode, &QComboBox::currentIndexChanged, table, [&a](int i) {
                    a.mode = i == 0 ? MidiAssignment::Mode::Toggle : MidiAssignment::Mode::Follow;
                });
                bl->addWidget(mode);
            } else {
                auto makePercent = [behaviour](float v) {
                    auto* spin = new QSpinBox(behaviour);
                    spin->setRange(0, 100);
                    spin->setSuffix(" %");
                    spin->setValue(qRound(v * 100.0f));
                    return spin;
                };
                auto* minSpin = makePercent(a.min);
                auto* maxSpin = makePercent(a.max);
                auto* invert = new QCheckBox("Invert", behaviour);
                invert->setChecked(a.invert);
                QObject::connect(minSpin, &QSpinBox::valueChanged, table, [&a](int v) { a.min = v / 100.0f; });
                QObject::connect(maxSpin, &QSpinBox::valueChanged, table, [&a](int v) { a.max = v / 100.0f; });
                QObject::connect(invert, &QCheckBox::toggled, table, [&a](bool on) { a.invert = on; });
                bl->addWidget(new QLabel("From", behaviour));
                bl->addWidget(minSpin);
                bl->addWidget(new QLabel("to", behaviour));
                bl->addWidget(maxSpin);
                bl->addWidget(invert);
            }
            table->setCellWidget(row, 3, behaviour);

            auto* remove = new QPushButton("Remove", table);
            QObject::connect(remove, &QPushButton::clicked, table, [row, &edited, &rebuild]() {
                auto& items = edited.assignments();
                if (row < static_cast<int>(items.size())) items.erase(items.begin() + row);
                // Rebuild after this click handler; it deletes the button.
                QTimer::singleShot(0, [&rebuild]() { rebuild(); });
            });
            table->setCellWidget(row, 4, remove);
        }
        table->resizeRowsToContents();
    };
    rebuild();

    auto* empty = new QLabel("No MIDI assignments in this preset yet.", &dialog);
    empty->setStyleSheet("color: #777777; font-style: italic;");
    empty->setVisible(edited.assignments().empty());
    layout->addWidget(empty);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    for (const auto& a : edited.assignments()) {
        if (m_midiConfig.commandForCC(a.cc)) {
            rejectGlobalMidiConflict(a.cc);
            return;
        }
    }
    const QJsonObject before = m_presetMidi.toJson();
    m_presetMidi = edited;
    if (m_presetMidi.toJson() != before) setUnsavedChanges(true);
    refreshSceneMarkers();
    if (m_parameterControlNode) showPluginControls(m_parameterControlNode);
}

QWidget* MainWindow::buildMidiSettingsTab() {
    auto* tab = new QWidget();
    auto* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #00B0FF; border: 1px solid #333338; border-radius: 6px; margin-top: 10px; padding: 12px; background: #1a1a1f; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
        "QGroupBox QLabel, QGroupBox QCheckBox { background: transparent; }";

    // Device and channel
    auto* deviceBox = new QGroupBox("MIDI Input", tab);
    deviceBox->setStyleSheet(groupStyle);
    auto* deviceForm = new QFormLayout(deviceBox);
    auto* deviceRow = new QHBoxLayout();
    auto* deviceCombo = new QComboBox(deviceBox);
    deviceCombo->setMinimumWidth(220);
    deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* refreshBtn = new QPushButton("Refresh", deviceBox);
    deviceRow->addWidget(deviceCombo, 1);
    deviceRow->addWidget(refreshBtn);
    auto populateDevices = [this, deviceCombo]() {
        const QSignalBlocker blocker(deviceCombo);
        deviceCombo->clear();
        deviceCombo->addItem("None", QString());
        const QString current = QString::fromStdString(m_midiConfig.inputPort);
        bool found = current.isEmpty();
        for (const auto& port : m_engine.getMidiSources()) {
            const QString name = QString::fromStdString(port);
            deviceCombo->addItem(name, name);
            if (name == current) found = true;
        }
        if (!found) deviceCombo->addItem(current + "  (not connected)", current);
        deviceCombo->setCurrentIndex(std::max(0, deviceCombo->findData(current)));
    };
    populateDevices();
    connect(refreshBtn, &QPushButton::clicked, this, populateDevices);
    connect(deviceCombo, &QComboBox::currentIndexChanged, this, [this, deviceCombo](int) {
        setMidiInput(deviceCombo->currentData().toString());
    });
    deviceForm->addRow("Device:", deviceRow);

    auto* channelCombo = new QComboBox(deviceBox);
    channelCombo->addItem("Omni (all channels)", 0);
    for (int ch = 1; ch <= 16; ++ch) channelCombo->addItem(QString("Channel %1").arg(ch), ch);
    channelCombo->setCurrentIndex(std::max(0, channelCombo->findData(m_midiConfig.channel)));
    connect(channelCombo, &QComboBox::currentIndexChanged, this, [this, channelCombo](int) {
        m_midiConfig.channel = channelCombo->currentData().toInt();
        saveConfigSettings();
    });
    deviceForm->addRow("Channel:", channelCombo);

    auto* lastLabel = new QLabel("—", deviceBox);
    lastLabel->setStyleSheet("color: #CE93D8; font-weight: bold;");
    m_midiLastMessageSink = [lastLabel](const QString& text) { lastLabel->setText(text); };
    deviceForm->addRow("Last message:", lastLabel);
    tabLayout->addWidget(deviceBox);

    // Program Change
    auto* pcBox = new QGroupBox("Program Change", tab);
    pcBox->setStyleSheet(groupStyle);
    auto* pcLayout = new QVBoxLayout(pcBox);
    auto* pcRow = new QHBoxLayout();
    auto* pcLabel = new QLabel("Program Change selects:", pcBox);
    auto* pcModeCombo = new QComboBox(pcBox);
    pcModeCombo->addItem("Presets  (PC 0 = 01A, PC 1 = 01B, …; changing preset has a short gap)",
                         static_cast<int>(GlobalMidiConfig::PcMode::Presets));
    pcModeCombo->addItem("Scenes of the loaded preset  (PC 0 = scene 1 … PC 7 = scene 8; no gap)",
                         static_cast<int>(GlobalMidiConfig::PcMode::Scenes));
    pcModeCombo->addItem("Nothing  (ignore Program Change)", static_cast<int>(GlobalMidiConfig::PcMode::Off));
    pcModeCombo->setCurrentIndex(std::max(0, pcModeCombo->findData(static_cast<int>(m_midiConfig.pcMode))));
    pcModeCombo->setToolTip("Scenes mode is for footswitches that can only send Program Change: "
                            "each switch then picks a scene without reloading the preset.");
    pcRow->addWidget(pcLabel);
    pcRow->addWidget(pcModeCombo, 1);
    pcLayout->addLayout(pcRow);
    auto* bankSelect = new QCheckBox("Bank Select (CC 0) reaches preset slots beyond 128", pcBox);
    bankSelect->setChecked(m_midiConfig.bankSelect);
    auto* pcOffset = new QCheckBox("My controller counts programs from 1 (its PC 1 = the first preset or scene)", pcBox);
    pcOffset->setChecked(m_midiConfig.pcOffset == 1);
    auto updatePcOptions = [bankSelect, pcModeCombo]() {
        const auto mode = static_cast<GlobalMidiConfig::PcMode>(pcModeCombo->currentData().toInt());
        bankSelect->setEnabled(mode == GlobalMidiConfig::PcMode::Presets);
    };
    updatePcOptions();
    connect(pcModeCombo, &QComboBox::currentIndexChanged, this, [this, pcModeCombo, updatePcOptions](int) {
        m_midiConfig.pcMode = static_cast<GlobalMidiConfig::PcMode>(pcModeCombo->currentData().toInt());
        updatePcOptions();
        saveConfigSettings();
    });
    connect(bankSelect, &QCheckBox::toggled, this, [this](bool on) { m_midiConfig.bankSelect = on; saveConfigSettings(); });
    connect(pcOffset, &QCheckBox::toggled, this, [this](bool on) { m_midiConfig.pcOffset = on ? 1 : 0; saveConfigSettings(); });
    pcLayout->addWidget(bankSelect);
    pcLayout->addWidget(pcOffset);
    tabLayout->addWidget(pcBox);

    // Performance commands, in two columns: presets & banks | scenes.
    auto* cmdBox = new QGroupBox("Performance Commands (CC)", tab);
    cmdBox->setStyleSheet(groupStyle);
    auto* cmdLayout = new QVBoxLayout(cmdBox);
    auto* cmdHint = new QLabel(
        "Global switches that work in every preset. Switches fire when pressed (value 64 or more); "
        "set a command to Off to free its CC. Block on/off and pedal controls are learned per preset instead: "
        "right-click a block or knob → MIDI Learn.", cmdBox);
    cmdHint->setWordWrap(true);
    cmdHint->setStyleSheet("color: #AAB3C0; font-size: 11px; font-weight: normal;");
    cmdLayout->addWidget(cmdHint);

    auto* columns = new QHBoxLayout();
    columns->setSpacing(24);
    auto makeColumn = [cmdBox](const QString& title) {
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(4);
        auto* caption = new QLabel(title, cmdBox);
        caption->setStyleSheet("color: #7A7A86; font-weight: bold; font-size: 10px; letter-spacing: 1px;");
        grid->addWidget(caption, 0, 0, 1, 3);
        return grid;
    };
    QGridLayout* presetGrid = makeColumn("PRESETS & BANKS");
    QGridLayout* sceneGrid = makeColumn("SCENES  (no audio gap)");
    auto* presetGuide = new QLabel(
        "Bank up/down only change the shown bank, like the ◀ ▶ buttons; Preset A-D then load from it.", cmdBox);
    auto* sceneGuide = new QLabel(
        "Use whichever your controller can send:<br>"
        "• <b>Scene select</b>: one CC, value picks the scene (0 = scene 1, 1 = scene 2…)<br>"
        "• <b>Scene 1-8</b>: one CC per switch, fires on press (Off until you set them)<br>"
        "• <b>Program Change</b>: choose Scenes above<br>"
        "• <b>Previous / Next</b>: step through scenes", cmdBox);
    for (QLabel* guide : {presetGuide, sceneGuide}) {
        guide->setWordWrap(true);
        guide->setTextFormat(Qt::RichText);
        guide->setStyleSheet("color: #8A93A0; font-size: 11px; font-weight: normal;");
    }
    presetGrid->addWidget(presetGuide, 1, 0, 1, 3);
    sceneGrid->addWidget(sceneGuide, 1, 0, 1, 3);
    columns->addLayout(presetGrid, 1);
    columns->addLayout(sceneGrid, 1);
    cmdLayout->addLayout(columns);

    auto* cmdMessage = new QLabel(cmdBox);
    cmdMessage->setStyleSheet("color: #FFB74D; font-size: 11px; font-weight: normal;");

    auto spinsShared = std::make_shared<std::vector<QSpinBox*>>(kMidiCommandCount, nullptr);
    int presetRow = 2;
    int sceneRow = 2;
    const MidiCommand order[] = {
        MidiCommand::PresetPrev, MidiCommand::PresetNext, MidiCommand::BankDown, MidiCommand::BankUp,
        MidiCommand::SelectA, MidiCommand::SelectB, MidiCommand::SelectC, MidiCommand::SelectD,
        MidiCommand::SceneSelect,
        MidiCommand::Scene1, MidiCommand::Scene2, MidiCommand::Scene3, MidiCommand::Scene4,
        MidiCommand::Scene5, MidiCommand::Scene6, MidiCommand::Scene7, MidiCommand::Scene8,
        MidiCommand::ScenePrev, MidiCommand::SceneNext,
    };
    for (MidiCommand command : order) {
        const int i = static_cast<int>(command);
        const bool isScene = command == MidiCommand::SceneSelect || command == MidiCommand::ScenePrev
                             || command == MidiCommand::SceneNext
                             || (command >= MidiCommand::Scene1 && command <= MidiCommand::Scene8);
        QGridLayout* grid = isScene ? sceneGrid : presetGrid;
        const int row = isScene ? sceneRow++ : presetRow++;

        auto* name = new QLabel(midiCommandName(command), cmdBox);
        name->setStyleSheet("color: #D0D0D8; font-weight: normal;");
        auto* spin = new QSpinBox(cmdBox);
        spin->setRange(-1, 127);
        spin->setSpecialValueText("Off");
        spin->setPrefix("CC ");
        spin->setValue(m_midiConfig.commandCC[i]);
        spin->setFixedWidth(92);
        auto* learn = new QPushButton("Learn", cmdBox);
        learn->setFixedWidth(76);
        grid->addWidget(name, row, 0);
        grid->addWidget(spin, row, 1);
        grid->addWidget(learn, row, 2);
        (*spinsShared)[i] = spin;

        connect(spin, &QSpinBox::valueChanged, this, [this, i, spinsShared, cmdMessage](int value) {
            // One CC drives one command: take it away from any other command.
            if (value >= 0) {
                for (int other = 0; other < kMidiCommandCount; ++other) {
                    if (other == i || m_midiConfig.commandCC[other] != value) continue;
                    m_midiConfig.commandCC[other] = -1;
                    const QSignalBlocker blocker((*spinsShared)[other]);
                    (*spinsShared)[other]->setValue(-1);
                    cmdMessage->setText(QString("CC %1 moved from \"%2\".").arg(value)
                        .arg(midiCommandName(static_cast<MidiCommand>(other))));
                }
            }
            m_midiConfig.commandCC[i] = value;
            saveConfigSettings();
        });
        connect(learn, &QPushButton::clicked, this, [this, spin, learn]() {
            if (m_midi->isLearning()) {
                m_midi->cancelLearn();
                return;
            }
            learn->setText("Listening");
            QPointer<QPushButton> guard(learn);
            auto restore = [guard]() { if (guard) guard->setText("Learn"); };
            connect(m_midi, &MidiRouter::learnCancelled, learn, restore, Qt::SingleShotConnection);
            m_midi->startLearn([spin, restore](const MidiMessage& msg) {
                restore();
                if (msg.isCC()) spin->setValue(msg.data1);
            });
        });
    }
    sceneGrid->setRowStretch(sceneRow, 1);
    presetGrid->setRowStretch(presetRow, 1);


    auto* bottomRow = new QHBoxLayout();
    bottomRow->addWidget(cmdMessage, 1);
    auto* resetBtn = new QPushButton("Reset to Defaults", cmdBox);
    connect(resetBtn, &QPushButton::clicked, this, [this, spinsShared, cmdMessage]() {
        for (int i = 0; i < kMidiCommandCount; ++i) {
            m_midiConfig.commandCC[i] = GlobalMidiConfig::defaultCC(static_cast<MidiCommand>(i));
            const QSignalBlocker blocker((*spinsShared)[i]);
            (*spinsShared)[i]->setValue(m_midiConfig.commandCC[i]);
        }
        cmdMessage->setText("Default CCs restored.");
        saveConfigSettings();
    });
    bottomRow->addWidget(resetBtn);
    cmdLayout->addLayout(bottomRow);
    tabLayout->addWidget(cmdBox);
    tabLayout->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(tab);
    return scroll;
}

void MainWindow::triggerSaveFeedback() {
    if (!m_savePresetButton) return;

    if (!m_saveFeedbackTimer) {
        m_saveFeedbackTimer = new QTimer(this);
        m_saveFeedbackTimer->setSingleShot(true);
        connect(m_saveFeedbackTimer, &QTimer::timeout, this, [this]() {
            setUnsavedChanges(m_unsavedChanges);
        });
    }

    m_savePresetButton->setText("Saved!");
    m_savePresetButton->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; border-radius: 4px; padding: 5px 10px; font-size: 11px; border: none; }"
        "QPushButton:hover { background-color: #66BB6A; }"
    );
    m_saveFeedbackTimer->start(1500);

    if (m_statusLabel) {
        m_statusLabel->setText(QString("Preset '%1' saved to %2")
            .arg(m_currentPresetName, m_presetLibrary.slotLabel(m_currentSlot)));
    }
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
    m_hwInputCombo->addItem("No input", QVariant(QStringList{"", ""}));
    
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
    int selectIdx = 0;
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
    m_hwOutputCombo->addItem("No output", QVariant(QStringList{"", ""}));
    
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
    int selectIdx = 0;
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
    m_audioConfigured = true;
    m_canvas->setSystemChannelModes(m_hwInputModeCombo->currentIndex() == 1, m_hwOutputModeCombo->currentIndex() == 1);
    saveConfigSettings();
}

void MainWindow::onOutputHardwareChanged(int index) {
    if (index < 0 || index >= m_hwOutputCombo->count()) return;
    QStringList ports = m_hwOutputCombo->itemData(index).toStringList();
    if (ports.size() >= 2) {
        m_engine.setHardwareOutputPorts(ports[0].toStdString(), ports[1].toStdString(), m_hwOutputModeCombo->currentIndex() == 1);
    }
    m_audioConfigured = true;
    m_canvas->setSystemChannelModes(m_hwInputModeCombo->currentIndex() == 1, m_hwOutputModeCombo->currentIndex() == 1);
    saveConfigSettings();
}

void MainWindow::saveConfigSettings() {
    QJsonObject configObj;
    
    QFile configFileRead(QDir::homePath() + "/.config/RigRoom/config.json");
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
    configObj["audioConfigured"] = m_audioConfigured;
    configObj["defaultTrackSlots"] = m_globalDefaultSlots;

    QJsonArray lv2Arr, vst3Arr, clapArr;
    for (const auto& p : m_customLV2Paths) lv2Arr.append(p);
    for (const auto& p : m_customVST3Paths) vst3Arr.append(p);
    for (const auto& p : m_customCLAPPaths) clapArr.append(p);
    configObj["customLV2Paths"] = lv2Arr;
    configObj["customVST3Paths"] = vst3Arr;
    configObj["customCLAPPaths"] = clapArr;

    configObj["midi"] = m_midiConfig.toJson();
    configObj["lastPreset"] = m_currentPresetName;
    configObj["lastSlot"] = m_currentSlot;
    
    QFile configFileWrite(QDir::homePath() + "/.config/RigRoom/config.json");
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
    // Update CPU load with Exponential Moving Average (EMA) smoothing to eliminate visual jitter
    static int cpuTickCounter = 0;
    if (cpuTickCounter++ >= 5) {
        cpuTickCounter = 0;
        float rawCpu = m_engine.getCPULoad();
        m_smoothedCpuLoad = 0.15f * rawCpu + 0.85f * m_smoothedCpuLoad;
        if (m_smoothedCpuLoad > m_peakCpuLoad) {
            m_peakCpuLoad = m_smoothedCpuLoad;
        }

        const int displayCpu = qRound(m_smoothedCpuLoad);
        if (m_dspCpuButton) {
            const int cpuSeverity = displayCpu >= 85 ? 2 : displayCpu >= 70 ? 1 : 0;
            if (displayCpu != m_lastCpuDisplay) {
                m_dspCpuButton->setText(cpuSeverity == 2
                    ? QString("DSP %1% BOTTLENECK").arg(displayCpu)
                    : QString("DSP %1%").arg(displayCpu));
                m_lastCpuDisplay = displayCpu;
            }
            if (cpuSeverity != m_lastCpuSeverity && cpuSeverity == 2) {
                m_dspCpuButton->setStyleSheet(
                    "QToolButton { background-color: #D32F2F; color: white; font-weight: bold; border-radius: 4px; padding: 2px 8px; border: none; }"
                    "QToolButton:hover { background-color: #F44336; }"
                );
            } else if (cpuSeverity != m_lastCpuSeverity && cpuSeverity == 1) {
                m_dspCpuButton->setStyleSheet(
                    "QToolButton { background-color: #FBC02D; color: #18181B; font-weight: bold; border-radius: 4px; padding: 2px 8px; border: none; }"
                    "QToolButton:hover { background-color: #FDD835; }"
                );
            } else if (cpuSeverity != m_lastCpuSeverity) {
                m_dspCpuButton->setStyleSheet(
                    "QToolButton { background-color: #263238; color: #80CBC4; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
                    "QToolButton:hover { background-color: #37474F; }"
                );
            }
            m_lastCpuSeverity = cpuSeverity;
            QString tooltip = QString("DSP CPU Load: %1%\nPeak Load: %2%\nBuffer: %3 frames @ %4 Hz")
                .arg(displayCpu)
                .arg(qRound(m_peakCpuLoad))
                .arg(m_engine.getBufferSize())
                .arg(static_cast<int>(m_engine.getSampleRate()));
#ifdef RIGROOM_ENABLE_RT_METRICS
            tooltip += QString("\nWorst callback: %1 us")
                .arg(m_engine.getMaxProcessDurationUsec());
#endif
            m_dspCpuButton->setToolTip(tooltip + "\nClick to reset peak");
        }
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
    if (inputValue != m_lastInputMeterValue) {
        m_inputMeter->setValue(inputValue);
        m_lastInputMeterValue = inputValue;
    }
    if (m_inputPopupMeter && m_inputPopupMeter->isVisible() && m_inputPopupMeter->value() != inputValue) {
        m_inputPopupMeter->setValue(inputValue);
    }
    if (outputValue != m_lastOutputMeterValue) {
        m_outputMeter->setValue(outputValue);
        m_lastOutputMeterValue = outputValue;
    }
    if (m_outputPopupMeter && m_outputPopupMeter->isVisible() && m_outputPopupMeter->value() != outputValue) {
        m_outputPopupMeter->setValue(outputValue);
    }

    // Update XRun count
    const uint32_t xruns = m_engine.getXRunCount();
    if (m_xrunButton && xruns != m_lastXrunCount) {
        m_xrunButton->setText(QString("XRuns: %1").arg(xruns));
        if (xruns > 0) {
            m_xrunButton->setStyleSheet(
                "QToolButton { background-color: #D32F2F; color: white; font-weight: bold; border-radius: 4px; padding: 2px 8px; border: none; }"
                "QToolButton:hover { background-color: #F44336; }"
            );
        } else {
            m_xrunButton->setStyleSheet(
                "QToolButton { background-color: #263238; color: #80CBC4; font-size: 11px; border-radius: 4px; padding: 2px 8px; border: none; }"
                "QToolButton:hover { background-color: #37474F; }"
            );
        }
        m_lastXrunCount = xruns;
    }

    // Check clipping with hold decay (holds for ~1.5s after clipping stops)
    if (m_engine.hasInputClipped() || inPeak >= 1.0f) {
        m_inputClipHoldTicks = 50;
    } else if (m_inputClipHoldTicks > 0) {
        m_inputClipHoldTicks--;
    }

    if (m_engine.hasOutputClipped() || outPeak >= 1.0f) {
        m_outputClipHoldTicks = 50;
    } else if (m_outputClipHoldTicks > 0) {
        m_outputClipHoldTicks--;
    }

    if (m_inputGainLabel) {
        if (m_inputClipHoldTicks > 0) {
            m_inputGainLabel->setStyleSheet(
                "QToolButton { background-color: rgba(211, 47, 47, 0.35); border: 1px solid #FF5252; color: #FF8A80; font-weight: bold; border-radius: 4px; padding: 4px; }"
                "QToolButton:hover { background-color: rgba(211, 47, 47, 0.5); }"
            );
            m_inputGainLabel->setToolTip("Input is CLIPPING (> 0 dB)! Click to clear");
        } else {
            m_inputGainLabel->setStyleSheet(
                "QToolButton { background: transparent; border: none; color: #b8b8c0; padding: 4px; }"
                "QToolButton:hover { color: white; background: #29292f; border-radius: 4px; }"
            );
            m_inputGainLabel->setToolTip("Open input gain fader");
        }
    }

    if (m_outputGainLabel) {
        if (m_outputClipHoldTicks > 0) {
            m_outputGainLabel->setStyleSheet(
                "QToolButton { background-color: rgba(211, 47, 47, 0.35); border: 1px solid #FF5252; color: #FF8A80; font-weight: bold; border-radius: 4px; padding: 4px; }"
                "QToolButton:hover { background-color: rgba(211, 47, 47, 0.5); }"
            );
            m_outputGainLabel->setToolTip("Output is CLIPPING (> 0 dB)! Click to clear");
        } else {
            m_outputGainLabel->setStyleSheet(
                "QToolButton { background: transparent; border: none; color: #b8b8c0; padding: 4px; }"
                "QToolButton:hover { color: white; background: #29292f; border-radius: 4px; }"
            );
            m_outputGainLabel->setToolTip("Open output gain fader");
        }
    }
}

void MainWindow::onInputGainChanged(int value) {
    m_engine.setInputGain(static_cast<float>(value));
    m_inputGainLabel->setText(QString::number(value) + " dB");
    saveConfigSettings();
}

void MainWindow::onOutputGainChanged(int value) {
    m_engine.setOutputGain(static_cast<float>(value));
    m_outputGainLabel->setText(QString("Master Out  %1 dB").arg(value));
    saveConfigSettings();
}

void MainWindow::savePresetToFile(const QString& path) {
    // Remember any edits made in the active scene before writing.
    const SceneModel::BoardState live = captureBoardState();
    m_scenes.captureActive(live);
    m_scenes.prune(live);

    QJsonObject presetObj;
    presetObj["formatVersion"] = 8;
    presetObj["outputLevelDb"] = m_engine.getPresetOutputLevelDB();
    // Top-level node state mirrors the active scene, so older builds open it as-is.
    presetObj["scenes"] = m_scenes.toJson();
    {
        std::set<std::string> liveIds;
        for (const auto& [id, bypassed] : live.bypass) liveIds.insert(id);
        m_presetMidi.prune(liveIds);
        presetObj["midi"] = m_presetMidi.toJson();
    }
    
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
                nodeObj["model_image_url"] = QString::fromStdString(node->getModelMetadata().imageUrl);
                
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
        branch["row"] = row;
        branch["hasSplitSection"] = m_canvas->hasSplitSection(row);
        branch["parentRow"] = m_canvas->getSplitParentRow(row);
        branch["splitMode"] = m_canvas->getSplitMode(row) == GridRow::SplitMode::AB ? "ab" : "copy";
        branch["splitPosition"] = static_cast<double>(m_canvas->getSplitPosition(row));
        branch["mainInputEnabled"] = m_canvas->isMainInputEnabled(row);
        branch["mainMix"] = static_cast<double>(m_canvas->getMainMix(row));
        branch["name"] = m_canvas->getBranchName(row);
        branch["splitAfterNodeId"] = QString::fromStdString(m_canvas->getSplitAnchor(row));
        branch["mergeBeforeNodeId"] = QString::fromStdString(m_canvas->getMergeAnchor(row));
        branch["splitCol"] = m_canvas->getSplitCol(row);
        branch["mergeCol"] = m_canvas->getMergeCol(row);
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
    presetObj["trackSlots"] = m_canvas->getNumCols();
    
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
    showPluginControls(nullptr);
    
    QJsonObject presetObj = doc.object();
    // Older presets did not have a final output stage, so their neutral value is 0 dB.
    m_engine.setPresetOutputLevel(static_cast<float>(presetObj["outputLevelDb"].toDouble(0.0)));
    m_engine.setSceneOutputLevel(0.0f);
    
    m_canvas->beginRoutingUpdate();
    m_canvas->clearCanvas();

    int loadedSlots = presetObj.contains("trackSlots") ? presetObj["trackSlots"].toInt(m_globalDefaultSlots) : m_globalDefaultSlots;
    m_canvas->setNumCols(loadedSlots);
    updateSlotControls();
    
    QJsonArray nodesArray = presetObj["nodes"].toArray();
    std::unordered_set<std::string> loadedNodeIds;
    for (int i = 0; i < nodesArray.size(); ++i) {
        QJsonObject nObj = nodesArray[i].toObject();
        std::string id = nObj["id"].toString().toStdString();
        if (id.empty() || !loadedNodeIds.insert(id).second) {
            id = "preset-node-" + std::to_string(i) + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            loadedNodeIds.insert(id);
        }
        std::string uri = nObj["uri"].toString().toStdString();
        std::string typeStr = nObj["type"].toString().toStdString();
        int row = nObj["row"].toInt();
        int col = nObj["col"].toInt();
        bool bypassed = nObj["bypassed"].toBool();
        
        std::shared_ptr<AudioNode> node;
        
        if (uri == "builtin:bypass") {
            // BypassNode reports itself as LV2Plugin, so it is saved with that type.
            node = std::make_shared<BypassNode>();
        } else if (typeStr == "LV2Plugin") {
            if (m_lilvWorld) {
                const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
                if (plugins) {
                    LILV_FOREACH(plugins, j, plugins) {
                        const LilvPlugin* p = lilv_plugins_get(plugins, j);
                        const LilvNode* uriNode = lilv_plugin_get_uri(p);
                        std::string puri = lilv_node_as_string(uriNode);
                        if (puri == uri) {
                            node = std::make_shared<LV2PluginNode>(m_lilvWorld, p);
                            break;
                        }
                    }
                }
            }
            if (!node || node->isMissing()) {
                std::string origName = nObj.contains("name") ? nObj["name"].toString().toStdString() : uri;
                if (origName == uri) {
                    auto lastSlash = uri.rfind('/');
                    auto lastHash = uri.rfind('#');
                    size_t namePos = std::string::npos;
                    if (lastHash != std::string::npos) namePos = lastHash + 1;
                    else if (lastSlash != std::string::npos) namePos = lastSlash + 1;
                    if (namePos != std::string::npos && namePos < uri.length()) {
                        origName = uri.substr(namePos);
                    }
                }
                node = std::make_shared<MissingPluginNode>(NodeType::LV2Plugin, uri, origName);
            }
        } else if (typeStr == "CLAPPlugin") {
            std::string path = uri;
            uint32_t idx = 0;
            auto colonPos = path.rfind(':');
            if (colonPos != std::string::npos && colonPos > path.find(".clap")) {
                idx = std::stoul(path.substr(colonPos + 1));
                path = path.substr(0, colonPos);
            }
            auto clapNode = std::make_shared<CLAPPluginNode>(path, idx);
            if (clapNode->isMissing()) {
                std::string origName = nObj.contains("name") ? nObj["name"].toString().toStdString() : std::filesystem::path(path).stem().string();
                node = std::make_shared<MissingPluginNode>(NodeType::CLAPPlugin, uri, origName);
            } else {
                node = clapNode;
            }
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
                auto clapNode = std::make_shared<CLAPPluginNode>(path, idx);
                if (clapNode->isMissing()) {
                    std::string origName = nObj.contains("name") ? nObj["name"].toString().toStdString() : std::filesystem::path(path).stem().string();
                    node = std::make_shared<MissingPluginNode>(NodeType::CLAPPlugin, uri, origName);
                } else {
                    node = clapNode;
                }
            } else {
                auto vstNode = std::make_shared<VST3PluginNode>(uri);
                if (vstNode->isMissing()) {
                    std::string origName = nObj.contains("name") ? nObj["name"].toString().toStdString() : std::filesystem::path(uri).stem().string();
                    node = std::make_shared<MissingPluginNode>(NodeType::VST3Plugin, uri, origName);
                } else {
                    node = vstNode;
                }
            }
        }
        
        if (node) {
            node->uniqueId = id;
            node->setBypassed(bypassed);
            QString modelFilePath = nObj["model_file_path"].toString();
            const QString legacyCachePrefix = QDir::homePath() + "/.cache/PedalBoard/";
            const QString rigRoomCachePrefix = QDir::homePath() + "/.cache/RigRoom/";
            const auto migrateModelPath = [&legacyCachePrefix, &rigRoomCachePrefix](const QString& path) {
                if (!path.startsWith(legacyCachePrefix)) return path;

                const QString migratedPath = rigRoomCachePrefix + path.mid(legacyCachePrefix.size());
                if (!QFile::exists(migratedPath) && QFile::exists(path)) {
                    QDir().mkpath(QFileInfo(migratedPath).absolutePath());
                    QFile::copy(path, migratedPath);
                }
                return QFile::exists(migratedPath) ? migratedPath : path;
            };
            modelFilePath = migrateModelPath(modelFilePath);
            if (!modelFilePath.isEmpty()) {
                m_engine.suspendProcessing();
                node->loadModelFile(modelFilePath.toStdString());
                m_engine.resumeProcessing();
            }
            node->setModelDisplayName(nObj["model_display_name"].toString().toStdString());
            node->setModelSourceUrl(nObj["model_source_url"].toString().toStdString());
            AudioNode::ModelMetadata restoredMetadata = node->getModelMetadata();
            restoredMetadata.imageUrl = nObj["model_image_url"].toString().toStdString();
            node->setModelMetadata(restoredMetadata);
            
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
                var.localPath = migrateModelPath(vObj["local_path"].toString()).toStdString();
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
        if (branch.contains("splitCol")) {
            m_canvas->setSplitCol(row, branch["splitCol"].toInt(-1));
        }
        if (branch.contains("mergeCol")) {
            m_canvas->setMergeCol(row, branch["mergeCol"].toInt(-1));
        }
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
            QJsonObject branchByRow[NodeCanvas::NUM_ROWS];
            bool hasBranchForRow[NodeCanvas::NUM_ROWS] = {};
            for (int index = 0; index < branches.size(); ++index) {
                const QJsonObject branch = branches[index].toObject();
                const int legacyRowOrder[] = {0, 1, 3, 4};
                const int row = branch.contains("row") ? branch["row"].toInt(-1)
                    : (index < 4 ? legacyRowOrder[index] : -1);
                if (row != NodeCanvas::MAIN_ROW && row >= 0 && row < NodeCanvas::NUM_ROWS) {
                    branchByRow[row] = branch;
                    hasBranchForRow[row] = true;
                }
            }
            // Restore each parent before its child so a nested section is normalized
            // against the parent interval in the same batch.
            for (int r : {1, 0, 3, 4}) {
                if (hasBranchForRow[r]) restoreBranch(r, branchByRow[r], false);
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

    // Presets before format 7 have no scenes; they load as a single scene.
    m_scenes.fromJson(presetObj["scenes"].toObject(), captureBoardState());
    m_presetMidi = PresetMidiMap::fromJson(presetObj["midi"].toObject());
    m_engine.setSceneOutputLevel(m_scenes.active().levelDb);
    rebuildSceneBar();
    
    // Layout and selection notifications posted while nodes are restored can arrive
    // after this function returns. Keep them from being treated as user edits.
    QTimer::singleShot(0, this, [this]() {
        m_isLoadingPreset = false;
        setUnsavedChanges(false);
        // Only reframe on load when the user asked for automatic fitting.
        if (m_canvas->autoFit()) m_canvas->fitToCanvas();
    });
}

void MainWindow::onNodeBypassToggled(std::shared_ptr<AudioNode> node) {
    if (!node || m_isLoadingPreset) return;
    setUnsavedChanges(true);
    refreshSceneMarkers();
    emit m_rig->blockToggled(QString::fromStdString(node->uniqueId), node->isBypassed());
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
        m_serverName = "RigRoom-external-ui-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        // Native GTK/X11 LV2 UIs must use one host library stack.  In an
        // AppImage, inheriting its bundled Qt/X11 libraries mixes ABIs with GTK.
        QProcessEnvironment helperEnvironment = QProcessEnvironment::systemEnvironment();
        helperEnvironment.remove("LD_LIBRARY_PATH");
        helperEnvironment.remove("QT_PLUGIN_PATH");
        helperEnvironment.remove("SUIL_MODULE_DIR");
        m_process->setProcessEnvironment(helperEnvironment);
        connect(m_process, &QProcess::finished, this, [this]() {
            m_syncTimer.stop();
            deleteLater();
        });
        m_process->start(
            QCoreApplication::applicationFilePath(),
            {x11Ui ? "--x11-ui-helper" : "--gtk-ui-helper", m_serverName, QString::fromStdString(node->getPluginURI()),
             QString::fromUtf8(lilv_node_as_uri(lilv_ui_get_uri(ui))), QString::number(m_parentWindow)});
        m_valid = m_process->waitForStarted(3000);
        if (MainWindow* mw = qobject_cast<MainWindow*>(parent)) {
            mw->registerExternalUI(this);
        }
    }

    ~ExternalPluginUIWindow() override {
        if (MainWindow* mw = qobject_cast<MainWindow*>(parent())) {
            mw->unregisterExternalUI(this);
        }
        m_syncTimer.stop();
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            m_process->waitForFinished(1000);
        }
        m_server.close();
        QLocalServer::removeServer(m_serverName);
    }

    bool isValid() const { return m_valid; }
    AudioNode* getNode() const { return m_node; }

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

    AudioNode* getNode() const { return m_node; }
    
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

    AudioNode* getNode() const { return m_node; }

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

    AudioNode* getNode() const { return m_node; }

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

static void parseNamFileMetadata(const QString& filePath, AudioNode::ModelMetadata& meta) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    
    QJsonObject obj = doc.object();
    QJsonObject metaObj = obj;
    if (obj.contains("metadata") && obj["metadata"].isObject()) {
        metaObj = obj["metadata"].toObject();
    }

    auto getString = [&](const QString& key) -> QString {
        if (metaObj.contains(key) && !metaObj[key].toString().isEmpty()) return metaObj[key].toString();
        if (obj.contains(key) && !obj[key].toString().isEmpty()) return obj[key].toString();
        return QString();
    };

    if (meta.version.empty()) {
        QString v = getString("version");
        if (!v.isEmpty()) meta.version = v.toStdString();
    }
    if (meta.description.empty()) {
        QString d = getString("description");
        if (!d.isEmpty()) meta.description = d.toStdString();
    }
    if (meta.toneTitle.empty()) {
        QString n = getString("name");
        if (!n.isEmpty()) meta.toneTitle = n.toStdString();
    }
    if (meta.modeledBy.empty()) {
        QString mb = getString("modeled_by");
        if (!mb.isEmpty()) meta.modeledBy = mb.toStdString();
    }
    if (meta.author.empty()) {
        QString a = getString("author");
        if (!a.isEmpty()) meta.author = a.toStdString();
    }
    if (meta.gearMake.empty()) {
        QString gm = getString("gear_make");
        if (!gm.isEmpty()) meta.gearMake = gm.toStdString();
    }
    if (meta.gearModel.empty()) {
        QString gmod = getString("gear_model");
        if (!gmod.isEmpty()) meta.gearModel = gmod.toStdString();
    }
    if (meta.gearType.empty()) {
        QString gt = getString("gear_type");
        if (!gt.isEmpty()) meta.gearType = gt.toStdString();
    }
    if (meta.tags.empty()) {
        QJsonValue tagsVal = metaObj.contains("tags") ? metaObj["tags"] : obj["tags"];
        if (tagsVal.isArray()) {
            QStringList tagList;
            for (const auto& tagVal : tagsVal.toArray()) {
                tagList << tagVal.toString();
            }
            meta.tags = tagList.join(", ").toStdString();
        } else if (tagsVal.isString()) {
            meta.tags = tagsVal.toString().toStdString();
        }
    }
    if (meta.architecture.empty()) {
        QString arch = getString("architecture");
        if (!arch.isEmpty()) meta.architecture = arch.toStdString();
    }
    if (meta.loudness == 0.0) {
        if (metaObj.contains("loudness")) meta.loudness = metaObj["loudness"].toDouble();
        else if (obj.contains("loudness")) meta.loudness = obj["loudness"].toDouble();
    }
    if (meta.sampleRate == 0.0) {
        if (metaObj.contains("sample_rate")) meta.sampleRate = metaObj["sample_rate"].toDouble();
        else if (obj.contains("sample_rate")) meta.sampleRate = obj["sample_rate"].toDouble();
    }
}

void MainWindow::showPluginControls(std::shared_ptr<AudioNode> node) {
    // Clear parameters container
    m_parameterControlBindings.clear();
    m_parameterControlNode = node;
    clearLayoutContents(m_paramLayout);
    if (m_paramScroll) m_paramScroll->verticalScrollBar()->setValue(0);
    bool hasCustomUI = false;
    
    if (!node) {
        m_noParamLabel->show();
        return;
    }

    m_noParamLabel->hide();

    if (node->getType() == NodeType::SystemOutput) {
        auto* outputCard = new InspectorCard(m_paramContainer);
        auto* outputLayout = new QVBoxLayout(outputCard);
        outputLayout->setContentsMargins(12, 10, 12, 12);
        outputLayout->setSpacing(6);
        outputLayout->addWidget(new InspectorSectionHeader("PRESET OUTPUT", outputCard));

        auto* description = new QLabel(
            "Matches this preset's level before the global Master Out control.", outputCard);
        description->setWordWrap(true);
        description->setStyleSheet("color:#AAB3C0; font-size:11px; border:none;");
        outputLayout->addWidget(description);

        auto makeLevelCell = [this, outputCard](const QString& name, float db, const QString& tooltip,
                                                 std::function<void(float)> apply) {
            auto* cell = new QWidget(outputCard);
            cell->setStyleSheet("background:transparent; border:none;");
            auto* cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(4, 3, 4, 3);
            cellLayout->setSpacing(3);
            auto* title = new QLabel(name, cell);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet("color:#C9D0DA; font-size:10px; border:none;");
            title->setToolTip(tooltip);
            auto* knob = new InspectorKnob(cell);
            knob->setRange(-240, 120);
            knob->setDefaultValue(0);
            knob->setValue(qRound(db * 10.0f));
            knob->setAccessibleName(name);
            knob->setToolTip(tooltip + "\nDrag to adjust. Double-click to reset.");
            auto* valueLabel = new InspectorValueLabel(cell);
            valueLabel->setAlignment(Qt::AlignCenter);
            valueLabel->setStyleSheet("color:#7DD3FC; font-size:11px; font-weight:bold; border:none;");
            auto updateValue = [knob, valueLabel](int value) {
                const QString text = QString::number(value / 10.0, 'f', 1) + " dB";
                valueLabel->setText(text);
                knob->setAccessibleValueText(text);
            };
            updateValue(knob->value());
            valueLabel->setEditor("Set " + name, -24.0, 12.0, 1,
                [knob] { return knob->value() / 10.0; },
                [knob](double value) { knob->setValue(qRound(value * 10.0)); });
            cellLayout->addWidget(title);
            cellLayout->addWidget(knob, 0, Qt::AlignHCenter);
            cellLayout->addWidget(valueLabel);
            connect(knob, &QDial::valueChanged, this, [this, updateValue, apply](int value) {
                apply(value / 10.0f);
                updateValue(value);
                setUnsavedChanges(true);
            });
            return cell;
        };

        auto* levelRow = new QHBoxLayout();
        levelRow->setSpacing(18);
        levelRow->addStretch();
        levelRow->addWidget(makeLevelCell("Preset Level", m_engine.getPresetOutputLevelDB(),
            "Level of the whole preset", [this](float db) { m_engine.setPresetOutputLevel(db); }));
        const auto& scene = m_scenes.active();
        levelRow->addWidget(makeLevelCell(QString("Scene Level (%1)").arg(scene.name), scene.levelDb,
            "Trim for the active scene, added to the preset level",
            [this](float db) {
                m_scenes.setActiveLevel(db);
                m_engine.setSceneOutputLevel(db);
            }));
        levelRow->addStretch();
        outputLayout->addLayout(levelRow);
        m_paramLayout->addWidget(outputCard);
        return;
    }

    if (node->isMissing()) {
        auto* missingBox = new InspectorCard(m_paramContainer);
        missingBox->setStyleSheet("QFrame#inspectorCard { background:#291B1E; border:1px solid #804147; border-radius:8px; }");
        auto* missingLayout = new QVBoxLayout(missingBox);
        
        auto* titleLabel = new QLabel("Plugin Missing", missingBox);
        titleLabel->setStyleSheet("font-weight: bold; color: #FF5252; font-size: 14px;");
        missingLayout->addWidget(titleLabel);

        auto* nameLabel = new QLabel(QString("<b>Name:</b> %1").arg(QString::fromStdString(node->getName())), missingBox);
        nameLabel->setStyleSheet("color: #ECECF0; font-size: 12px; margin-top: 4px;");
        missingLayout->addWidget(nameLabel);

        auto* uriLabel = new QLabel(QString("<b>URI / Path:</b><br><code style='color:#FF8A80;'>%1</code>").arg(QString::fromStdString(node->getPluginURI())), missingBox);
        uriLabel->setStyleSheet("color: #CCCCCC; font-size: 11px; margin-top: 4px;");
        uriLabel->setWordWrap(true);
        missingLayout->addWidget(uriLabel);

        auto* infoLabel = new QLabel("Audio is automatically passed through without processing. Once this plugin is restored or reinstalled, click <b>Rescan Plugins Now</b> in Settings to activate it.", missingBox);
        infoLabel->setStyleSheet("color: #A0A0B0; font-size: 11px; margin-top: 8px;");
        infoLabel->setWordWrap(true);
        missingLayout->addWidget(infoLabel);

        m_paramLayout->addWidget(missingBox);
        return;
    }

    bool isPluginNode = (node->getType() == NodeType::LV2Plugin ||
                          node->getType() == NodeType::VST3Plugin ||
                          node->getType() == NodeType::CLAPPlugin);
    QHBoxLayout* pluginActions = nullptr;

    if (isPluginNode) {
        auto* pluginHeader = new InspectorCard(m_paramContainer);
        pluginHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* pluginHeaderLayout = new QHBoxLayout(pluginHeader);
        pluginHeaderLayout->setContentsMargins(12, 7, 8, 7);
        pluginHeaderLayout->setSpacing(8);

        auto* pluginName = new QLabel(QString::fromStdString(node->getName()), pluginHeader);
        pluginName->setStyleSheet("font-weight: bold; color: #F1F3F6; font-size: 13px; border: none;");
        pluginHeaderLayout->addWidget(pluginName);

        const QString format = node->getType() == NodeType::LV2Plugin ? "LV2"
            : node->getType() == NodeType::VST3Plugin ? "VST3" : "CLAP";
        auto* formatBadge = new QLabel(format, pluginHeader);
        formatBadge->setStyleSheet("background: #273746; color: #80D8FF; border: none; border-radius: 3px; font-size: 10px; font-weight: bold; padding: 2px 5px;");
        pluginHeaderLayout->addWidget(formatBadge);
        pluginHeaderLayout->addStretch();
        pluginActions = pluginHeaderLayout;
        m_paramLayout->addWidget(pluginHeader);

        auto* presetsButton = new QPushButton("Plugin Presets ▾", m_paramContainer);
        presetsButton->setToolTip("Load, save, and manage reusable settings for this plugin");
        presetsButton->setStyleSheet(
            "QPushButton { background-color: #333338; color: #C5DDE8; padding: 5px 8px; font-size: 11px; font-weight: bold; border-radius: 4px; border: none; }"
            "QPushButton:hover { background-color: #44444A; color: #FFFFFF; }"
        );
        pluginActions->addWidget(presetsButton);

        connect(presetsButton, &QPushButton::clicked, this, [this, node, presetsButton]() {
            QMenu menu(this);
            QAction* loadAction = menu.addAction("Load...");
            menu.addSeparator();
            QAction* saveAsAction = menu.addAction("Save As...");
            QAction* updateAction = menu.addAction("Update Existing...");
            QAction* renameAction = menu.addAction("Rename...");
            QAction* deleteAction = menu.addAction("Delete Preset");

            QAction* chosen = menu.exec(presetsButton->mapToGlobal(QPoint(0, presetsButton->height())));
            auto choosePreset = [this, node](const QString& title) {
                const QStringList files = QDir(pluginPresetDirectory(*node)).entryList({"*.json"}, QDir::Files, QDir::Name);
                QStringList names;
                for (const QString& file : files) names << file.left(file.size() - 5);
                bool accepted = false;
                const QString name = QInputDialog::getItem(this, title, "Preset:", names, 0, false, &accepted);
                return accepted ? name : QString{};
            };
            if (chosen == loadAction) {
                const QString name = choosePreset("Load Plugin Preset");
                if (name.isEmpty()) return;
                if (!loadPluginPreset(node, name)) {
                    QMessageBox::warning(this, "Plugin Preset", "Could not load this plugin preset.");
                    return;
                }
                QToolTip::showText(presetsButton->mapToGlobal(presetsButton->rect().center()),
                                   QString("Loaded '%1'").arg(name), presetsButton);
            } else if (chosen == saveAsAction) {
                bool accepted = false;
                const QString name = QInputDialog::getText(this, "Save Plugin Preset As", "Preset name:", QLineEdit::Normal, "", &accepted);
                if (!accepted) return;
                if (isValidPluginPresetName(name) && QFile::exists(QDir(pluginPresetDirectory(*node)).filePath(name.trimmed() + ".json"))) {
                    QMessageBox::warning(this, "Plugin Preset", "A preset with that name already exists. Use 'Update Existing...' to replace it.");
                    return;
                }
                if (!savePluginPreset(node, name)) {
                    QMessageBox::warning(this, "Plugin Preset", "Could not save this plugin preset.");
                    return;
                }
            } else if (chosen == updateAction) {
                const QString name = choosePreset("Update Plugin Preset");
                if (name.isEmpty()) return;
                if (!savePluginPreset(node, name)) {
                    QMessageBox::warning(this, "Plugin Preset", "Could not update this plugin preset.");
                }
            } else if (chosen == renameAction) {
                const QString currentPreset = choosePreset("Rename Plugin Preset");
                if (currentPreset.isEmpty()) return;
                bool accepted = false;
                const QString newName = QInputDialog::getText(this, "Rename Plugin Preset", "New preset name:", QLineEdit::Normal, currentPreset, &accepted);
                if (!accepted || newName.trimmed() == currentPreset) return;
                if (!isValidPluginPresetName(newName)) {
                    QMessageBox::warning(this, "Plugin Preset", "Preset names cannot be empty or contain path separators.");
                    return;
                }
                const QString dir = pluginPresetDirectory(*node);
                const QString oldPath = QDir(dir).filePath(currentPreset + ".json");
                const QString newPath = QDir(dir).filePath(newName.trimmed() + ".json");
                if (QFile::exists(newPath)) {
                    QMessageBox::warning(this, "Plugin Preset", "A preset with that name already exists.");
                    return;
                }
                if (!QFile::rename(oldPath, newPath)) {
                    QMessageBox::warning(this, "Plugin Preset", "Could not rename this preset.");
                }
            } else if (chosen == deleteAction) {
                const QString currentPreset = choosePreset("Delete Plugin Preset");
                if (currentPreset.isEmpty()) return;
                if (QMessageBox::question(this, "Delete Plugin Preset", QString("Delete preset '%1'?").arg(currentPreset), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                    const QString dir = pluginPresetDirectory(*node);
                    QFile::remove(QDir(dir).filePath(currentPreset + ".json"));
                }
            }
        });
    
    }
    
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
            hasCustomUI = true;
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
            
            QPushButton* uiBtn = new QPushButton("Open GUI", m_paramContainer);
            uiBtn->setStyleSheet(
                "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 5px 8px; font-size: 11px; border: none; }"
                "QPushButton:hover { background-color: #009688; }"
            );
            pluginActions->addWidget(uiBtn);
            
            connect(uiBtn, &QPushButton::clicked, this, [this, lv2Node, uiToOpen, isGtkUi, isX11Ui]() {
                if (raisePluginUIForNode(lv2Node)) return;
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
            
        }
    }
    
    auto* vst3Node = dynamic_cast<VST3PluginNode*>(node.get());
    if (vst3Node && vst3Node->hasEditor()) {
        hasCustomUI = true;
        QPushButton* uiBtn = new QPushButton("Open GUI", m_paramContainer);
        uiBtn->setStyleSheet(
            "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 5px 8px; font-size: 11px; border: none; }"
            "QPushButton:hover { background-color: #009688; }"
        );
        pluginActions->addWidget(uiBtn);
        
        connect(uiBtn, &QPushButton::clicked, this, [this, vst3Node]() {
            if (raisePluginUIForNode(vst3Node)) return;
            auto* uiWin = new VST3PluginUIWindow(vst3Node, this);
            uiWin->show();
        });
        
    }

    auto* clapNode = dynamic_cast<CLAPPluginNode*>(node.get());
    if (clapNode && clapNode->hasGUI()) {
        hasCustomUI = true;
        QPushButton* uiBtn = new QPushButton("Open GUI", m_paramContainer);
        uiBtn->setStyleSheet(
            "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 5px 8px; font-size: 11px; border: none; }"
            "QPushButton:hover { background-color: #009688; }"
        );
        pluginActions->addWidget(uiBtn);
        
        connect(uiBtn, &QPushButton::clicked, this, [this, clapNode]() {
            if (raisePluginUIForNode(clapNode)) return;
            auto* uiWin = new CLAPPluginUIWindow(clapNode, this);
            uiWin->show();
        });
        
    }
    
    const std::vector<AudioNode::FileProperty> fileProps = node->getFileProperties();
    static constexpr const char* namModelUri = "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model";
    const auto namProperty = std::find_if(fileProps.begin(), fileProps.end(), [](const auto& property) {
        return property.uri == namModelUri;
    });
    const bool hasNamModel = namProperty != fileProps.end();
    const bool hasGenericResources = std::any_of(fileProps.begin(), fileProps.end(), [](const auto& property) {
        return property.uri != namModelUri;
    }) || (!hasNamModel && !node->getModelVariants().empty());
    int controlPortCount = 0;
    for (const auto& param : node->getControlPorts()) {
        if (!param.isOutput) ++controlPortCount;
    }
    const bool hasControlPorts = controlPortCount > 0;

    QWidget* parameterSection = nullptr;
    QVBoxLayout* parameterSectionLayout = nullptr;
    if (!hasNamModel) {
        parameterSection = new InspectorCard(m_paramContainer);
        parameterSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        parameterSectionLayout = new QVBoxLayout(parameterSection);
        parameterSectionLayout->setContentsMargins(12, 10, 12, 12);
        parameterSectionLayout->setSpacing(6);
    }

    QFrame* namModule = nullptr;
    QVBoxLayout* namInfoLayout = nullptr;
    QHBoxLayout* namFooterLayout = nullptr;
    FlowLayout* namMainFlow = nullptr;
    QWidget* namKnobBank = nullptr;
    QVBoxLayout* controlHostLayout = parameterSectionLayout;
    if (hasNamModel) {
        namModule = new InspectorCard(m_paramContainer);
        namModule->setObjectName("namModelModule");
        namModule->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        namModule->setStyleSheet(
            "QFrame#namModelModule { background: #20242A; border: 1px solid #3A424B; border-radius: 8px; }"
            "QFrame#namModelModule QLabel { border: none; background: transparent; }"
        );
        auto* moduleLayout = new QVBoxLayout(namModule);
        moduleLayout->setContentsMargins(16, 10, 16, 10);
        moduleLayout->setSpacing(6);
        auto* moduleHeader = new QHBoxLayout();
        auto* moduleTitle = new QLabel("NAM MODEL", namModule);
        moduleTitle->setStyleSheet("color: #F0A35A; font-size: 10px; font-weight: bold; letter-spacing: 1.2px;");
        moduleHeader->addWidget(moduleTitle);
        auto* moduleSubtitle = new QLabel("Neural Amp Modeler", namModule);
        moduleSubtitle->setStyleSheet("color: #9AA6B4; font-size: 11px;");
        moduleHeader->addWidget(moduleSubtitle);
        moduleHeader->addStretch();
        moduleLayout->addLayout(moduleHeader);

        namMainFlow = new FlowLayout(nullptr, 16);
        moduleLayout->addLayout(namMainFlow);
        namKnobBank = new QWidget(namModule);
        namKnobBank->setObjectName("namKnobBank");
        // Three standard 96px controls, their gaps, and the bank inset fit on one row.
        namKnobBank->setMinimumWidth(312);
        namKnobBank->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        namKnobBank->setStyleSheet("QWidget#namKnobBank { background: transparent; border-left: 1px solid #3A424B; padding-left: 10px; }");
        controlHostLayout = new QVBoxLayout(namKnobBank);
        controlHostLayout->setContentsMargins(8, 0, 0, 0);
        controlHostLayout->setSpacing(2);
        m_paramLayout->addWidget(namModule);
    }

    QWidget* resourceSection = nullptr;
    QVBoxLayout* resourceSectionLayout = nullptr;
    if (hasGenericResources) {
        resourceSection = new InspectorCard(m_paramContainer);
        resourceSection->setObjectName("resourceSection");
        resourceSection->setStyleSheet(
            "QFrame#resourceSection { background:#22242A; border:1px solid #363941; border-radius:8px; }"
        );
        resourceSectionLayout = new QVBoxLayout(resourceSection);
        resourceSectionLayout->setContentsMargins(12, 10, 12, 10);
        resourceSectionLayout->setSpacing(5);
        auto* resourcesTitle = new InspectorSectionHeader("RESOURCES", resourceSection);
        resourceSectionLayout->addWidget(resourcesTitle);
    }

    if (!hasNamModel) {
        parameterSectionLayout->insertWidget(0, new InspectorSectionHeader("PARAMETERS", parameterSection));
        m_paramLayout->addWidget(parameterSection);
    }
    if (resourceSection) m_paramLayout->addWidget(resourceSection);

    if (!hasControlPorts) {
        QWidget* controlParent = hasNamModel ? namKnobBank : parameterSection;
        if (hasCustomUI) {
            QLabel* infoLabel = new QLabel("All controls are managed directly in the graphical interface.", controlParent);
            infoLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic; margin: 4px;");
            infoLabel->setWordWrap(true);
            controlHostLayout->addWidget(infoLabel);
        } else {
            QLabel* noParamLabel = new QLabel("No parameters available for this plugin.", controlParent);
            noParamLabel->setStyleSheet("color: #888888; font-size: 11px; font-style: italic; margin: 4px;");
            controlHostLayout->addWidget(noParamLabel);
        }
    }

    auto* controlFlow = new FlowLayout(nullptr, 8);
    if (hasControlPorts) controlHostLayout->addLayout(controlFlow);
    int controlCount = 0;

    for (auto& param : node->getControlPorts()) {
        if (param.isOutput) continue; // Skip meter outputs
        
        QWidget* rowWidget = new QWidget(hasNamModel ? namKnobBank : parameterSection);
        rowWidget->setFixedWidth(hasNamModel ? 96 : 112);
        rowWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        rowWidget->setStyleSheet("background: transparent; border: none;");
        QVBoxLayout* rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(4, 3, 4, 3);
        rowLayout->setSpacing(3);
        
        QLabel* label = new QLabel(QString::fromStdString(param.name), rowWidget);
        label->setToolTip(QString::fromStdString(param.name));
        label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        label->setStyleSheet("color: #C9CDD5; font-size: 10px; border: none;");
        label->setWordWrap(false);
        rowLayout->addWidget(label);
        
        uint32_t idx = param.index;

        // Right-click a parameter to make it vary per scene.
        const bool perScene = m_scenes.isAssigned(node->uniqueId, idx);
        if (perScene) {
            label->setText(QString::fromUtf8("◆ ") + label->text());
            label->setStyleSheet("color: #FFD54F; font-size: 10px; border: none;");
            label->setToolTip(label->toolTip() + "\nControlled per scene");
        }
        const MidiAssignment* paramMidi = m_presetMidi.find(node->uniqueId, MidiAssignment::Target::Param, idx);
        if (paramMidi) {
            label->setText(QString("M ") + label->text());
            label->setToolTip(label->toolTip() + QString("\nMIDI: CC %1").arg(paramMidi->cc));
        }
        rowWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        const int paramMidiCC = paramMidi ? paramMidi->cc : -1;
        connect(rowWidget, &QWidget::customContextMenuRequested, this, [this, node, idx, perScene, rowWidget, paramMidiCC](const QPoint& pos) {
            QMenu menu(this);
            menu.setStyleSheet(
                "QMenu { background-color: #1E1E22; color: #E0E0E0; border: 1px solid #333333; }"
                "QMenu::item:selected { background-color: #007ACC; color: white; }");
            QAction* act = menu.addAction(perScene ? "Remove per-scene control" : "Control per scene");
            menu.addSeparator();
            QAction* learnAct = menu.addAction(paramMidiCC >= 0 ? QString("MIDI: CC %1 - Learn Again...").arg(paramMidiCC)
                                                               : QString("MIDI Learn..."));
            QAction* removeMidiAct = paramMidiCC >= 0 ? menu.addAction(QString("Remove MIDI (CC %1)").arg(paramMidiCC)) : nullptr;
            QAction* chosen = menu.exec(rowWidget->mapToGlobal(pos));
            // Rebuilding the Inspector deletes rowWidget; defer past this handler.
            if (chosen == act) {
                QTimer::singleShot(0, this, [this, node, idx]() { toggleParamSceneControl(node, idx); });
            } else if (chosen == learnAct) {
                QTimer::singleShot(0, this, [this, node, idx]() { learnParamMidi(node, idx); });
            } else if (removeMidiAct && chosen == removeMidiAct) {
                QTimer::singleShot(0, this, [this, node, idx]() {
                    m_presetMidi.removeFor(node->uniqueId, MidiAssignment::Target::Param, idx);
                    setUnsavedChanges(true);
                    refreshSceneMarkers();
                    showPluginControls(node);
                });
            }
        });
        float min = param.minVal;
        float max = param.maxVal;
        auto makeResettable = [this, idx, defaultValue = param.defaultVal](QWidget* control) {
            control->setProperty("parameterIndex", idx);
            control->setProperty("parameterDefault", defaultValue);
            control->installEventFilter(this);
        };

        if (param.isToggle) {
            auto* toggle = new QCheckBox("Enabled", rowWidget);
            toggle->setChecked(param.value > (min + max) * 0.5f);
            makeResettable(toggle);
            rowLayout->addWidget(toggle, 0, Qt::AlignHCenter);
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
            combo->setFixedWidth(102);
            rowLayout->addWidget(combo, 0, Qt::AlignHCenter);
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
            auto* knob = new InspectorKnob(rowWidget);
            knob->setRange(qFloor(min), qCeil(max));
            knob->setValue(qRound(param.value));
            knob->setDefaultValue(qRound(param.defaultVal));
            if (hasNamModel) knob->setAccentColor(QColor("#D98A45"));
            knob->setToolTip("Drag to adjust. Hold Shift for fine control. Double-click to reset.");
            knob->setAccessibleName(QString::fromStdString(param.name));
            knob->setAccessibleValueText(QString::number(qRound(param.value)));
            makeResettable(knob);
            rowLayout->addWidget(knob, 0, Qt::AlignHCenter);

            auto* valueLabel = new InspectorValueLabel(rowWidget);
            valueLabel->setText(QString::number(qRound(param.value)));
            valueLabel->setAlignment(Qt::AlignCenter);
            valueLabel->setStyleSheet("color: #80D8FF; font-size: 11px; border: none;");
            valueLabel->setEditor(QString::fromStdString(param.name), min, max, 0,
                [knob] { return static_cast<double>(knob->value()); },
                [knob](double value) { knob->setValue(qRound(value)); });
            rowLayout->addWidget(valueLabel);

            connect(knob, &QDial::valueChanged, this, [this, node, idx, valueLabel, knob](int value) {
                node->setParameter(idx, static_cast<float>(value));
                valueLabel->setText(QString::number(value));
                knob->setAccessibleValueText(valueLabel->text());
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [knob, valueLabel](float value) {
                if (knob->isSliderDown()) return;
                const QSignalBlocker blocker(knob);
                knob->setValue(qRound(value));
                valueLabel->setText(QString::number(qRound(value)));
                knob->setAccessibleValueText(valueLabel->text());
            }});
        } else {
            auto* knob = new InspectorKnob(rowWidget);
            knob->setRange(0, 1000);
            const float normalized = max > min ? (param.value - min) / (max - min) : 0.0f;
            knob->setValue(qRound(std::clamp(normalized, 0.0f, 1.0f) * 1000));
            const float defaultNormalized = max > min ? (param.defaultVal - min) / (max - min) : 0.0f;
            knob->setDefaultValue(qRound(std::clamp(defaultNormalized, 0.0f, 1.0f) * 1000));
            if (hasNamModel) knob->setAccentColor(QColor("#D98A45"));
            knob->setToolTip("Drag to adjust. Double-click to reset.");
            knob->setAccessibleName(QString::fromStdString(param.name));
            knob->setAccessibleValueText(QString::number(param.value, 'f', 2));
            makeResettable(knob);
            rowLayout->addWidget(knob, 0, Qt::AlignHCenter);

            auto* valueLabel = new InspectorValueLabel(rowWidget);
            valueLabel->setText(QString::number(param.value, 'f', 2));
            valueLabel->setAlignment(Qt::AlignCenter);
            valueLabel->setStyleSheet("color: #80D8FF; font-size: 11px; border: none;");
            valueLabel->setEditor(QString::fromStdString(param.name), min, max, 4,
                [knob, min, max] { return min + (knob->value() / 1000.0) * (max - min); },
                [knob, min, max](double value) {
                    const float normalized = max > min ? (static_cast<float>(value) - min) / (max - min) : 0.0f;
                    knob->setValue(qRound(std::clamp(normalized, 0.0f, 1.0f) * 1000));
                });
            rowLayout->addWidget(valueLabel);
            connect(knob, &QDial::valueChanged, this, [this, node, idx, min, max, valueLabel, knob](int value) {
                const float floatVal = min + (value / 1000.0f) * (max - min);
                node->setParameter(idx, floatVal);
                valueLabel->setText(QString::number(floatVal, 'f', 2));
                knob->setAccessibleValueText(valueLabel->text());
                setUnsavedChanges(true);
            });
            m_parameterControlBindings.push_back({idx, [knob, valueLabel, min, max](float value) {
                if (knob->isSliderDown()) return;
                const float normalized = max > min ? (value - min) / (max - min) : 0.0f;
                const QSignalBlocker blocker(knob);
                knob->setValue(qRound(std::clamp(normalized, 0.0f, 1.0f) * 1000));
                valueLabel->setText(QString::number(value, 'f', 2));
                knob->setAccessibleValueText(valueLabel->text());
            }});
        }
        
        controlFlow->addWidget(rowWidget);
        ++controlCount;
    }
    if (controlCount == 0) delete controlFlow;
    
    // Add custom file picker buttons dynamically for any file-loading parameters
    if (!fileProps.empty()) {
        for (const auto& fp : fileProps) {
        if (fp.uri == namModelUri) {
            const std::string currentPath = fp.fileValue;
            AudioNode::ModelMetadata meta = node->getModelMetadata();
            if (!currentPath.empty()) {
                parseNamFileMetadata(QString::fromStdString(currentPath), meta);
                node->setModelMetadata(meta);
            }

            auto* imageLabel = new AmpPreviewLabel(namModule);
            imageLabel->setToolTip(meta.imageUrl.empty() ? "No model image supplied" : "Image supplied by TONE3000");
            if (!meta.imageUrl.empty()) m_toneImageLoader->load(imageLabel, QString::fromStdString(meta.imageUrl));
            namMainFlow->addWidget(imageLabel);

            auto* modelInfo = new QWidget(namModule);
            modelInfo->setMinimumWidth(220);
            modelInfo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            modelInfo->setStyleSheet("background: transparent;");
            namInfoLayout = new QVBoxLayout(modelInfo);
            namInfoLayout->setContentsMargins(0, 3, 0, 3);
            namInfoLayout->setSpacing(4);
            const QString fallbackName = currentPath.empty() ? "No model loaded" : QFileInfo(QString::fromStdString(currentPath)).completeBaseName();
            const QString title = QString::fromStdString(meta.toneTitle.empty()
                ? (node->getModelDisplayName().empty() ? fallbackName.toStdString() : node->getModelDisplayName())
                : meta.toneTitle);
            auto* titleLabel = new QLabel(title, modelInfo);
            titleLabel->setWordWrap(true);
            titleLabel->setStyleSheet("color: #F2F5F7; font-size: 16px; font-weight: bold;");
            namInfoLayout->addWidget(titleLabel);
            const QString author = QString::fromStdString(meta.author.empty() ? meta.modeledBy : meta.author);
            auto* authorLabel = new QLabel(author.isEmpty() ? "Local model file" : "Captured by " + author, modelInfo);
            authorLabel->setWordWrap(true);
            authorLabel->setStyleSheet("color: #A6B1BC; font-size: 11px;");
            namInfoLayout->addWidget(authorLabel);
            namFooterLayout = new QHBoxLayout();
            namFooterLayout->setContentsMargins(0, 3, 0, 0);
            namFooterLayout->setSpacing(6);
            namInfoLayout->addLayout(namFooterLayout);
            namMainFlow->addWidget(modelInfo);
            namMainFlow->addWidget(namKnobBank);

            auto* browseBtn = new QPushButton("Browse TONE3000", namModule);
            browseBtn->setToolTip("Browse and download a NAM model from TONE3000");
            browseBtn->setStyleSheet(
                "QPushButton { background: #D97B32; color: #16191D; font-weight: bold; border: none; border-radius: 4px; padding: 7px 11px; font-size: 11px; }"
                "QPushButton:hover { background: #EE9146; } QPushButton:focus { outline: 1px solid #80D8FF; }"
            );
            auto* loadBtn = new QPushButton("Load Local File", namModule);
            loadBtn->setStyleSheet(
                "QPushButton { background: #303840; color: #DCE6EC; font-weight: bold; border: 1px solid #46515C; border-radius: 4px; padding: 6px 10px; font-size: 11px; }"
                "QPushButton:hover { background: #3A454F; }"
            );
            namFooterLayout->addWidget(browseBtn);
            namFooterLayout->addWidget(loadBtn);
            if (!currentPath.empty()) {
                auto* detailsBtn = new QPushButton("Model Details", namModule);
                detailsBtn->setStyleSheet(
                    "QPushButton { background: transparent; color: #80D8FF; font-weight: bold; border: 1px solid #3D5664; border-radius: 4px; padding: 6px 10px; font-size: 11px; }"
                    "QPushButton:hover { background: #26343D; }"
                );
                namFooterLayout->addWidget(detailsBtn);
                connect(detailsBtn, &QPushButton::clicked, this, [this, node]() {
                    ModelDetailsDialog dialog(node, &m_engine, this);
                    dialog.exec();
                    QMetaObject::invokeMethod(this, [this, node]() { showPluginControls(node); }, Qt::QueuedConnection);
                });
            }
            namFooterLayout->addStretch();

            const std::string uri = fp.uri;
            connect(browseBtn, &QPushButton::clicked, this, [this, node, uri]() {
                Tone3000Dialog dialog(node.get(), &m_engine, this);
                if (dialog.exec() != QDialog::Accepted) return;
                const std::string filePath = dialog.getDownloadedModelPath();
                if (filePath.empty()) return;
                m_engine.suspendProcessing();
                node->setFileProperty(uri, filePath);
                m_engine.resumeProcessing();
                AudioNode::ModelMetadata metadata = dialog.getDownloadedMetadata();
                parseNamFileMetadata(QString::fromStdString(filePath), metadata);
                node->setModelMetadata(metadata);
                node->setModelVariants(dialog.getDownloadedVariants());
                const QString toneName = dialog.getDownloadedToneName();
                node->setModelDisplayName((toneName.isEmpty() ? QFileInfo(QString::fromStdString(filePath)).fileName() : toneName).toStdString());
                node->setModelSourceUrl(dialog.getDownloadedToneUrl().toStdString());
                setUnsavedChanges(true);
                saveConfigSettings();
                QMetaObject::invokeMethod(this, [this, node]() { showPluginControls(node); }, Qt::QueuedConnection);
            });
            connect(loadBtn, &QPushButton::clicked, this, [this, node, uri]() {
                const QString filePath = QFileDialog::getOpenFileName(this, "Select File", "",
                    "Neural Models (*.nam *.nammodel *.json *.aidax *.aidadspmodel);;All Files (*)");
                if (filePath.isEmpty()) return;
                m_engine.suspendProcessing();
                node->setFileProperty(uri, filePath.toStdString());
                m_engine.resumeProcessing();
                AudioNode::ModelMetadata metadata;
                metadata.toneTitle = QFileInfo(filePath).completeBaseName().toStdString();
                parseNamFileMetadata(filePath, metadata);
                node->setModelMetadata(metadata);
                node->setModelDisplayName(metadata.toneTitle.empty() ? QFileInfo(filePath).fileName().toStdString() : metadata.toneTitle);
                node->setModelSourceUrl({});
                node->setModelVariants({});
                setUnsavedChanges(true);
                saveConfigSettings();
                QMetaObject::invokeMethod(this, [this, node]() { showPluginControls(node); }, Qt::QueuedConnection);
            });
            continue;
        }
        QFrame* fpFrame = new QFrame(resourceSection);
        fpFrame->setStyleSheet("background: transparent; border: none;");
        QVBoxLayout* fpLayout = new QVBoxLayout(fpFrame);
        fpLayout->setContentsMargins(0, 2, 0, 2);
        fpLayout->setSpacing(5);
        auto* resourceInfoLayout = new QVBoxLayout();
        resourceInfoLayout->setContentsMargins(0, 0, 0, 0);
        resourceInfoLayout->setSpacing(2);
        fpLayout->addLayout(resourceInfoLayout);

        QLabel* titleLabel = new QLabel(QString::fromStdString(fp.label), fpFrame);
        titleLabel->setStyleSheet("font-weight: bold; color: #00B0FF; font-size: 12px; background: transparent; border: none;");
        resourceInfoLayout->addWidget(titleLabel);

        std::string currentPath = fp.fileValue;
        QLabel* fileLabel = new QLabel(fpFrame);
        fileLabel->setStyleSheet("color: #E0E0E0; font-size: 11px; font-style: italic; background: transparent; border: none;");
        fileLabel->setWordWrap(true);
        if (currentPath.empty()) {
            fileLabel->setText("No file loaded");
        } else {
            size_t slash = currentPath.find_last_of("/\\");
            std::string filename = (slash != std::string::npos) ? currentPath.substr(slash + 1) : currentPath;
            fileLabel->setText("Loaded: " + QString::fromStdString(filename));
        }
        resourceInfoLayout->addWidget(fileLabel);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setContentsMargins(4, 0, 0, 0);
        btnLayout->setSpacing(4);

        QPushButton* loadBtn = new QPushButton("Load File...", fpFrame);
        loadBtn->setFixedWidth(112);
        loadBtn->setStyleSheet(
            "QPushButton { background-color: #00897B; color: white; font-weight: bold; border-radius: 4px; padding: 6px; font-size: 11px; border: none; }"
            "QPushButton:hover { background-color: #009688; }"
        );
        btnLayout->addWidget(loadBtn);
        btnLayout->addStretch();

        std::string uri = fp.uri;

        fpLayout->addLayout(btnLayout);

        connect(loadBtn, &QPushButton::clicked, this, [this, node, uri]() {
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
                
                if (uri == "http://github.com/mikeoliphant/neural-amp-modeler-lv2#model") {
                    AudioNode::ModelMetadata meta;
                    meta.toneTitle = QFileInfo(filePath).completeBaseName().toStdString();
                    parseNamFileMetadata(filePath, meta);
                    node->setModelMetadata(meta);
                    size_t slash = filePath.toStdString().find_last_of("/\\");
                    std::string filename = (slash != std::string::npos) ? filePath.toStdString().substr(slash + 1) : filePath.toStdString();
                    node->setModelDisplayName(meta.toneTitle.empty() ? filename : meta.toneTitle);
                    node->setModelSourceUrl({});
                    node->setModelVariants({});
                }

                setUnsavedChanges(true);
                saveConfigSettings();
                QMetaObject::invokeMethod(this, [this, node]() { showPluginControls(node); }, Qt::QueuedConnection);
            }
        });

        resourceSectionLayout->addWidget(fpFrame);
    }
    }

    // Add variants dropdown combo box if variants list is populated
    if (node && !node->getModelVariants().empty()) {
        auto* variantPanel = new QWidget(resourceSection);
        variantPanel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        auto* variantLayout = new QVBoxLayout(variantPanel);
        variantLayout->setContentsMargins(8, 0, 0, 0);
        variantLayout->setSpacing(3);

        QLabel* varTitleLabel = new QLabel("PROFILE VARIANT", variantPanel);
        varTitleLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #8F98A8; letter-spacing: 1px; border: none;");
        variantLayout->addWidget(varTitleLabel);

        QComboBox* varCombo = new QComboBox(variantPanel);
        varCombo->setFixedWidth(240);

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

        variantLayout->addWidget(varCombo);
        if (hasNamModel && namInfoLayout) {
            namInfoLayout->addSpacing(5);
            namInfoLayout->addWidget(variantPanel);
        } else {
            resourceSectionLayout->addWidget(variantPanel, 0, Qt::AlignLeft);
        }

        connect(varCombo, &QComboBox::activated, this, [this, node, varCombo](int index) {
            int variantIdx = varCombo->itemData(index).toInt();
            const auto& vars = node->getModelVariants();
            if (variantIdx >= 0 && variantIdx < (int)vars.size()) {
                auto& selectedVar = vars[variantIdx];
                if (!selectedVar.localPath.empty() && QFile::exists(QString::fromStdString(selectedVar.localPath))) {
                    m_engine.suspendProcessing();
                    node->loadModelFile(selectedVar.localPath);
                    m_engine.resumeProcessing();

                    AudioNode::ModelMetadata meta = node->getModelMetadata();
                    std::string parentAuthor = meta.author.empty() ? meta.modeledBy : meta.author;
                    std::string parentGroup = node->getModelDisplayName();

                    parseNamFileMetadata(QString::fromStdString(selectedVar.localPath), meta);

                    if (meta.author.empty() && !parentAuthor.empty()) meta.author = parentAuthor;
                    if (meta.modeledBy.empty() && !parentAuthor.empty()) meta.modeledBy = parentAuthor;
                    if (!selectedVar.name.empty()) meta.toneTitle = selectedVar.name;
                    node->setModelMetadata(meta);

                    if (!parentGroup.empty()) {
                        node->setModelDisplayName(parentGroup);
                    } else if (!selectedVar.name.empty()) {
                        node->setModelDisplayName(selectedVar.name);
                    }
                    
                    setUnsavedChanges(true);
                    saveConfigSettings();
                    QMetaObject::invokeMethod(this, [this, node]() { showPluginControls(node); }, Qt::QueuedConnection);
                } else {
                    downloadVariant(node, variantIdx, varCombo, nullptr);
                }
            }
        });
    }
}

void MainWindow::registerExternalUI(ExternalPluginUIWindow* uiWin) {
    if (uiWin && !m_externalUiWindows.contains(uiWin)) {
        m_externalUiWindows.append(uiWin);
    }
}

void MainWindow::unregisterExternalUI(ExternalPluginUIWindow* uiWin) {
    m_externalUiWindows.removeAll(uiWin);
}

void MainWindow::closeAllPluginUIs() {
    m_parameterControlNode.reset();
    m_parameterControlBindings.clear();
    showPluginControls(nullptr);

    const auto dialogs = findChildren<QDialog*>();
    for (QDialog* dialog : dialogs) {
        if (dynamic_cast<PluginUIWindow*>(dialog) ||
            dynamic_cast<VST3PluginUIWindow*>(dialog) ||
            dynamic_cast<CLAPPluginUIWindow*>(dialog)) {
            dialog->setAttribute(Qt::WA_DeleteOnClose, false);
            dialog->close();
            delete dialog;
        }
    }

    while (!m_externalUiWindows.isEmpty()) {
        delete m_externalUiWindows.takeLast();
    }
}

void MainWindow::closePluginUIForNode(AudioNode* node) {
    if (!node) return;

    if (m_parameterControlNode.get() == node) {
        m_parameterControlNode.reset();
        m_parameterControlBindings.clear();
        showPluginControls(nullptr);
    }

    const auto dialogs = findChildren<QDialog*>();
    for (QDialog* dialog : dialogs) {
        AudioNode* winNode = nullptr;
        if (auto* win = dynamic_cast<PluginUIWindow*>(dialog)) winNode = win->getNode();
        else if (auto* win = dynamic_cast<VST3PluginUIWindow*>(dialog)) winNode = win->getNode();
        else if (auto* win = dynamic_cast<CLAPPluginUIWindow*>(dialog)) winNode = win->getNode();

        if (winNode == node) {
            dialog->setAttribute(Qt::WA_DeleteOnClose, false);
            dialog->close();
            delete dialog;
        }
    }

    for (int i = m_externalUiWindows.size() - 1; i >= 0; --i) {
        if (m_externalUiWindows[i]->getNode() == node) {
            delete m_externalUiWindows.takeAt(i);
        }
    }
}

bool MainWindow::raisePluginUIForNode(AudioNode* node) {
    if (!node) return false;

    const auto dialogs = findChildren<QDialog*>();
    for (QDialog* dialog : dialogs) {
        AudioNode* winNode = nullptr;
        if (auto* win = dynamic_cast<PluginUIWindow*>(dialog)) winNode = win->getNode();
        else if (auto* win = dynamic_cast<VST3PluginUIWindow*>(dialog)) winNode = win->getNode();
        else if (auto* win = dynamic_cast<CLAPPluginUIWindow*>(dialog)) winNode = win->getNode();

        if (winNode == node) {
            dialog->raise();
            dialog->activateWindow();
            return true;
        }
    }

    for (ExternalPluginUIWindow* extUi : m_externalUiWindows) {
        if (extUi && extUi->getNode() == node) {
            return true;
        }
    }
    return false;
}

void MainWindow::onPluginDoubleClicked(std::shared_ptr<AudioNode> node) {
    if (!node) return;
    showPluginControls(node);

    if (raisePluginUIForNode(node.get())) {
        return;
    }

    if (auto* lv2Node = dynamic_cast<LV2PluginNode*>(node.get())) {
        if (lv2Node->getLilvPlugin()) {
            LilvUIs* uis = (LilvUIs*)lilv_plugin_get_uis(lv2Node->getLilvPlugin());
            if (uis && lilv_uis_size(uis) > 0) {
                const LilvUI* uiToOpen = nullptr;
                bool isGtkUi = false;
                bool isX11Ui = false;

                const LilvNode* gtkUri = lilv_new_uri(m_lilvWorld, LV2_UI__GtkUI);
                const LilvNode* x11Uri = lilv_new_uri(m_lilvWorld, LV2_UI__X11UI);
                const LilvNode* gtk3Uri = lilv_new_uri(m_lilvWorld, "http://lv2plug.in/ns/extensions/ui#Gtk3UI");
                const LilvNode* qt5Uri = lilv_new_uri(m_lilvWorld, "http://lv2plug.in/ns/extensions/ui#Qt5UI");

                LILV_FOREACH(uis, i, uis) {
                    const LilvUI* ui = lilv_uis_get(uis, i);
                    if (lilv_ui_is_supported(ui, suil_ui_supported, gtkUri, nullptr) ||
                        lilv_ui_is_supported(ui, suil_ui_supported, x11Uri, nullptr) ||
                        lilv_ui_is_supported(ui, suil_ui_supported, gtk3Uri, nullptr) ||
                        lilv_ui_is_supported(ui, suil_ui_supported, qt5Uri, nullptr)) {
                        uiToOpen = ui;
                        break;
                    }
                }

                if (uiToOpen) {
                    const LilvNodes* selectedClasses = lilv_ui_get_classes(uiToOpen);
                    LILV_FOREACH(nodes, i, selectedClasses) {
                        const LilvNode* type = lilv_nodes_get(selectedClasses, i);
                        const QString typeUri = QString::fromUtf8(lilv_node_as_uri(type));
                        if (typeUri == LV2_UI__GtkUI) isGtkUi = true;
                        else if (typeUri == LV2_UI__X11UI) isX11Ui = true;
                    }

                    if (isGtkUi || isX11Ui) {
                        auto* uiWin = new ExternalPluginUIWindow(lv2Node, uiToOpen, isX11Ui, winId(), this);
                        if (!uiWin->isValid()) {
                            uiWin->deleteLater();
                        }
                    } else {
                        auto* uiWin = new PluginUIWindow(lv2Node, uiToOpen, this);
                        uiWin->show();
                    }
                    lilv_node_free((LilvNode*)gtkUri);
                    lilv_node_free((LilvNode*)x11Uri);
                    lilv_node_free((LilvNode*)gtk3Uri);
                    lilv_node_free((LilvNode*)qt5Uri);
                    return;
                }
                lilv_node_free((LilvNode*)gtkUri);
                lilv_node_free((LilvNode*)x11Uri);
                lilv_node_free((LilvNode*)gtk3Uri);
                lilv_node_free((LilvNode*)qt5Uri);
            }
        }
    }

    if (auto* vst3Node = dynamic_cast<VST3PluginNode*>(node.get())) {
        if (vst3Node->hasEditor()) {
            auto* uiWin = new VST3PluginUIWindow(vst3Node, this);
            uiWin->show();
            return;
        }
    }

    if (auto* clapNode = dynamic_cast<CLAPPluginNode*>(node.get())) {
        if (clapNode->hasGUI()) {
            auto* uiWin = new CLAPPluginUIWindow(clapNode, this);
            uiWin->show();
            return;
        }
    }
}

void MainWindow::showRoutingNodeControls(int row, bool isSplit) {
    if (row == NodeCanvas::MAIN_ROW || row < 0 || row >= NodeCanvas::NUM_ROWS) {
        showPluginControls(nullptr);
        return;
    }
    m_parameterControlBindings.clear();
    m_parameterControlNode.reset();
    clearLayoutContents(m_paramLayout);
    if (m_paramScroll) m_paramScroll->verticalScrollBar()->setValue(0);
    m_noParamLabel->hide();

    const QString pathName = m_canvas->getBranchName(row);
    QString pathLetter = pathName;
    if (pathLetter.startsWith("Path ")) {
        pathLetter = pathLetter.mid(5);
    }
    const std::vector<int> mixerRows = isSplit ? std::vector<int>{} : m_canvas->getMixerGroupRows(row);
    const bool sharedMixer = mixerRows.size() > 1;
    QString headerText = isSplit ? "Split Section " + pathLetter : "Mixer Section " + pathLetter;
    if (sharedMixer) {
        QStringList branches;
        for (int mixerRow : mixerRows) branches.append(m_canvas->getBranchName(mixerRow));
        headerText = "Shared Mixer A + " + branches.join(" + ");
    }
    auto* headerCard = new InspectorCard(m_paramContainer);
    auto* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(5);
    auto* header = new QLabel(headerText, headerCard);
    header->setStyleSheet("font-weight:bold; color:#DCE8F5; font-size:14px; border:none; background:transparent;");
    headerLayout->addWidget(header);

    const int sourceCol = m_canvas->getSplitCol(row);
    const int returnCol = m_canvas->getMergeCol(row);
    const int parentRow = m_canvas->getSplitParentRow(row);
    QString source = parentRow == NodeCanvas::MAIN_ROW ? "System Input" : m_canvas->getBranchName(parentRow) + " input";
    QString destination = parentRow == NodeCanvas::MAIN_ROW ? "System Output" : m_canvas->getBranchName(parentRow) + " return";
    if (sourceCol >= 0) {
        for (int col = sourceCol; col >= 0; --col) {
            if (auto node = m_canvas->getPluginAt(parentRow, col)) {
                source = "After " + QString::fromStdString(node->getName());
                break;
            }
        }
    }
    if (returnCol >= 0) {
        for (int col = returnCol; col < m_canvas->getNumCols(); ++col) {
            if (auto node = m_canvas->getPluginAt(parentRow, col)) {
                destination = "Before " + QString::fromStdString(node->getName());
                break;
            }
        }
    }
    auto* route = new QLabel(source + "  ->  " + destination, headerCard);
    route->setWordWrap(true);
    route->setStyleSheet("color:#AAB3C0; background:#191B20; border:1px solid #343842; border-radius:4px; padding:4px 7px;");
    headerLayout->addWidget(route);
    m_paramLayout->addWidget(headerCard);

    auto addKnob = [this](QLayout* host, const QString& title, int minimum, int maximum, int value, int defaultValue, const QColor& accent = QColor("#55B8E8")) {
        auto* cell = new QWidget(m_paramContainer); cell->setFixedWidth(104);
        cell->setStyleSheet("background:transparent; border:none;");
        auto* layout = new QVBoxLayout(cell); layout->setContentsMargins(4, 3, 4, 3); layout->setSpacing(3);
        auto* label = new QLabel(title, cell); label->setAlignment(Qt::AlignCenter); label->setStyleSheet("color:#C9D0DA; font-size:10px;");
        auto* valueLabel = new InspectorValueLabel(cell); valueLabel->setAlignment(Qt::AlignCenter); valueLabel->setStyleSheet("color:#7DD3FC; font-size:11px; font-weight:bold;");
        auto* knob = new InspectorKnob(cell); knob->setRange(minimum, maximum); knob->setValue(value); knob->setDefaultValue(defaultValue); knob->setAccentColor(accent); knob->setAccessibleName(title);
        knob->setToolTip("Drag to adjust. Double-click to reset.");
        valueLabel->setEditor("Set " + title, minimum, maximum, 0,
            [knob] { return static_cast<double>(knob->value()); },
            [knob](double exactValue) { knob->setValue(qRound(exactValue)); });
        layout->addWidget(label); layout->addWidget(knob, 0, Qt::AlignHCenter); layout->addWidget(valueLabel); host->addWidget(cell);
        return std::make_tuple(knob, label, valueLabel);
    };
    if (isSplit) {
        auto* splitCard = new InspectorCard(m_paramContainer);
        auto* splitLayout = new QVBoxLayout(splitCard); splitLayout->setContentsMargins(12, 10, 12, 10); splitLayout->setSpacing(8);
        splitLayout->addWidget(new InspectorSectionHeader("SPLIT", splitCard));

        if (m_canvas->isSharedSplitJunction(row)) {
            auto* fanout = new QLabel("Shared source fan-out: this path receives a full copy. Use the MIX controls to set its power and level.", m_paramContainer);
            fanout->setWordWrap(true);
            fanout->setStyleSheet("color: #a6adb8; margin-bottom: 6px;");
            fanout->setStyleSheet("color:#A6ADB8; background:#1B2028; border-left:2px solid #5A86B6; padding:6px 8px;");
            splitLayout->addWidget(fanout);
        } else {
            auto* typeRow = new QHBoxLayout(); typeRow->setSpacing(0);
            auto* copy = new QPushButton("Copy", splitCard); auto* ab = new QPushButton("A / B", splitCard);
            copy->setCheckable(true); ab->setCheckable(true); auto* modes = new QButtonGroup(splitCard); modes->setExclusive(true); modes->addButton(copy, static_cast<int>(GridRow::SplitMode::Copy)); modes->addButton(ab, static_cast<int>(GridRow::SplitMode::AB));
            (m_canvas->getSplitMode(row) == GridRow::SplitMode::Copy ? copy : ab)->setChecked(true);
            const QString segmentStyle = "QPushButton { background:#191B20; color:#AAB3C0; border:1px solid #3B404A; border-radius:0; padding:5px 12px; font-size:11px; } QPushButton:checked { background:#29445A; color:#DFF3FF; border-color:#5BAEDB; }";
            copy->setStyleSheet(segmentStyle); ab->setStyleSheet(segmentStyle); typeRow->addWidget(copy); typeRow->addWidget(ab); typeRow->addStretch(); splitLayout->addLayout(typeRow);

            auto* knobFlowHost = new QWidget(splitCard); knobFlowHost->setStyleSheet("background:transparent; border:none;"); auto* knobFlow = new FlowLayout(knobFlowHost, 8); splitLayout->addWidget(knobFlowHost);
            auto [splitPosition, splitTitleLabel, splitPositionValue] = addKnob(knobFlow, "Route A / B", -100, 100,
                qRound(m_canvas->getSplitPosition(row) * 100.0f), 0);
            auto updateSplitPosition = [splitPositionValue](int value) {
                splitPositionValue->setText(value == 0 ? "A = B" : QString("%1 %2").arg(std::abs(value)).arg(value < 0 ? "to A" : "to B"));
            };
            auto updateABEnableState = [splitPosition, splitTitleLabel, splitPositionValue](bool isAB) {
                splitPosition->setEnabled(isAB);
                splitTitleLabel->setEnabled(isAB);
                splitPositionValue->setEnabled(isAB);
                splitTitleLabel->setStyleSheet(isAB ? "color: #e2e8f0;" : "color: #4a5568;");
                splitPositionValue->setStyleSheet(isAB ? "color: #35c7ff; font-weight: bold;" : "color: #4a5568; font-weight: bold;");
            };
            updateSplitPosition(splitPosition->value());
            updateABEnableState(m_canvas->getSplitMode(row) == GridRow::SplitMode::AB);
            connect(modes, &QButtonGroup::idClicked, this, [this, row, updateABEnableState](int id) {
                const auto mode = static_cast<GridRow::SplitMode>(id);
                m_canvas->setSplitMode(row, mode);
                updateABEnableState(mode == GridRow::SplitMode::AB);
            });
            connect(splitPosition, &QDial::valueChanged, this, [this, row, updateSplitPosition](int value) {
                m_canvas->setSplitPosition(row, value / 100.0f);
                updateSplitPosition(value);
            });
        }
        auto* mainEnabled = new QCheckBox(m_canvas->isSharedSplitJunction(row) ? "Shared Path A enabled" : "Path A enabled", m_paramContainer);
        mainEnabled->setChecked(m_canvas->isMainInputEnabled(row));
        splitLayout->addWidget(mainEnabled);
        connect(mainEnabled, &QCheckBox::toggled, this, [this, row](bool checked) { m_canvas->setMainInputEnabled(row, checked); });
        auto* hint = new QLabel("Drag SPLIT to move this source. A shared source is a fan-out; separate source gaps remain independent binary splits.", m_paramContainer);
        hint->setWordWrap(true);
        hint->setStyleSheet("color: #8b929d; margin-top: 9px;");
        splitLayout->addWidget(hint);
        m_paramLayout->addWidget(splitCard);
    } else {
        auto* mixerHost = new QWidget(m_paramContainer);
        mixerHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mixerHost->setStyleSheet("background:transparent; border:none;");
        auto* mixerFlow = new FlowLayout(mixerHost, 10);
        auto makeStrip = [this, mixerFlow, mixerHost](const QString& title) {
            auto* card = new InspectorCard(mixerHost); card->setFixedWidth(250);
            auto* layout = new QVBoxLayout(card); layout->setContentsMargins(10, 9, 10, 10); layout->setSpacing(6);
            layout->setAlignment(Qt::AlignTop);
            auto* heading = new QLabel(title.toUpper(), card); heading->setStyleSheet("color:#D8B8FF; font-size:10px; font-weight:bold; letter-spacing:1px;"); layout->addWidget(heading);
            mixerFlow->addWidget(card); return layout;
        };
        auto* mainStrip = makeStrip(sharedMixer ? "Shared Path A" : "Path A");
        auto* mainKnobs = new QWidget(m_paramContainer); mainKnobs->setStyleSheet("background:transparent; border:none;"); auto* mainKnobFlow = new FlowLayout(mainKnobs, 6); mainStrip->addWidget(mainKnobs);

        auto [mainLevel, mainLevelTitle, mainLevelValue] = addKnob(mainKnobFlow, "Level", 0, 200,
            qRound(m_canvas->getMainMix(row) * 100.0f), 100, QColor("#B58BDE"));
        auto updateMainLevel = [mainLevelValue](int value) {
            if (value == 0) mainLevelValue->setText("-inf dB");
            else {
                const double db = 20.0 * std::log10(value / 100.0);
                mainLevelValue->setText(QString("%1%2 dB").arg(db > 0.05 ? "+" : "").arg(db, 0, 'f', 1));
            }
        };
        updateMainLevel(mainLevel->value());

        const auto addBranchControls = [this, &addKnob, &makeStrip](int branchRow, bool showHeading) {
            const QString branchName = m_canvas->getBranchName(branchRow);
            auto* strip = makeStrip(showHeading ? branchName : "Branch " + branchName);
            auto* branchEnabled = new QCheckBox("Enabled", m_paramContainer);
            branchEnabled->setChecked(m_canvas->isBranchEnabled(branchRow));
            strip->addWidget(branchEnabled);
            auto* knobs = new QWidget(m_paramContainer); knobs->setStyleSheet("background:transparent; border:none;"); auto* knobFlow = new FlowLayout(knobs, 6); strip->addWidget(knobs);
            auto [level, levelTitle, levelValue] = addKnob(knobFlow, "Level", 0, 200,
                qRound(m_canvas->getMix(branchRow) * 100.0f), 100, QColor("#B58BDE"));
            auto updateLevel = [levelValue](int value) {
                if (value == 0) levelValue->setText("-inf dB");
                else {
                    const double db = 20.0 * std::log10(value / 100.0);
                    levelValue->setText(QString("%1%2 dB").arg(db > 0.05 ? "+" : "").arg(db, 0, 'f', 1));
                }
            };
            updateLevel(level->value());
            const int sourceChannels = m_canvas->getBranchOutputChannels(branchRow);
            const int destinationChannels = m_canvas->getBranchDestinationChannels(branchRow);
            const bool hasSpatialControl = sourceChannels >= 2 || destinationChannels >= 2;
            const QString spatialTitle = sourceChannels >= 2 ? "Balance" : (destinationChannels >= 2 ? "Pan" : "Mono");
            auto [pan, panTitle, panValue] = addKnob(knobFlow, spatialTitle,
                -100, 100, qRound(m_canvas->getPan(branchRow) * 100.0f), 0, QColor("#B58BDE"));
            auto updatePan = [panValue, hasSpatialControl](int value) {
                panValue->setText(!hasSpatialControl ? "Fixed"
                    : (value == 0 ? "Center" : QString("%1% %2").arg(std::abs(value)).arg(value < 0 ? "Left" : "Right")));
            };
            updatePan(pan->value());
            if (!hasSpatialControl) {
                pan->setEnabled(false);
                pan->setToolTip("Mono source returning to a mono destination has no spatial control.");
                panTitle->setToolTip(pan->toolTip());
                panValue->setToolTip(pan->toolTip());
            }
            const QString collapseReason = m_canvas->getBranchStereoCollapseReason(branchRow);
            if (!collapseReason.isEmpty()) {
                const QString warningText = QString(
                    "Stereo placement is collapsed downstream by %1. Move this MIX return after it to preserve Balance/Pan.")
                    .arg(collapseReason);
                auto* warning = new QLabel(warningText, m_paramContainer);
                warning->setWordWrap(true);
                warning->setStyleSheet(
                    "color:#E2B66E; background:#2B261D; border:1px solid #5A4930; border-radius:4px; padding:5px 7px;");
                warning->setToolTip(warningText);
                strip->addWidget(warning);
                pan->setToolTip(warningText);
            }
            auto* polarity = new QCheckBox("Invert polarity", m_paramContainer);
            polarity->setChecked(m_canvas->isPolarityInverted(branchRow));
            strip->addWidget(polarity);

            connect(branchEnabled, &QCheckBox::toggled, this, [this, branchRow](bool checked) { m_canvas->setBranchEnabled(branchRow, checked); });
            connect(level, &QDial::valueChanged, this, [this, branchRow, updateLevel](int value) {
                m_canvas->setMix(branchRow, value / 100.0f);
                updateLevel(value);
            });
            if (hasSpatialControl) {
                connect(pan, &QDial::valueChanged, this, [this, branchRow, updatePan](int value) {
                    m_canvas->setPan(branchRow, value / 100.0f);
                    updatePan(value);
                });
            }
            connect(polarity, &QCheckBox::toggled, this, [this, branchRow](bool checked) { m_canvas->setPolarityInverted(branchRow, checked); });
        };

        for (int mixerRow : mixerRows) addBranchControls(mixerRow, sharedMixer);
        m_paramLayout->addWidget(mixerHost);
        auto* hint = new QLabel(sharedMixer
            ? "This return is shared: Path A level is common, while every branch control remains independent. Drag a MIX handle away to create a separate mixer."
            : "Drag MIX to move this return. Path A level is independent until another section returns at this exact gap.", m_paramContainer);
        hint->setWordWrap(true);
        hint->setStyleSheet("color: #8b929d; margin-top: 9px;");
        m_paramLayout->addWidget(hint);

        connect(mainLevel, &QDial::valueChanged, this, [this, row, updateMainLevel](int value) {
            m_canvas->setMainMix(row, value / 100.0f);
            updateMainLevel(value);
        });
    }

    auto* remove = new QPushButton("Remove Split Section", m_paramContainer);
    remove->setEnabled(true);
    remove->setToolTip("Remove this Split and Mixer along with all plugins on this branch.");
    remove->setStyleSheet(
        "QPushButton { background:transparent; color:#D9959D; border:1px solid #694047; border-radius:5px; padding:6px 10px; font-size:11px; }"
        "QPushButton:hover { background:#321F23; color:#FFB2BA; border-color:#95545D; }"
    );
    m_paramLayout->addWidget(remove, 0, Qt::AlignRight);

    connect(remove, &QPushButton::clicked, this, [this, row] {
        m_canvas->removeSplitSection(row);
        showPluginControls(nullptr);
    });
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

    QString cacheDir = QDir::homePath() + "/.cache/RigRoom/tone3000";
    QDir().mkpath(cacheDir);
    QString localFilePath = cacheDir + "/" + safeName;

    if (!combo.isNull()) combo->setEnabled(false);
    if (!fileLabel.isNull()) fileLabel->setText("Downloading variant: 0%...");

    const QString activeKey = CredentialStore::tone3000ApiKey();
    bool useOfficial = !activeKey.isEmpty();
    if (!useOfficial) {
        if (!combo.isNull()) combo->setEnabled(true);
        if (!fileLabel.isNull()) fileLabel->setText("TONE3000 secret key required.");
        else if (!combo.isNull()) combo->setToolTip("Set a TONE3000 secret key in Settings to download this capture.");
        return;
    }

    QString finalUrlStr = urlStr;

    QNetworkRequest request((QUrl(finalUrlStr)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    if (useOfficial && !isRedirect && QUrl(finalUrlStr).host().endsWith("tone3000.com")) {
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

            AudioNode::ModelMetadata meta = node->getModelMetadata();
            parseNamFileMetadata(localFilePath, meta);
            if (!var.name.empty()) meta.toneTitle = var.name;
            node->setModelMetadata(meta);

            node->setModelDisplayName(var.name.empty() ? safeName.toStdString() : var.name);
            setUnsavedChanges(true);
            saveConfigSettings();

            if (!combo.isNull()) combo->setItemText(variantIdx, QString::fromStdString(var.name) + " (Cached)");
            showPluginControls(node);
        } else {
            if (!fileLabel.isNull()) fileLabel->setText("Failed to save model file!");
        }

        if (!combo.isNull()) combo->setEnabled(true);
    });
}
