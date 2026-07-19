#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <vector>
#include <memory>
#include <unordered_map>
#include <QTimer>
#include "GridRow.h"
#include "../audio/AudioEngine.h"
#include "NodeWidget.h"
#include "PortWidget.h"

class PlusButtonWidget;

class NodeCanvas : public QGraphicsView {
    Q_OBJECT
signals:
    void routingChanged();
    void branchSelected(int row);
public:
    explicit NodeCanvas(AudioEngine* engine, QWidget* parent = nullptr);
    ~NodeCanvas() override;
    
    // Insert / remove plugin at grid position
    void insertPluginAt(int row, int col, std::shared_ptr<AudioNode> audioNode);
    // Insert before the node currently at col (shifts others right)
    void insertPluginBefore(int row, int col, std::shared_ptr<AudioNode> audioNode);
    void removePluginAt(int row, int col);
    void replacePluginAt(int row, int col, std::shared_ptr<AudioNode> newNode);
    // Move a plugin from one slot to another (reorder)
    void movePlugin(int fromRow, int fromCol, int toRow, int toCol);
    void movePluginToGap(int fromRow, int fromCol, int toRow, int toColGap);

    void setDragGap(int row, int plusIdx);
    void clearDragGap();
    int getDragGapRow() const { return m_dragGapRow; }
    int getDragGapCol() const { return m_dragGapCol; }
    
    // Full layout recalculation
    void updateLayout();
    void clearCanvas();
    
    void nodeDoubleClicked(NodeWidget* node);
    void setSystemChannelModes(bool inputStereo, bool outputStereo);
    
    // Called by PlusButtonWidget when clicked
    void onPlusButtonClicked(int row, int col, QPoint screenPos);
    
    std::shared_ptr<AudioNode> getPluginAt(int row, int col) const;
    std::pair<int,int> findNode(const std::shared_ptr<AudioNode>& node) const;

    int getSplitCol(int row) const { return (row == 0 || row == 2) ? m_rows[row].splitCol : -1; }
    void setSplitCol(int row, int col);
    int getMergeCol(int row) const { return (row == 0 || row == 2) ? m_rows[row].mergeCol : -1; }
    void setMergeCol(int row, int col);
    float getMix(int row) const { return (row == 0 || row == 2) ? m_rows[row].mix : 1.0f; }
    void setMix(int row, float val);
    float getPan(int row) const { return (row == 0 || row == 2) ? m_rows[row].pan : 0.0f; }
    void setPan(int row, float val);
    bool isBranchEnabled(int row) const { return (row == 0 || row == 2) && m_rows[row].enabled; }
    void setBranchEnabled(int row, bool enabled);
    int getBranchOutputChannels(int row) const;
    int getBranchDestinationChannels(int row) const;
    void selectBranch(int row) { if (row == 0 || row == 2) emit branchSelected(row); }
    
    static constexpr int NUM_ROWS = 3;
    // Rows are signal chains, not visual wrap rows. Keep enough stages for a
    // practical pedalboard and let the canvas scroll when a chain is wider.
    static constexpr int NUM_COLS = 12;

signals:
    void nodeSelected(std::shared_ptr<AudioNode> node);
    void editPluginUI(std::shared_ptr<AudioNode> node);
    void plusButtonClicked(int row, int col, QPoint screenPos);
    void nodeContextMenuRequested(int row, int col, QPoint screenPos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void clearSceneItems();
    void rebuildAudioConnections();
    void updateBranchGains(int row);
    void layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY);
    void reflowLayoutWithDragGap();
    void reflowRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW);
    PlusButtonWidget* findPlusButton(int row, int col) const;

    AudioEngine* m_engine;
    QGraphicsScene* m_scene;

    GridRow m_rows[NUM_ROWS];
    struct BranchGainControls {
        std::shared_ptr<std::atomic<float>> left = std::make_shared<std::atomic<float>>(1.0f);
        std::shared_ptr<std::atomic<float>> right = std::make_shared<std::atomic<float>>(1.0f);
        std::shared_ptr<std::atomic<float>> mono = std::make_shared<std::atomic<float>>(1.0f);
        std::shared_ptr<std::atomic<float>> monoHalf = std::make_shared<std::atomic<float>>(0.5f);
    };
    BranchGainControls m_branchGains[NUM_ROWS];

    NodeWidget* m_sysInputWidget  = nullptr;
    NodeWidget* m_sysOutputWidget = nullptr;

    NodeWidget* m_nodeWidgets[NUM_ROWS][NUM_COLS] = {};
    
    std::vector<QGraphicsItem*> m_dynamicItems;

    int m_dragGapRow = -1;
    int m_dragGapCol = -1;
    QGraphicsRectItem* m_dragPlaceholderItem = nullptr;

    QTimer* m_animationTimer = nullptr;
    std::unordered_map<QGraphicsItem*, QPointF> m_targetPositions;

    void setItemTargetPos(QGraphicsItem* item, QPointF targetPos, bool animate = true);

private slots:
    void tickAnimations();
};
