#include "CapturePanel.h"
#include "../audio/CaptureNode.h"
#include "InspectorComponents.h"
#include "NamMetadata.h"
#include "Tone3000ImageLoader.h"
#include <QDial>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr int kArt = 132; // image / artwork size in the cards

QString gearOf(const std::string& raw) {
    return NamMetadata::normalizedGear(QString::fromStdString(raw));
}

// IR uploads use their own types (cab, space, pedal, outboard).
QString irGearOf(const std::string& raw) {
    const QString g = QString::fromStdString(raw).toLower();
    if (g.contains("space") || g.contains("room") || g.contains("reverb") || g.contains("hall")) return "space";
    if (g.contains("pedal")) return "pedal";
    if (g.contains("outboard")) return "outboard";
    return "cab";
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

QString rgba(const QColor& c, double alpha) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

enum class Art { Amp, Pedal, Cab, Room };

// Drawn when a capture or IR has no picture.
QPixmap placeholderArt(Art art, const QColor& accent, qreal dpr) {
    QPixmap pm(QSize(kArt, kArt) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kArt, kArt), 8, 8);
    p.setClipPath(clip);
    p.fillRect(QRectF(0, 0, kArt, kArt), QColor("#141518"));
    if (art == Art::Pedal) {
        // A stompbox: enclosure, two knobs, a footswitch and its LED.
        const QRectF box(30, 12, kArt - 60, kArt - 24);
        p.setPen(QPen(accent.darker(150), 2));
        p.setBrush(QColor("#1F2126"));
        p.drawRoundedRect(box, 8, 8);
        for (int i = 0; i < 2; ++i) {
            const QPointF k(box.left() + 20 + i * 32, box.top() + 24);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#0E0F11"));
            p.drawEllipse(k, 10, 10);
            p.setPen(QPen(accent.lighter(130), 2));
            p.drawLine(k, k + QPointF(i ? 5 : -5, -6));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QPointF(box.center().x(), box.top() + 52), 3.5, 3.5);
        p.setBrush(QColor("#9AA0A8"));
        p.drawEllipse(QPointF(box.center().x(), box.bottom() - 24), 11, 11);
        p.setBrush(QColor("#6C727A"));
        p.drawEllipse(QPointF(box.center().x(), box.bottom() - 24), 7, 7);
    } else if (art == Art::Room) {
        // Reflections spreading from a source.
        p.setBrush(Qt::NoBrush);
        const QPointF c(30, kArt / 2.0);
        for (int i = 1; i <= 5; ++i) {
            QColor ring = accent;
            ring.setAlphaF(1.0 - i * 0.16);
            p.setPen(QPen(ring, 2.5));
            const qreal r = i * 19.0;
            p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), -60 * 16, 120 * 16);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(c, 6, 6);
    } else if (art == Art::Cab) {
        // Grille cloth with a speaker behind it.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#202227"));
        for (int y = 6; y < kArt; y += 7)
            for (int x = (y / 7) % 2 ? 6 : 9; x < kArt; x += 7) p.drawEllipse(QPointF(x, y), 1.4, 1.4);
        const QPointF c(kArt / 2.0, kArt / 2.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent.darker(160), 3));
        p.drawEllipse(c, 46, 46);
        p.setPen(QPen(accent.darker(120), 2));
        p.drawEllipse(c, 30, 30);
        p.setBrush(accent.darker(130));
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, 12, 12);
    } else {
        // Amp face: a control strip with knobs over a grille.
        p.fillRect(QRectF(0, 0, kArt, 46), QColor("#1D1F24"));
        p.fillRect(QRectF(0, 44, kArt, 3), accent);
        for (int i = 0; i < 5; ++i) {
            const QPointF k(18 + i * 24, 23);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#0E0F11"));
            p.drawEllipse(k, 8, 8);
            p.setPen(QPen(accent.lighter(130), 2));
            p.drawLine(k, k + QPointF(std::cos(-2.2 + i * 0.9) * 6, std::sin(-2.2 + i * 0.9) * 6));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#202227"));
        for (int y = 56; y < kArt; y += 7)
            for (int x = (y / 7) % 2 ? 6 : 9; x < kArt; x += 7) p.drawEllipse(QPointF(x, y), 1.4, 1.4);
    }
    return pm;
}

QLabel* artLabel(QWidget* parent, const std::string& imageUrl, Art kind, const QColor& accent) {
    auto* art = new QLabel(parent);
    art->setFixedSize(kArt, kArt);
    art->setAlignment(Qt::AlignCenter);
    art->setStyleSheet("QLabel { background: #141518; border-radius: 8px; border: none; }");
    art->setPixmap(placeholderArt(kind, accent, art->devicePixelRatioF()));
    if (!imageUrl.empty()) Tone3000ImageLoader::instance()->load(art, QString::fromStdString(imageUrl));
    return art;
}

QLabel* chip(QWidget* parent, const QString& text, const QColor& accent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setStyleSheet(QString("QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 4px;"
                                 " padding: 1px 7px; font-size: 10px; font-weight: bold; letter-spacing: 1px; }")
                             .arg(hex(accent.lighter(130)), rgba(accent, 0.14), rgba(accent, 0.45)));
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

QPushButton* flatButton(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    b->setStyleSheet("QPushButton { background: #24262B; color: #D6DAE0; border: 1px solid #34373E; border-radius: 5px;"
                     " padding: 4px 10px; font-size: 11px; }"
                     "QPushButton:hover { background: #2E3137; color: white; }");
    return b;
}

QFrame* card(QWidget* parent, const QColor& accent, bool dim) {
    auto* frame = new QFrame(parent);
    frame->setObjectName("captureCard");
    frame->setStyleSheet(QString(
        "QFrame#captureCard { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2);"
        " border: 1px solid #30333A; border-top: 3px solid %3; border-radius: 10px; }"
        "QLabel { border: none; background: transparent; }")
        .arg(dim ? "#1A1B1E" : "#23252A", dim ? "#141517" : "#17181B", dim ? rgba(accent, 0.35) : hex(accent)));
    return frame;
}

QLabel* elided(QWidget* parent, const QString& text, const QString& style, int width) {
    auto* label = new QLabel(parent);
    label->setStyleSheet(style);
    label->setText(QFontMetrics(label->font()).elidedText(text, Qt::ElideRight, width));
    label->setToolTip(text);
    return label;
}
} // namespace

CapturePanel::Kind CapturePanel::kindOf(const CaptureNode& node) {
    const bool model = !node.modelPath().empty();
    const bool ir = !node.irPath().empty();
    if (!model) return ir ? Kind::IrOnly : Kind::Empty;
    const QString gear = gearOf(node.getModelMetadata().gearType);
    if (gear == "amp") return Kind::Amp;
    if (gear == "amp-cab") return Kind::AmpCab;
    if (gear == "pedal") return Kind::Pedal;
    if (gear == "outboard") return Kind::Outboard;
    return Kind::Capture;
}

QString CapturePanel::modelTypeLabel(const CaptureNode& node) {
    switch (kindOf(node)) {
    case Kind::Amp: return "Amp";
    case Kind::AmpCab: return "Amp + Cab";
    case Kind::Pedal: return "Pedal";
    case Kind::Outboard: return "Outboard";
    default: return "Capture";
    }
}

QString CapturePanel::irTypeLabel(const CaptureNode& node) {
    const QString gear = irGearOf(node.irInfo().gearType);
    if (gear == "space") return "Room";
    if (gear == "pedal") return "Pedal IR";
    if (gear == "outboard") return "Outboard IR";
    return "Cab";
}

QColor CapturePanel::modelAccent(const CaptureNode& node) {
    switch (kindOf(node)) {
    case Kind::Amp: return QColor("#E0913A");
    case Kind::AmpCab: return QColor("#E06A3A");
    case Kind::Pedal: return QColor("#4CC38A");
    case Kind::Outboard: return QColor("#9C7BE0");
    default: return QColor("#6FA8DC");
    }
}

QColor CapturePanel::irAccent(const CaptureNode& node) {
    const QString gear = irGearOf(node.irInfo().gearType);
    if (gear == "space") return QColor("#7E6BD9");
    if (gear == "pedal") return QColor("#4CC38A");
    if (gear == "outboard") return QColor("#9C7BE0");
    return QColor("#8FA3B8");
}

QString CapturePanel::blockLabel(const CaptureNode& node) {
    const Kind kind = kindOf(node);
    if (kind == Kind::Empty) return "EMPTY";
    if (kind == Kind::IrOnly) return irTypeLabel(node).toUpper();
    bool irOn = !node.irPath().empty();
    for (const auto& p : const_cast<CaptureNode&>(node).getControlPorts()) {
        if (p.index == CaptureNode::CabEnabled && p.value < 0.5f) irOn = false;
    }
    QString label = modelTypeLabel(node);
    if (irOn) label += " + " + irTypeLabel(node);
    return label.toUpper();
}

QColor CapturePanel::blockAccent(const CaptureNode& node) {
    return kindOf(node) == Kind::IrOnly ? irAccent(node) : modelAccent(node);
}

CapturePanel::CapturePanel(std::shared_ptr<CaptureNode> node, QWidget* parent)
    : QWidget(parent), m_node(std::move(node)) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    switch (kindOf(*m_node)) {
    case Kind::Empty:
        row->addWidget(buildGhost("Amp, pedal or capture", "Pick a NAM capture from your library or TONE3000", false, true), 1);
        row->addWidget(buildGhost("Cab, room or effect IR", "Pick an impulse response", true, true), 1);
        break;
    case Kind::IrOnly:
        row->addWidget(buildIrCard(true), 1);
        row->addWidget(buildGhost("+ Capture", "Put an amp or pedal capture in front of this IR", false, false));
        break;
    case Kind::Amp:
        // An amp needs a cab: the cab card is always there, asking if empty.
        row->addWidget(buildModelCard(), 3);
        row->addWidget(m_node->irPath().empty()
                           ? buildGhost("No cab yet", "An amp capture needs a cab IR after it. Choose one", true, true)
                           : buildIrCard(false), 2);
        break;
    default:
        row->addWidget(buildModelCard(), 3);
        if (m_node->irPath().empty()) {
            row->addWidget(buildGhost("+ IR", kindOf(*m_node) == Kind::AmpCab
                                                  ? "The cab is already in this capture. Add an IR anyway"
                                                  : "Add a cab, room or effect IR after it", true, false));
        } else {
            row->addWidget(buildIrCard(false), 2);
        }
        break;
    }
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
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(2);
    auto* title = new QLabel(name.toUpper(), cell);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #9AA3AE; font-size: 9px; font-weight: bold; letter-spacing: 1px;");
    auto* dial = new InspectorKnob(cell);
    dial->setRange(qRound(min * scale), qRound(max * scale));
    dial->setDefaultValue(qRound(def * scale));
    dial->setValue(qRound(value * scale));
    dial->setAccentColor(accent);
    dial->setAccessibleName(name);
    dial->setToolTip(name + "\nDrag to adjust. Double-click to reset.");
    auto* valueLabel = new InspectorValueLabel(cell);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(hex(accent.lighter(125))));
    auto show = [dial, valueLabel, scale, display, decimals, unit](int v) {
        const QString text = QString::number(v / scale * display, 'f', decimals) + unit;
        valueLabel->setText(text);
        dial->setAccessibleValueText(text);
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
    layout->addWidget(title);
    layout->addWidget(dial, 0, Qt::AlignHCenter);
    layout->addWidget(valueLabel);
    return cell;
}

QWidget* CapturePanel::buildModelCard() {
    const QColor accent = modelAccent(*m_node);
    const auto& meta = m_node->getModelMetadata();
    auto* frame = card(this, accent, false);
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 12, 12, 12);
    row->setSpacing(14);
    const Art art = kindOf(*m_node) == Kind::Pedal ? Art::Pedal : Art::Amp;
    row->addWidget(artLabel(frame, meta.imageUrl, art, accent), 0, Qt::AlignTop);

    auto* info = new QVBoxLayout();
    info->setSpacing(4);
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    chips->addWidget(chip(frame, modelTypeLabel(*m_node), accent));
    if (!meta.toneId.empty()) chips->addWidget(chip(frame, "TONE3000", QColor("#55B8E8")));
    chips->addStretch();
    info->addLayout(chips);

    const QString title = !m_node->getModelDisplayName().empty() ? QString::fromStdString(m_node->getModelDisplayName())
                        : QFileInfo(QString::fromStdString(m_node->modelPath())).completeBaseName();
    info->addWidget(elided(frame, title, "color: #F2F3F5; font-size: 16px; font-weight: bold;", 300));
    QStringList details;
    if (!meta.author.empty()) details << "by " + QString::fromStdString(meta.author);
    const QString gear = QString::fromStdString(meta.gearMake + " " + meta.gearModel).trimmed();
    if (!gear.isEmpty()) details << gear;
    if (m_node->modelIsResampled()) {
        details << QString("%1 kHz model, resampled").arg(m_node->modelSampleRate() / 1000.0, 0, 'g', 3);
    }
    if (!details.isEmpty()) {
        info->addWidget(elided(frame, details.join(QString::fromUtf8("  ·  ")), "color: #9AA3AE; font-size: 11px;", 300));
    }
    info->addStretch();

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* change = flatButton(frame, "Change...", "Pick another capture (Captures or IRs)");
    connect(change, &QPushButton::clicked, this, [this]() { emit browseRequested(false); });
    buttons->addWidget(change);
    if (!meta.toneId.empty() || !m_node->getModelVariants().empty()) {
        auto* details = flatButton(frame, "Details", "Variants, description and the TONE3000 page");
        connect(details, &QPushButton::clicked, this, [this]() { emit detailsRequested(); });
        buttons->addWidget(details);
    }
    auto* remove = flatButton(frame, QString::fromUtf8("✕"), "Remove the capture from this block");
    remove->setFixedWidth(30);
    connect(remove, &QPushButton::clicked, this, [this]() { emit removeRequested(false); });
    buttons->addWidget(remove);
    buttons->addStretch();
    info->addLayout(buttons);
    row->addLayout(info, 1);

    auto* knobs = new QHBoxLayout();
    knobs->setSpacing(6);
    knobs->addWidget(knob(frame, "Input", CaptureNode::InputGainDb, -24, 24, " dB", 1, 1, accent));
    knobs->addWidget(knob(frame, "Output", CaptureNode::OutputGainDb, -40, 24, " dB", 1, 1, accent));
    row->addLayout(knobs);
    return frame;
}

QWidget* CapturePanel::buildIrCard(bool alone) {
    const QColor accent = irAccent(*m_node);
    const auto& ir = m_node->irInfo();
    bool on = true;
    for (const auto& p : m_node->getControlPorts()) {
        if (p.index == CaptureNode::CabEnabled) on = p.value >= 0.5f;
    }
    auto* frame = card(this, accent, !on);
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 12, 12, 12);
    row->setSpacing(12);
    QLabel* art = artLabel(frame, ir.imageUrl, irTypeLabel(*m_node) == "Room" ? Art::Room : Art::Cab, accent);
    if (!alone) art->setFixedSize(96, 96), art->setScaledContents(true);
    row->addWidget(art, 0, Qt::AlignTop);

    auto* info = new QVBoxLayout();
    info->setSpacing(4);
    auto* top = new QHBoxLayout();
    top->setSpacing(6);
    top->addWidget(chip(frame, irTypeLabel(*m_node), accent));
    top->addStretch();
    // On/off for the IR stage (the cab of an amp, or the whole IR block).
    auto* power = new QPushButton(on ? "ON" : "OFF", frame);
    power->setCheckable(true);
    power->setChecked(on);
    power->setCursor(Qt::PointingHandCursor);
    power->setFixedSize(46, 22);
    power->setToolTip(on ? "Turn the IR off" : "Turn the IR on");
    power->setStyleSheet(QString(
        "QPushButton { background: #2A2C31; color: #8A8F98; border: 1px solid #3A3D44; border-radius: 11px; font-size: 10px; font-weight: bold; }"
        "QPushButton:checked { background: %1; color: #101114; border-color: %1; }").arg(hex(accent)));
    connect(power, &QPushButton::toggled, this, [this](bool checked) {
        m_node->setParameter(CaptureNode::CabEnabled, checked ? 1.0f : 0.0f);
        emit edited();
    });
    top->addWidget(power);
    info->addLayout(top);

    const QString name = !ir.name.empty() ? QString::fromStdString(ir.name)
                       : QFileInfo(QString::fromStdString(m_node->irPath())).completeBaseName();
    info->addWidget(elided(frame, name, QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(on ? "#F2F3F5" : "#8A8F98").arg(alone ? 16 : 13), alone ? 300 : 200));
    info->addStretch();
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* change = flatButton(frame, "Change...", "Pick another IR");
    connect(change, &QPushButton::clicked, this, [this]() { emit browseRequested(true); });
    buttons->addWidget(change);
    auto* remove = flatButton(frame, QString::fromUtf8("✕"), "Remove the IR from this block");
    remove->setFixedWidth(30);
    connect(remove, &QPushButton::clicked, this, [this]() { emit removeRequested(true); });
    buttons->addWidget(remove);
    buttons->addStretch();
    info->addLayout(buttons);
    row->addLayout(info, 1);

    // A cab runs fully wet; rooms and effect IRs blend with the dry signal.
    auto* knobs = new QHBoxLayout();
    knobs->setSpacing(6);
    if (irTypeLabel(*m_node) != "Cab") knobs->addWidget(knob(frame, "Mix", CaptureNode::IrMix, 0, 1, " %", 0, 100, accent));
    if (alone) knobs->addWidget(knob(frame, "Output", CaptureNode::OutputGainDb, -40, 24, " dB", 1, 1, accent));
    if (knobs->count() > 0) row->addLayout(knobs);
    else delete knobs;
    return frame;
}

QWidget* CapturePanel::buildGhost(const QString& title, const QString& text, bool ir, bool wide) {
    auto* button = new QPushButton(this);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(text);
    button->setMinimumHeight(156);
    if (!wide) button->setFixedWidth(150);
    const QColor accent = ir ? QColor("#8FA3B8") : QColor("#E0913A");
    button->setStyleSheet(QString(
        "QPushButton { background: transparent; border: 2px dashed #34373E; border-radius: 10px; }"
        "QPushButton:hover { border-color: %1; background: %2; }").arg(hex(accent), rgba(accent, 0.06)));
    auto* layout = new QVBoxLayout(button);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->addStretch();
    auto* head = new QLabel(title, button);
    head->setAlignment(Qt::AlignCenter);
    head->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold; background: transparent; border: none;").arg(hex(accent)));
    auto* body = new QLabel(text, button);
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    body->setStyleSheet("color: #8A8F98; font-size: 11px; background: transparent; border: none;");
    for (QLabel* l : {head, body}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(head);
    layout->addWidget(body);
    layout->addStretch();
    connect(button, &QPushButton::clicked, this, [this, ir]() { emit browseRequested(ir); });
    return button;
}
