#pragma once
#include "CaptureLibrary.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// Adding several captures to the library at once: which ones (ticked in a
// list grouped by tone or folder), which library folder they go to, and what
// they all share (tags, type, tone, make and model, rating). Anything left
// empty comes from each file itself.
class AddToLibraryDialog : public QDialog {
    Q_OBJECT
public:
    struct Row {
        QString key;     // a file path, or whatever the caller maps back
        QString group;   // rows with the same group are listed together
        QString name;
        QString detail;  // shown after the name, dimmed
    };
    AddToLibraryDialog(const QString& title, const QList<Row>& rows, Tone3000::Format format, QWidget* parent = nullptr);

    QStringList selectedKeys() const;
    CaptureLibrary::AddOptions options() const;

private:
    void updateCount();

    Tone3000::Format m_format;
    QTreeWidget* m_tree = nullptr;
    QComboBox* m_folder = nullptr;
    QComboBox* m_pack = nullptr;
    QLineEdit* m_tags = nullptr;
    QComboBox* m_type = nullptr;
    QComboBox* m_tone = nullptr;
    QLineEdit* m_make = nullptr;
    QLineEdit* m_model = nullptr;
    QComboBox* m_rating = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_addButton = nullptr;
};
