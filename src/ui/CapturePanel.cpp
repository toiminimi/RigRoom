#include "CapturePanel.h"
#include "../audio/CaptureNode.h"
#include "InspectorComponents.h"
#include "Tone3000ImageLoader.h"
#include <QDial>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

using namespace CaptureSounds;

namespace {
constexpr int kHeight = 176;

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }
QString rgba(const QColor& c, double alpha) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

bool irOn(CaptureNode& node) {
    for (const auto& p : node.getControlPorts())
        if (p.index == CaptureNode::CabEnabled) return p.value >= 0.5f;
    return true;
}

// The picture, or drawn artwork until (or instead of) it.
QLabel* art(QWidget* parent, const std::string& imageUrl, Type type, int size) {
    auto* label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("QLabel { background: #111215; border-radius: 8px; border: none; }");
    label->setPixmap(artwork(type, QSize(size, size), label->devicePixelRatioF()));
    if (!imageUrl.empty()) Tone3000ImageLoader::instance()->load(label, QString::fromStdString(imageUrl));
    return label;
}

QLabel* chip(QWidget* parent, const QString& text, const QColor& accent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setStyleSheet(QString("QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 4px;"
                                 " padding: 1px 7px; font-size: 10px; font-weight: bold; letter-spacing: 1px; }")
                             .arg(hex(accent.lighter(130)), rgba(accent, 0.14), rgba(accent, 0.45)));
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

QPushButton* button(QWidget* parent, const QString& text, const QString& tip, const QColor& accent = QColor()) {
    auto* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    b->setStyleSheet(accent.isValid()
        ? QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 12px; font-size: 11px; font-weight: bold; }"
                  "QPushButton:hover { background: %4; }").arg(rgba(accent, 0.16), hex(accent.lighter(135)), rgba(accent, 0.5), rgba(accent, 0.28))
        : QString("QPushButton { background: #24262B; color: #D6DAE0; border: 1px solid #34373E; border-radius: 6px; padding: 5px 12px; font-size: 11px; }"
                  "QPushButton:hover { background: #2E3137; color: white; }"));
    return b;
}

QLabel* text(QWidget* parent, const QString& value, const QString& style, int width) {
    auto* label = new QLabel(parent);
    label->setStyleSheet(style + " background: transparent; border: none;");
    label->setText(QFontMetrics(label->font()).elidedText(value, Qt::ElideRight, width));
    label->setToolTip(value);
    return label;
}

QString modelName(const CaptureNode& node) {
    return !node.getModelDisplayName().empty() ? QString::fromStdString(node.getModelDisplayName())
                                               : QFileInfo(QString::fromStdString(node.modelPath())).completeBaseName();
}

QString irName(const CaptureNode& node) {
    return !node.irInfo().name.empty() ? QString::fromStdString(node.irInfo().name)
                                       : QFileInfo(QString::fromStdString(node.irPath())).completeBaseName();
}
} // namespace

QString CapturePanel::blockLabel(const CaptureNode& node) {
    const bool model = !node.modelPath().empty();
    const bool ir = !node.irPath().empty();
    if (!model && !ir) return "EMPTY";
    if (!model) return typeLabel(irType(node)).toUpper();
    QString label = typeLabel(modelType(node));
    if (ir && irOn(const_cast<CaptureNode&>(node))) label += " + " + typeLabel(irType(node));
    return label.toUpper();
}

QColor CapturePanel::blockAccent(const CaptureNode& node) {
    return node.modelPath().empty() && !node.irPath().empty() ? typeColor(irType(node)) : typeColor(modelType(node));
}

Category CapturePanel::changeCategory(const CaptureNode& node, bool ir) {
    if (ir) return irType(node) == Type::Room ? Category::Room : Category::Cab;
    switch (modelType(node)) {
    case Type::Pedal:
    case Type::Outboard: return Category::Pedal;
    case Type::FullRig: return Category::AmpCab;
    default: return node.irPath().empty() ? Category::AmpCab : Category::Amp;
    }
}

CapturePanel::CapturePanel(std::shared_ptr<CaptureNode> node, QWidget* parent)
    : QWidget(parent), m_node(std::move(node)) {
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const bool model = !m_node->modelPath().empty();
    const bool ir = !m_node->irPath().empty();
    const Type type = model ? modelType(*m_node) : irType(*m_node);
    const QColor accent = blockAccent(*m_node);

    auto* frame = new QFrame(this);
    frame->setObjectName("captureDevice");
    frame->setStyleSheet(QString(
        "QFrame#captureDevice { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #22242A, stop:1 #17181C);"
        " border: 1px solid #2E3137; border-top: 3px solid %1; border-radius: 10px; }"
        "QLabel { background: transparent; border: none; }").arg(hex(accent)));
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 10, 14, 10);
    row->setSpacing(14);

    if (!model && !ir) {
        // Normally the gallery fills the block; this is for an emptied one.
        auto* choose = button(frame, "Choose a sound...", "Pick an amp, pedal, cab or room", QColor("#E0913A"));
        connect(choose, &QPushButton::clicked, this, [this]() { emit galleryRequested(Category::AmpCab); });
        row->addStretch();
        row->addWidget(choose);
        row->addStretch();
        return;
    }

    row->addWidget(buildSound(!model), 1);
    if (model && (ir || type == Type::Amp || type == Type::Other)) {
        auto* divider = new QFrame(frame);
        divider->setFixedWidth(1);
        divider->setStyleSheet("background: #2E3137; border: none;");
        row->addWidget(divider);
        row->addWidget(ir ? buildCab() : buildNoCab());
    }
}

QWidget* CapturePanel::buildSound(bool irIsMain) {
    auto* section = new QWidget(this);
    auto* row = new QHBoxLayout(section);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);
    const Type type = irIsMain ? irType(*m_node) : modelType(*m_node);
    const QColor accent = typeColor(type);
    const std::string imageUrl = irIsMain ? m_node->irInfo().imageUrl : m_node->getModelMetadata().imageUrl;
    row->addWidget(art(section, imageUrl, type, 148), 0, Qt::AlignVCenter);

    auto* info = new QVBoxLayout();
    info->setSpacing(3);
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    chips->addWidget(chip(section, typeLabel(type), accent));
    const bool fromTone3000 = irIsMain ? !m_node->irInfo().sourceUrl.empty() : !m_node->getModelMetadata().toneId.empty();
    if (fromTone3000) chips->addWidget(chip(section, "TONE3000", QColor("#55B8E8")));
    chips->addStretch();
    if (irIsMain) {
        // The whole block is the IR: its on/off sits with its name.
        auto* power = new QPushButton(irOn(*m_node) ? "ON" : "OFF", section);
        power->setCheckable(true);
        power->setChecked(irOn(*m_node));
        power->setCursor(Qt::PointingHandCursor);
        power->setFixedSize(46, 22);
        power->setStyleSheet(QString(
            "QPushButton { background: #2A2C31; color: #8A8F98; border: 1px solid #3A3D44; border-radius: 11px; font-size: 10px; font-weight: bold; }"
            "QPushButton:checked { background: %1; color: #101114; border-color: %1; }").arg(hex(accent)));
        connect(power, &QPushButton::toggled, this, [this, power](bool on) {
            m_node->setParameter(CaptureNode::CabEnabled, on ? 1.0f : 0.0f);
            power->setText(on ? "ON" : "OFF");
            emit edited();
        });
        chips->addWidget(power);
    }
    info->addLayout(chips);
    info->addSpacing(2);
    info->addWidget(text(section, irIsMain ? irName(*m_node) : modelName(*m_node), "color: #F2F3F5; font-size: 18px; font-weight: bold;", 360));

    QStringList by;
    const auto& meta = m_node->getModelMetadata();
    if (!irIsMain) {
        if (!meta.author.empty()) by << "by " + QString::fromStdString(meta.author);
        const QString gear = gearText(QString::fromStdString(meta.gearMake), QString::fromStdString(meta.gearModel));
        if (!gear.isEmpty()) by << gear;
    }
    if (!by.isEmpty()) info->addWidget(text(section, by.join(QString::fromUtf8("  ·  ")), "color: #9AA3AE; font-size: 12px;", 360));
    if (!irIsMain && m_node->modelIsResampled()) {
        info->addWidget(text(section, QString("%1 kHz capture, resampled to the session rate").arg(m_node->modelSampleRate() / 1000.0, 0, 'g', 3),
                             "color: #C9A15A; font-size: 11px;", 360));
    }
    info->addStretch();
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* change = button(section, QString::fromUtf8("⇄  Change"), "Pick another one: click to hear, Enter to keep", accent);
    connect(change, &QPushButton::clicked, this, [this, irIsMain]() { emit galleryRequested(changeCategory(*m_node, irIsMain)); });
    buttons->addWidget(change);
    if (!irIsMain && (!meta.toneId.empty() || !m_node->getModelVariants().empty())) {
        auto* details = button(section, "Details", "Variants, description and the TONE3000 page");
        connect(details, &QPushButton::clicked, this, [this]() { emit detailsRequested(); });
        buttons->addWidget(details);
    }
    // Pedals and full rigs need no cab, but an IR after them is one click away.
    if (!irIsMain && m_node->irPath().empty() && type != Type::Amp && type != Type::Other) {
        auto* addIr = cabMenuButton(section, "+ IR");
        addIr->setToolTip("Add a cab or room IR after it");
        buttons->addWidget(addIr);
    }
    buttons->addStretch();
    info->addLayout(buttons);
    row->addLayout(info, 1);

    auto* knobs = new QHBoxLayout();
    knobs->setSpacing(10);
    if (irIsMain) {
        if (irType(*m_node) == Type::Room) knobs->addWidget(knob(section, "Mix", CaptureNode::IrMix, 0, 1, " %", 0, 100, accent));
    } else {
        knobs->addWidget(knob(section, "Input", CaptureNode::InputGainDb, -24, 24, " dB", 1, 1, accent));
    }
    knobs->addWidget(knob(section, "Output", CaptureNode::OutputGainDb, -40, 24, " dB", 1, 1, accent));
    row->addLayout(knobs);
    return section;
}

QWidget* CapturePanel::buildCab() {
    const Type type = irType(*m_node);
    const QColor accent = typeColor(type);
    const bool on = irOn(*m_node);
    auto* section = new QWidget(this);
    section->setFixedWidth(type == Type::Room ? 380 : 320);
    auto* row = new QHBoxLayout(section);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    QLabel* picture = art(section, m_node->irInfo().imageUrl, type, 96);
    if (!on) picture->setEnabled(false);
    row->addWidget(picture, 0, Qt::AlignVCenter);

    auto* info = new QVBoxLayout();
    info->setSpacing(4);
    auto* top = new QHBoxLayout();
    top->addWidget(chip(section, typeLabel(type), accent));
    top->addStretch();
    auto* power = new QPushButton(on ? "ON" : "OFF", section);
    power->setCheckable(true);
    power->setChecked(on);
    power->setCursor(Qt::PointingHandCursor);
    power->setFixedSize(46, 22);
    power->setToolTip("Cab on / off");
    power->setStyleSheet(QString(
        "QPushButton { background: #2A2C31; color: #8A8F98; border: 1px solid #3A3D44; border-radius: 11px; font-size: 10px; font-weight: bold; }"
        "QPushButton:checked { background: %1; color: #101114; border-color: %1; }").arg(hex(accent)));
    connect(power, &QPushButton::toggled, this, [this, power](bool checked) {
        m_node->setParameter(CaptureNode::CabEnabled, checked ? 1.0f : 0.0f);
        power->setText(checked ? "ON" : "OFF");
        emit changed(); // restyles the panel and the block in the chain
    });
    top->addWidget(power);
    info->addLayout(top);
    info->addWidget(text(section, irName(*m_node), QString("color: %1; font-size: 14px; font-weight: bold;").arg(on ? "#F2F3F5" : "#7A7F88"), 190));
    info->addStretch();
    info->addWidget(cabMenuButton(section, type == Type::Room ? QString::fromUtf8("Change room  ▾") : QString::fromUtf8("Change cab  ▾")),
                    0, Qt::AlignLeft);
    row->addLayout(info, 1);
    if (type == Type::Room) row->addWidget(knob(section, "Mix", CaptureNode::IrMix, 0, 1, " %", 0, 100, accent));
    return section;
}

QWidget* CapturePanel::buildNoCab() {
    // An amp without a cab sounds harsh: say so and offer the cab menu.
    auto* section = new QWidget(this);
    section->setFixedWidth(320);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addStretch();
    auto* head = new QLabel("No cab", section);
    head->setStyleSheet("color: #E0913A; font-size: 15px; font-weight: bold;");
    auto* body = new QLabel("An amp capture needs a cab after it.", section);
    body->setStyleSheet("color: #9AA3AE; font-size: 12px;");
    layout->addWidget(head);
    layout->addWidget(body);
    layout->addWidget(cabMenuButton(section, QString::fromUtf8("Choose cab  ▾")), 0, Qt::AlignLeft);
    layout->addStretch();
    return section;
}

QPushButton* CapturePanel::cabMenuButton(QWidget* parent, const QString& label) {
    auto* b = button(parent, label, "Your recent cabs, or browse all of them");
    connect(b, &QPushButton::clicked, this, [this, b]() {
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu { background: #1E1F23; color: #E0E0E0; border: 1px solid #34373E; padding: 4px; }"
            "QMenu::item { padding: 6px 14px 6px 8px; border-radius: 4px; }"
            "QMenu::item:selected { background: #00897B; color: white; }"
            "QMenu::separator { height: 1px; background: #34373E; margin: 4px 6px; }");
        const bool room = !m_node->irPath().empty() && irType(*m_node) == Type::Room;
        const QList<Item> recent = itemsIn(room ? Category::Room : Category::Cab).mid(0, 8);
        const qreal dpr = devicePixelRatioF();
        for (const Item& item : recent) {
            QPixmap icon = item.imageUrl.isEmpty() ? QPixmap()
                         : Tone3000ImageLoader::instance()->thumbnail(item.imageUrl, QSize(32, 32) * dpr);
            if (icon.isNull()) icon = artwork(item.type, QSize(32, 32), dpr);
            else icon.setDevicePixelRatio(dpr);
            QAction* action = menu.addAction(QIcon(icon), item.name);
            action->setCheckable(true);
            action->setChecked(item.path.toStdString() == m_node->irPath());
            connect(action, &QAction::triggered, this, [this, item]() {
                QString error;
                if (apply(*m_node, item, &error)) markUsed(item);
                emit changed();
            });
        }
        if (!recent.isEmpty()) menu.addSeparator();
        QAction* more = menu.addAction(room ? "All rooms..." : "All cabs...");
        connect(more, &QAction::triggered, this, [this, room]() { emit galleryRequested(room ? Category::Room : Category::Cab); });
        if (!m_node->irPath().empty()) {
            QAction* none = menu.addAction(room ? "No room" : "No cab");
            connect(none, &QAction::triggered, this, [this]() {
                m_node->loadIr("");
                m_node->setIrInfo({});
                emit changed();
            });
        }
        menu.exec(b->mapToGlobal(QPoint(0, b->height() + 2)));
    });
    return b;
}

QWidget* CapturePanel::knob(QWidget* parent, const QString& name, uint32_t param, double min, double max,
                            const QString& unit, int decimals, double display, const QColor& accent) {
    // The dial counts in steps of the last shown decimal.
    const double scale = display * std::pow(10.0, decimals);
    float value = 0.0f, def = 0.0f;
    for (const auto& p : m_node->getControlPorts()) {
        if (p.index == param) {
            value = p.value;
            def = p.defaultVal;
        }
    }
    auto* cell = new QWidget(parent);
    cell->setStyleSheet("background: transparent; border: none;");
    auto* layout = new QVBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    auto* title = new QLabel(name.toUpper(), cell);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #9AA3AE; font-size: 10px; font-weight: bold; letter-spacing: 1px;");
    auto* dial = new InspectorKnob(cell);
    dial->setFixedSize(62, 62);
    dial->setRange(qRound(min * scale), qRound(max * scale));
    dial->setDefaultValue(qRound(def * scale));
    dial->setValue(qRound(value * scale));
    dial->setAccentColor(accent);
    dial->setAccessibleName(name);
    dial->setToolTip(name + "\nDrag to adjust. Double-click to reset.");
    auto* valueLabel = new InspectorValueLabel(cell);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(hex(accent.lighter(125))));
    auto show = [dial, valueLabel, scale, display, decimals, unit](int v) {
        const QString shown = QString::number(v / scale * display, 'f', decimals) + unit;
        valueLabel->setText(shown);
        dial->setAccessibleValueText(shown);
    };
    show(dial->value());
    valueLabel->setEditor("Set " + name, min * display, max * display, decimals,
        [dial, scale, display] { return dial->value() / scale * display; },
        [dial, scale, display](double v) { dial->setValue(qRound(v / display * scale)); });
    connect(dial, &QDial::valueChanged, this, [this, param, scale, show](int v) {
        m_node->setParameter(param, static_cast<float>(v / scale));
        show(v);
        emit edited();
    });
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(dial, 0, Qt::AlignHCenter);
    layout->addWidget(valueLabel);
    layout->addStretch();
    return cell;
}
