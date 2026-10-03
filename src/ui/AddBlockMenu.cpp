#include "AddBlockMenu.h"
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

using namespace CaptureSounds;

namespace {
QString hex(const QColor& c) { return c.name(QColor::HexRgb); }
QString rgba(const QColor& c, double a) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

// A big tile: artwork, name, what it is, and how many the library has.
QPushButton* tile(QWidget* parent, Type art, const QString& title, const QString& hint, const QString& count) {
    const QColor accent = typeColor(art);
    auto* button = new QPushButton(parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(244, 96);
    button->setStyleSheet(QString(
        "QPushButton { background: #1A1B1F; border: 1px solid #2C2E34; border-radius: 10px; text-align: left; }"
        "QPushButton:hover { background: %1; border-color: %2; }"
        "QPushButton:focus { border-color: %2; }").arg(rgba(accent, 0.10), hex(accent)));
    auto* row = new QHBoxLayout(button);
    row->setContentsMargins(10, 10, 12, 10);
    row->setSpacing(12);
    auto* icon = new QLabel(button);
    icon->setFixedSize(76, 76);
    icon->setPixmap(artwork(art, QSize(76, 76), button->devicePixelRatioF()));
    row->addWidget(icon);
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* name = new QLabel(title, button);
    name->setStyleSheet(QString("color: %1; font-size: 15px; font-weight: bold;").arg(hex(accent.lighter(115))));
    auto* desc = new QLabel(hint, button);
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #9AA3AE; font-size: 11px;");
    auto* number = new QLabel(count, button);
    number->setStyleSheet("color: #6E747D; font-size: 10px;");
    text->addWidget(name);
    text->addWidget(desc);
    text->addStretch();
    text->addWidget(number);
    row->addLayout(text, 1);
    for (QLabel* l : {icon, name, desc, number}) {
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->setStyleSheet(l->styleSheet() + "background: transparent; border: none;");
    }
    return button;
}
} // namespace

AddBlockMenu::AddBlockMenu(QWidget* parent) : QDialog(parent) {
    setWindowFlags(Qt::Popup);
    setStyleSheet("QDialog { background-color: #141518; border: 1px solid #3A3A42; border-radius: 10px; }");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 14);
    outer->setSpacing(10);
    auto* title = new QLabel("Add to the chain", this);
    title->setStyleSheet("color: #F2F3F5; font-size: 15px; font-weight: bold; background: transparent;");
    outer->addWidget(title);

    auto* grid = new QGridLayout();
    grid->setSpacing(10);
    const QList<Category> categories = {Category::AmpCab, Category::Amp, Category::Cab, Category::Pedal, Category::Room};
    int i = 0;
    for (Category category : categories) {
        const int count = itemsIn(category).size();
        auto* button = tile(this, categoryType(category), categoryTitle(category), categoryHint(category),
                            count == 0 ? QString("none in your library yet")
                                       : QString("%1 in your library").arg(count));
        connect(button, &QPushButton::clicked, this, [this, category]() {
            m_choice = category;
            accept();
        });
        grid->addWidget(button, i / 3, i % 3);
        ++i;
    }
    auto* plugin = tile(this, Type::Other, "Plugin", "LV2, VST3 and CLAP effects", "browse installed plugins");
    connect(plugin, &QPushButton::clicked, this, [this]() {
        m_plugin = true;
        accept();
    });
    grid->addWidget(plugin, 1, 2);
    outer->addLayout(grid);
}

std::optional<Category> AddBlockMenu::choose(const QPoint& globalPos) {
    adjustSize();
    QPoint pos = globalPos;
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left() + 8, avail.right() - width() - 8));
        pos.setY(std::clamp(pos.y(), avail.top() + 8, avail.bottom() - height() - 8));
    }
    move(pos);
    if (exec() != QDialog::Accepted) return std::nullopt;
    return m_choice;
}
