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
    void routingNodeSelected(int row, bool isSplit);
public:
    explicit NodeCanvas(AudioEngine* engine, QWidget* parent = nullptr);
    ~NodeCanvas() override;
    
    // Insert / remove plugin at grid position
    void insertPluginAt(int row, int col, std::shared_ptr<AudioNode> audioNode);
    // Insert before the node currently at col (shifts others right)
    void insertPluginBefore(int row, int col, std::shared_ptr<AudioNode> audioNode, bool isSecondOfCol = false);
    void removePluginAt(int row, int col);
    void replacePluginAt(int row, int col, std::shared_ptr<AudioNode> newNode);
    // Move a plugin from one slot to another (reorder)
    void movePlugin(int fromRow, int fromCol, int toRow, int toCol);
    void movePluginToGap(int fromRow, int fromCol, int toRow, int toColGap, bool isSecondOfCol = false);

    void setDragGap(int row, int plusIdx, bool isSecondOfCol = false);
    void clearDragGap();
    int getDragGapRow() const { return m_dragGapRow; }
    int getDragGapCol() const { return m_dragGapCol; }
    bool getDragGapIsSecondOfCol() const { return m_dragGapIsSecondOfCol; }
    
    // Full layout recalculation
    void updateLayout();
    void clearCanvas();
    
    void nodeDoubleClicked(NodeWidget* node);
    void setSystemChannelModes(bool inputStereo, bool outputStereo);
    
    void onPlusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol = false);
    
    std::shared_ptr<AudioNode> getPluginAt(int row, int col) const;
    std::pair<int,int> findNode(const std::shared_ptr<AudioNode>& node) const;

    int getSplitCol(int row) const;
    void setSplitCol(int row, int col);
    int getMergeCol(int row) const;
    void setMergeCol(int row, int col);
    std::string getSplitAnchor(int row) const;
    std::string getMergeAnchor(int row) const;
    void setSplitAnchor(int row, const std::string& nodeId);
    void setMergeAnchor(int row, const std::string& nodeId);
    bool createSplitAtMainGap(int gapIndex);
    bool createSplitAtPathGap(int parentRow, int gapIndex);
    void removeSplitSection(int row);
    void applyRoutingChange(bool rebuildAudio = true);
    bool hasSplitSection(int row) const;
    void setSplitSectionPresent(int row, bool present);
    int getSplitParentRow(int row) const;
    void setSplitParentRow(int row, int parentRow);
    GridRow::SplitMode getSplitMode(int row) const;
    void setSplitMode(int row, GridRow::SplitMode mode);
    float getSplitPosition(int row) const;
    void setSplitPosition(int row, float position);
    bool isMainInputEnabled(int row) const;
    void setMainInputEnabled(int row, bool enabled);
    float getMainMix(int row) const;
    void setMainMix(int row, float level);
    float getMix(int row) const { return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].mix : 1.0f; }
    void setMix(int row, float val);
    float getPan(int row) const { return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].pan : 0.0f; }
    void setPan(int row, float val);
    bool isBranchEnabled(int row) const { return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) && m_rows[row].enabled; }
    void setBranchEnabled(int row, bool enabled);
    bool isPolarityInverted(int row) const;
    void setPolarityInverted(int row, bool inverted);
    QString getBranchName(int row) const;
    void setBranchName(int row, const QString& name);
    bool isMainOutputEnabled() const { return m_mainOutputEnabled; }
    void setMainOutputEnabled(bool enabled);
    int getBranchOutputChannels(int row) const;
    int getBranchDestinationChannels(int row) const;
    void beginRoutingUpdate();
    void endRoutingUpdate();
    void selectBranch(int row) { emit branchSelected(row); }
    void selectRoutingNode(int row, bool isSplit) { emit routingNodeSelected(row, isSplit); }
    
    static constexpr int NUM_ROWS = 5;
    static constexpr int MAIN_ROW = 2;
    // Rows are signal chains, not visual wrap rows. Keep enough stages for a
    // practical pedalboard and let the canvas scroll when a chain is wider.
    static constexpr int NUM_COLS = 12;

signals:
    void nodeSelected(std::shared_ptr<AudioNode> node);
    void editPluginUI(std::shared_ptr<AudioNode> node);
    void plusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol);
    void nodeContextMenuRequested(int row, int col, QPoint screenPos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void clearSceneItems();
    void rebuildAudioConnections();
    void updateBranchGains(int row);
    int findNodeColumn(int row, const std::string& nodeId) const;
    void setBranchDefaults(int row, float level);
    float pathSplitGainAfter(int parentRow, const std::string& sourceNodeId) const;
    float pathMixerGainBefore(int parentRow, const std::string& destinationNodeId) const;
    float branchSplitGain(int row) const;
    void layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY);
    void reflowLayoutWithDragGap();
    void reflowRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW);
    PlusButtonWidget* findPlusButton(int row, int col) const;
    void calculateRowCenters(qreal rowCenters[NUM_ROWS], qreal H) const;

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
    bool m_dragGapIsSecondOfCol = false;
    QGraphicsRectItem* m_dragPlaceholderItem = nullptr;

    QTimer* m_animationTimer = nullptr;
    std::unordered_map<QGraphicsItem*, QPointF> m_targetPositions;
    int m_routingUpdateDepth = 0;
    bool m_routingUpdatePending = false;
    bool m_mainOutputEnabled = true;

    void setItemTargetPos(QGraphicsItem* item, QPointF targetPos, bool animate = true);

private slots:
    void tickAnimations();
};
