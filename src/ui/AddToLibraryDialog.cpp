#include "AddToLibraryDialog.h"
#include "BrowserStyle.h"

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using NamMetadata::TagCategory;
using Tone3000::Format;

AddToLibraryDialog::AddToLibraryDialog(const QString& title, const QList<Row>& rows, Format format, QWidget* parent)
    : QDialog(parent), m_format(format) {
    setWindowTitle(title);
    setStyleSheet(BrowserStyle::kStyleSheet);
    resize(680, 680);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setStyleSheet(
        "QTreeWidget { background: #17171B; border: 1px solid #26262C; border-radius: 6px; color: #D8D8DE; "
        "font-size: 12px; outline: none; } QTreeWidget::item { padding: 3px 2px; }"
        "QTreeWidget::item:hover { background: #202026; } QTreeWidget::item:selected { background: #0B4F6C; }");
    QMap<QString, QTreeWidgetItem*> groups;
    for (const Row& row : rows) {
        QTreeWidgetItem*& group = groups[row.group.toLower() + row.group];
        if (!group) {
            group = new QTreeWidgetItem();
            group->setText(0, row.group);
            group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            group->setCheckState(0, Qt::Checked);
            QFont font = group->font(0);
            font.setBold(true);
            group->setFont(0, font);
        }
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, row.detail.isEmpty() ? row.name : row.name + "     " + row.detail);
        item->setToolTip(0, row.key);
        item->setData(0, Qt::UserRole, row.key);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }
    for (QTreeWidgetItem* group : groups) {
        group->setText(0, group->text(0) + QString("  (%1)").arg(group->childCount()));
        m_tree->addTopLevelItem(group);
    }
    // Long lists start folded, short ones open.
    if (rows.size() <= 30) m_tree->expandAll();
    connect(m_tree, &QTreeWidget::itemChanged, this, &AddToLibraryDialog::updateCount);
    layout->addWidget(m_tree, 1);

    auto* ticks = new QHBoxLayout();
    auto* all = new QPushButton("Select All", this);
    auto* none = new QPushButton("Select None", this);
    for (QPushButton* b : {all, none}) {
        b->setAutoDefault(false);
        ticks->addWidget(b);
    }
    auto setAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) m_tree->topLevelItem(i)->setCheckState(0, state);
    };
    connect(all, &QPushButton::clicked, this, [setAll]() { setAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [setAll]() { setAll(Qt::Unchecked); });
    m_count = new QLabel(this);
    m_count->setStyleSheet("color: #8A8A96; font-size: 11px;");
    ticks->addWidget(m_count);
    ticks->addStretch();
    layout->addLayout(ticks);

    // What they all get. Empty fields leave each file's own details.
    auto* heading = new QLabel("FOR ALL OF THEM", this);
    heading->setStyleSheet("color: #8A8A96; font-size: 10px; font-weight: bold; margin-top: 4px;");
    layout->addWidget(heading);
    auto* form = new QFormLayout();
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    CaptureLibrary& library = CaptureLibrary::instance();
    m_folder = new QComboBox(this);
    for (const QString& folder : library.folders()) m_folder->addItem(folder, folder);
    m_folder->addItem(QString::fromUtf8("New folder…"), QString());
    m_folder->setCurrentIndex(std::max(0, m_folder->findData(library.lastFolder())));
    connect(m_folder, &QComboBox::activated, this, [this](int index) {
        if (!m_folder->itemData(index).toString().isEmpty()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_folder->insertItem(m_folder->count() - 1, name, name);
            m_folder->setCurrentIndex(m_folder->count() - 2);
        } else {
            m_folder->setCurrentIndex(0);
        }
    });
    // A pack groups captures that belong together, like a TONE3000 tone.
    m_pack = new QComboBox(this);
    m_pack->addItem("No pack", QString());
    for (const CaptureLibrary::Pack& pack : library.packs()) m_pack->addItem(pack.title, pack.id);
    m_pack->addItem(QString::fromUtf8("New pack…"), QString("__new"));
    const QString suggested = rows.isEmpty() ? QString() : rows.first().group.section(QString::fromUtf8("  ·  "), 0, 0);
    connect(m_pack, &QComboBox::activated, this, [this, suggested](int index) {
        if (m_pack->itemData(index).toString() != "__new") return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Pack", "Pack name:", QLineEdit::Normal, suggested, &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_pack->insertItem(m_pack->count() - 1, name, "new:" + name);
            m_pack->setCurrentIndex(m_pack->count() - 2);
        } else {
            m_pack->setCurrentIndex(0);
        }
    });
    m_tags = new QLineEdit(this);
    m_tags->setPlaceholderText("e.g. live, songs-2026 (comma separated)");
    m_type = new QComboBox(this);
    m_type->addItem("From each file", QString());
    const QStringList types = format == Format::Ir ? QStringList{"cab", "space", "pedal", "outboard"}
                                                   : QStringList{"amp-cab", "amp", "pedal", "outboard"};
    for (const QString& type : types) m_type->addItem(NamMetadata::tagLabel(type), type);
    m_tone = new QComboBox(this);
    m_tone->addItem("From each file", QString());
    for (const QString& tone : NamMetadata::vocabulary(TagCategory::Tone)) m_tone->addItem(NamMetadata::tagLabel(tone), tone);
    m_make = new QLineEdit(this);
    m_make->setPlaceholderText("From each file");
    m_model = new QLineEdit(this);
    m_model->setPlaceholderText("From each file");
    m_rating = new QComboBox(this);
    m_rating->addItem("No rating", 0);
    for (int stars = 1; stars <= 5; ++stars) m_rating->addItem(QString(stars, QChar(0x2605)), stars);
    auto label = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet("color: #9FA8B8; font-size: 12px;");
        return l;
    };
    form->addRow(label("Folder"), m_folder);
    form->addRow(label("Pack"), m_pack);
    form->addRow(label("Tags"), m_tags);
    form->addRow(label("Type"), m_type);
    form->addRow(label("Tone"), m_tone);
    form->addRow(label("Make"), m_make);
    form->addRow(label("Model"), m_model);
    form->addRow(label("Rating"), m_rating);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_addButton = new QPushButton(this);
    m_addButton->setStyleSheet(BrowserStyle::kPrimaryButton);
    m_addButton->setDefault(true);
    connect(m_addButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(m_addButton);
    layout->addLayout(buttons);
    updateCount();
}

QStringList AddToLibraryDialog::selectedKeys() const {
    QStringList keys;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            if (group->child(j)->checkState(0) == Qt::Checked) keys << group->child(j)->data(0, Qt::UserRole).toString();
        }
    }
    return keys;
}

CaptureLibrary::AddOptions AddToLibraryDialog::options() const {
    CaptureLibrary::AddOptions options;
    options.folder = m_folder->currentData().toString();
    for (const QString& tag : m_tags->text().split(',', Qt::SkipEmptyParts)) options.tags << tag.trimmed();
    options.gear = CaptureLibrary::Gear{m_type->currentData().toString(), m_make->text().trimmed(),
                                        m_model->text().trimmed(), m_tone->currentData().toString()};
    options.rating = m_rating->currentData().toInt();
    options.pack = m_pack->currentData().toString(); // "new:<name>" asks the caller to create it
    return options;
}

void AddToLibraryDialog::updateCount() {
    const int count = static_cast<int>(selectedKeys().size());
    m_count->setText(QString("%1 selected").arg(count));
    const QString noun = m_format == Format::Ir ? (count == 1 ? "IR" : "IRs") : (count == 1 ? "Capture" : "Captures");
    m_addButton->setText(QString("Add %1 %2").arg(count).arg(noun));
    m_addButton->setEnabled(count > 0);
}
