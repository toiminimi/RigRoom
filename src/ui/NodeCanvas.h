#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <QTimer>
#include "GridRow.h"
#include "../audio/AudioEngine.h"
#include "NodeWidget.h"
#include "PortWidget.h"

class PlusButtonWidget;
class QFrame;
class QLabel;
class QToolButton;
class QHBoxLayout;

class NodeCanvas : public QGraphicsView {
    Q_OBJECT
signals:
    void routingChanged();
    void routingNodeSelected(int row, bool isSplit);
    void canvasAboutToBeCleared();
    void nodeAboutToBeRemoved(AudioNode* node);
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

    // Blocks that scenes change (drawn with an "S" badge).
    void setSceneMarkedNodes(std::unordered_set<std::string> ids);
    bool isSceneMarked(const std::string& id) const { return m_sceneMarkedNodes.count(id) > 0; }
    
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
    qreal getRowCenterY(int row) const;
    qreal getGapX(int gapIdx) const;
    int getSplitParentRow(int row) const;
    void setSplitParentRow(int row, int parentRow);
    GridRow::SplitMode getSplitMode(int row) const;
    void setSplitMode(int row, GridRow::SplitMode mode);
    float getSplitPosition(int row) const;
    void setSplitPosition(int row, float position);
    bool isMainInputEnabled(int row) const;
    void setMainInputEnabled(int row, bool enabled);
    bool isSharedSplitJunction(int row) const;
    bool isSharedMixerJunction(int row) const;
    std::vector<int> getMixerGroupRows(int row) const;
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
    QString getBranchStereoCollapseReason(int row) const;
    void beginRoutingUpdate();
    void endRoutingUpdate();
    void selectRoutingNode(int row, bool isSplit) { emit routingNodeSelected(row, isSplit); }
    
    static constexpr int NUM_ROWS = 5;
    static constexpr int MAIN_ROW = 2;
    static constexpr int MIN_COLS = 6;
    static constexpr int MAX_COLS = 12;
    static constexpr int NUM_COLS = MAX_COLS;
    
    int getNumCols() const { return m_numCols; }
    int getHighestOccupiedCol() const;
    void setNumCols(int cols);
    void addCol();
    double getZoomLevel() const { return m_zoomLevel; }
    void setZoomLevel(double zoom);
    void resetZoom();
    // Appends a divider and `widget` to the zoom box in the lower-right corner.
    void addZoomOverlayWidget(QWidget* widget);
    // Read-only "what is loaded" label in the top-left corner (rich text).
    QLabel* infoOverlay();
    void setInfoOverlayText(const QString& html);
    void fitToCanvas();
    // Keep the whole signal path in view: refit on resize and edits.
    // Manual zooming turns it off.
    void setAutoFit(bool enabled);
    bool autoFit() const { return m_autoFit; }
    void zoomIn();
    void zoomOut();

signals:
    void zoomChanged(double zoom);
    void autoFitChanged(bool enabled);
    void nodeSelected(std::shared_ptr<AudioNode> node);
    void nodeBypassToggled(std::shared_ptr<AudioNode> node);
    void editPluginUI(std::shared_ptr<AudioNode> node);
    void plusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol);
    void nodeContextMenuRequested(int row, int col, QPoint screenPos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void clearSceneItems();
    void rebuildAudioConnections();
    void normalizeRoutingTopology();
    void refreshRoutingAnchors();
    void updateBranchGains(int row);
    int findNodeColumn(int row, const std::string& nodeId) const;
    int splitGroupSize(int parentRow, int splitCol) const;
    int splitGroupLeader(int parentRow, int splitCol) const;
    int mixerGroupLeader(int parentRow, int mergeCol) const;
    bool isCanonicalParent(int row, int parentRow) const;
    void setBranchDefaults(int row, float level);
    float pathSplitGainBetween(int parentRow, int sourceCol, int destinationCol, int ignoreBranchRow = -1) const;
    float pathMixerGainBetween(int parentRow, int sourceCol, int destinationCol) const;
    float branchSplitGain(int row) const;
    void layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY);
    void reflowLayoutWithDragGap();
    void reflowRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW);
    PlusButtonWidget* findPlusButton(int row, int col) const;
    int activeLaneSteps() const;
    qreal canvasHeightFor(qreal viewportHeight) const;
    void calculateRowCenters(qreal rowCenters[NUM_ROWS], qreal H) const;

    AudioEngine* m_engine;
    QGraphicsScene* m_scene;

    GridRow m_rows[NUM_ROWS];
    struct BranchGainControls {
        std::shared_ptr<std::atomic<float>> left = std::make_shared<std::atomic<float>>(1.0f);
        std::shared_ptr<std::atomic<float>> right = std::make_shared<std::atomic<float>>(1.0f);
        std::shared_ptr<std::atomic<float>> mono = std::make_shared<std::atomic<float>>(1.0f);
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
    std::unordered_set<std::string> m_sceneMarkedNodes;
    int m_routingUpdateDepth = 0;
    bool m_routingUpdatePending = false;
    bool m_mainOutputEnabled = true;
    int m_numCols = 6;
    double m_zoomLevel = 1.0;
    bool m_isPanning = false;
    QPoint m_panStartPos;

    QFrame* m_zoomOverlay = nullptr;
    QHBoxLayout* m_zoomOverlayLayout = nullptr;
    QLabel* m_infoOverlay = nullptr;
    QLabel* m_zoomOverlayLabel = nullptr;
    QToolButton* m_zoomOverlayMinusBtn = nullptr;
    QToolButton* m_zoomOverlayPlusBtn = nullptr;
    QToolButton* m_zoomOverlayResetBtn = nullptr;
    QToolButton* m_zoomOverlayFitBtn = nullptr;
    QToolButton* m_zoomOverlayAutoBtn = nullptr;
    QTimer* m_autoFitTimer = nullptr;
    class QVariantAnimation* m_zoomAnim = nullptr;
    void applyZoom(double zoom, bool relayout);
    void stopZoomAnimation();
    bool m_autoFit = false;
    bool m_fitting = false;
    void scheduleAutoFit();
    void setupZoomOverlay();
    void updateZoomOverlayPos();

    void setItemTargetPos(QGraphicsItem* item, QPointF targetPos, bool animate = true);

private slots:
    void tickAnimations();
};
