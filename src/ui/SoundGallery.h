#pragma once
#include "CaptureSounds.h"
#include <QDialog>
#include <memory>

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QComboBox;
class QStandardItemModel;
class QWidget;

// Pick a sound for a capture block, Quad Cortex style: cards with pictures,
// a preview pane, and every click heard right away in the block. Enter keeps
// it, Esc puts back what the block had. "Amp + Cab" continues with a cab step
// after an amp.
class SoundGallery : public QDialog {
    Q_OBJECT
public:
    // Result code when the user asked for TONE3000 instead.
    static constexpr int OpenTone3000 = 2;

    SoundGallery(std::shared_ptr<CaptureNode> target, CaptureSounds::Category category, QWidget* parent = nullptr);
    void placeUnder(QWidget* anchor, QWidget* window);
    // Which TONE3000 browser fits the category (IRs for Cab / Room).
    bool wantsIr() const;

    void reject() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void showCategory(CaptureSounds::Category category, const QString& preselectPath = {});
    void refill();
    void preview(int row);
    void showDetails(int row);
    void commit();
    void finish();
    const CaptureSounds::Item* itemAt(int row) const;

    std::shared_ptr<CaptureNode> m_target;
    CaptureSounds::Snapshot m_before;
    CaptureSounds::Category m_category;
    bool m_cabStep = false;
    QList<CaptureSounds::Item> m_items; // the category
    QList<int> m_shown;                 // indexes into m_items after search / sort

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_sort = nullptr;
    QWidget* m_stepBanner = nullptr;
    QLabel* m_stepLabel = nullptr;
    QListView* m_list = nullptr;
    QStandardItemModel* m_model = nullptr;
    QLabel* m_empty = nullptr;
    QLabel* m_previewImage = nullptr;
    QLabel* m_previewChips = nullptr;
    QLabel* m_previewName = nullptr;
    QLabel* m_previewBy = nullptr;
    QLabel* m_previewFacts = nullptr;
    QLabel* m_previewText = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_use = nullptr;
    QPushButton* m_skip = nullptr;
};
