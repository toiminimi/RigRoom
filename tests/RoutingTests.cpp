#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <algorithm>
#include <string>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>
#include "audio/AudioEngine.h"
#include "ui/NodeCanvas.h"
#include "ui/CanvasMetrics.h"

class TestNode final : public AudioNode {
public:
    explicit TestNode(std::string id, int inputs = 2, int outputs = 2) {
        uniqueId = std::move(id);
        m_ports.resize(inputs + outputs);
        for (int channel = 0; channel < inputs; ++channel) {
            m_ports[channel] = {"In", true, inputs >= 2, channel, nullptr};
        }
        for (int channel = 0; channel < outputs; ++channel) {
            m_ports[inputs + channel] = {"Out", false, outputs >= 2, channel, nullptr};
        }
    }

    std::string getName() const override { return uniqueId; }
    NodeType getType() const override { return NodeType::VST3Plugin; }
    void prepare(double, int) override {}
    void process(int) override {}
};

static float fixedGain(const AudioEngine& engine, const std::string& source, const std::string& destination) {
    for (const AudioConnection& connection : engine.getConnections()) {
        if (connection.srcNodeId == source && connection.dstNodeId == destination && !connection.liveGain) {
            return connection.gain;
        }
    }
    assert(false && "Expected fixed audio connection");
    return 0.0f;
}

static std::vector<AudioConnection> liveConnections(const AudioEngine& engine,
                                                    const std::string& source,
                                                    const std::string& destination) {
    std::vector<AudioConnection> result;
    for (const AudioConnection& connection : engine.getConnections()) {
        if (connection.srcNodeId == source && connection.dstNodeId == destination && connection.liveGain) {
            result.push_back(connection);
        }
    }
    return result;
}


// Which blocks feed which: the audio topology, independent of grid columns.
static std::vector<std::pair<std::string, std::string>> topology(const AudioEngine& engine) {
    std::vector<std::pair<std::string, std::string>> edges;
    for (const AudioConnection& c : engine.getConnections()) edges.emplace_back(c.srcNodeId, c.dstNodeId);
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

struct InsertFixture {
    AudioEngine engine;
    std::unique_ptr<NodeCanvas> canvas;
    InsertFixture() {
        auto input = std::make_shared<SystemAudioNode>("System Input", true, 2);
        input->uniqueId = "system_input";
        auto output = std::make_shared<SystemAudioNode>("System Output", false, 2);
        output->uniqueId = "system_output";
        engine.addNode(input);
        engine.addNode(output);
        canvas = std::make_unique<NodeCanvas>(&engine);
    }
    void put(int row, int col, const std::string& id) {
        canvas->insertPluginAt(row, col, std::make_shared<TestNode>(id));
    }
    std::string at(int row, int col) const {
        auto node = canvas->getPluginAt(row, col);
        return node ? node->uniqueId : std::string();
    }
};

static void testInsertColumn() {
    constexpr int M = NodeCanvas::MAIN_ROW;
    {
        // Main A B C D; branch (row 1) splits after A, merges before D: X Y.
        InsertFixture f;
        f.put(M, 0, "A"); f.put(M, 1, "B"); f.put(M, 2, "C"); f.put(M, 3, "D");
        f.canvas->setSplitSectionPresent(1, true);
        f.canvas->setSplitCol(1, 0);
        f.canvas->setMergeCol(1, 3);
        f.put(1, 1, "X"); f.put(1, 2, "Y");
        const auto before = topology(f.engine);
        assert(f.canvas->getNumCols() == 6);

        // Main lane between B and C: blocks slide right into the free column 4,
        // the branch stretches around the new column, audio is unchanged.
        assert(f.canvas->insertColumn(M, 2) == 2);
        f.canvas->applyRoutingChange(true);
        assert(f.at(M, 1) == "B" && f.at(M, 2).empty() && f.at(M, 3) == "C" && f.at(M, 4) == "D");
        assert(f.at(1, 1) == "X" && f.at(1, 3) == "Y");
        assert(f.canvas->getSplitCol(1) == 0 && f.canvas->getMergeCol(1) == 4);
        assert(topology(f.engine) == before);

        // Inside the branch before Y: the branch has a free slot of its own
        // (column 2), so only the branch lane is used and nothing else moves.
        assert(f.canvas->insertColumn(1, 3) == 2);
        f.canvas->applyRoutingChange(true);
        assert(f.at(1, 1) == "X" && f.at(1, 3) == "Y" && f.at(M, 3) == "C" && f.at(M, 4) == "D");
        assert(f.canvas->getMergeCol(1) == 4);
        assert(topology(f.engine) == before);

        // At the branch's own split boundary: the new column belongs to the branch.
        assert(f.canvas->insertColumn(1, 1) == 1);
        f.canvas->applyRoutingChange(true);
        assert(f.canvas->getSplitCol(1) == 0 && f.at(1, 2) == "X");
        assert(topology(f.engine) == before);

        // Main lane at that split: the new column goes before the split.
        assert(f.canvas->insertColumn(M, 1) == 1);
        f.canvas->applyRoutingChange(true);
        assert(f.canvas->getSplitCol(1) == 1 && f.at(M, 0) == "A" && f.at(M, 1).empty());
        assert(topology(f.engine) == before);

        // Main lane at the merge, nothing free on the right: blocks before it
        // slide left into a free column and the new column lands after the merge.
        const int merge = f.canvas->getMergeCol(1);
        assert(f.canvas->insertColumn(M, merge) == merge - 1);
        f.canvas->applyRoutingChange(true);
        assert(f.canvas->getMergeCol(1) == merge - 1);
        assert(f.at(M, merge - 1).empty() && f.at(M, merge) == "D");
        assert(topology(f.engine) == before);
        assert(f.canvas->getNumCols() == 6); // never grows
    }
    {
        // Nested: row 0 inside row 1. Adding at the end of row 0 widens both.
        InsertFixture f;
        f.put(M, 0, "A"); f.put(M, 5, "D");
        f.canvas->setSplitSectionPresent(1, true);
        f.canvas->setSplitCol(1, 0);  // gap 1
        f.canvas->setMergeCol(1, 4);  // gap 4
        f.put(1, 1, "X"); f.put(1, 2, "Y");
        assert(f.canvas->createSplitAtPathGap(1, 3));
        f.canvas->setSplitCol(0, 2);  // gap 3
        f.canvas->setMergeCol(0, 4);  // gap 4, shared with row 1
        f.put(0, 3, "Z");
        const auto before = topology(f.engine);
        const int cols = f.canvas->getNumCols();
        assert(f.canvas->insertColumn(0, 4) == 4);
        f.canvas->applyRoutingChange(true);
        assert(f.canvas->getMergeCol(0) == 5 && f.canvas->getMergeCol(1) == 5);
        assert(f.at(0, 3) == "Z" && f.at(M, 5) == "D");
        assert(f.canvas->getNumCols() == cols);
        assert(topology(f.engine) == before);
    }
    {
        // Full width but with an empty column on the right: it is used.
        InsertFixture f;
        f.canvas->setNumCols(NodeCanvas::MAX_COLS);
        f.put(M, 0, "A");
        f.put(M, 1, "B");
        f.put(M, NodeCanvas::MAX_COLS - 1, "Z");
        const auto before = topology(f.engine);
        assert(f.canvas->insertColumn(M, 1) == 1);
        f.canvas->applyRoutingChange(true);
        assert(f.canvas->getNumCols() == NodeCanvas::MAX_COLS);
        assert(f.at(M, 0) == "A" && f.at(M, 1).empty() && f.at(M, 2) == "B");
        assert(f.at(M, NodeCanvas::MAX_COLS - 1) == "Z");
        assert(topology(f.engine) == before);
    }
    {
        // Nothing free on the right: earlier blocks slide left into the gap.
        InsertFixture f;
        f.put(M, 0, "A");
        for (int c = 2; c < 6; ++c) f.put(M, c, std::string(1, char('B' + c - 2)));  // B C D E at 2..5
        const auto before = topology(f.engine);
        assert(f.canvas->insertColumn(M, 3) == 2);
        f.canvas->applyRoutingChange(true);
        assert(f.at(M, 0) == "A" && f.at(M, 1) == "B" && f.at(M, 2).empty() && f.at(M, 3) == "C");
        assert(f.canvas->getNumCols() == 6);
        assert(topology(f.engine) == before);
    }
    {
        // Every column holds a block: nothing can be inserted.
        InsertFixture f;
        for (int c = 0; c < f.canvas->getNumCols(); ++c) f.put(M, c, "N" + std::to_string(c));
        assert(f.canvas->insertColumn(M, 3) == -1);
        assert(!f.canvas->insertPluginBefore(M, 3, std::make_shared<TestNode>("X")));
        assert(f.at(M, 3) == "N3");
    }
    {
        // The user's board: 11 columns, two paths split after the amp (gap 4)
        // and merge before the doubler (gap 8), free columns 4, 5 and 10.
        auto build = [](InsertFixture& f) {
            f.canvas->setNumCols(11);
            f.put(M, 0, "Tuner"); f.put(M, 1, "Gate"); f.put(M, 2, "EQ"); f.put(M, 3, "Amp");
            f.put(M, 7, "Lush"); f.put(M, 8, "Doubler"); f.put(M, 9, "Volume");
            f.canvas->setSplitSectionPresent(1, true);
            f.canvas->setSplitCol(1, 3); f.canvas->setMergeCol(1, 8);
            f.put(1, 6, "Chorus"); f.put(1, 7, "Delay");
            f.canvas->setSplitSectionPresent(3, true);
            f.canvas->setSplitCol(3, 3); f.canvas->setMergeCol(3, 8);
            f.put(3, 7, "Reverb");
        };
        {
            // Before the split, at the end of a path, after the merge: each new
            // block uses up one free column and the board never grows. With the
            // three free columns gone, the next insert is refused.
            InsertFixture f;
            build(f);
            auto node = [](const char* id) { return std::make_shared<TestNode>(id); };
            assert(f.canvas->insertPluginBefore(M, f.canvas->getSplitCol(1) + 1, node("Pre"), true));
            assert(f.canvas->insertPluginBefore(1, f.canvas->getMergeCol(1), node("End"), true));
            assert(f.canvas->insertPluginBefore(M, f.canvas->getMergeCol(1), node("Post"), true));
            assert(f.canvas->getNumCols() == 11);
            const auto edges = topology(f.engine);
            auto has = [&](const char* a, const char* b) {
                return std::find(edges.begin(), edges.end(), std::make_pair(std::string(a), std::string(b))) != edges.end();
            };
            assert(has("Amp", "Pre") && has("Pre", "Chorus") && has("Pre", "Reverb"));
            assert(has("Delay", "End") && has("Chorus", "Delay"));
            assert(has("Post", "Doubler") && !has("Lush", "Doubler") && !has("End", "Doubler"));
            assert(!f.canvas->insertPluginBefore(M, 1, node("Nope"), true));
            assert(f.canvas->getNumCols() == 11);
        }
        {
            // A real block at the end of path 1 joins that path only.
            InsertFixture f;
            build(f);
            const int end = f.canvas->getMergeCol(1);
            assert(f.canvas->insertPluginBefore(1, end, std::make_shared<TestNode>("New"), true));
            const auto after = topology(f.engine);
            assert(std::find(after.begin(), after.end(), std::make_pair(std::string("Delay"), std::string("New"))) != after.end());
            assert(std::find(after.begin(), after.end(), std::make_pair(std::string("Reverb"), std::string("New"))) == after.end());
        }
    }
    {
        // No column is empty in every lane, but the lanes have gaps of their own:
        // main  A _ B [split] C _ [merge] D     branch  X Y (cols 2..4, gap at 4... full)
        // Inserts before the split, after the split and after the merge slide
        // only that lane, within its own stretch, and keep the signal path.
        InsertFixture f;
        f.canvas->setNumCols(7);
        f.put(M, 0, "A"); f.put(M, 2, "B"); f.put(M, 3, "C"); f.put(M, 5, "D");
        f.canvas->setSplitSectionPresent(1, true);
        f.canvas->setSplitCol(1, 2);  // gap 3
        f.canvas->setMergeCol(1, 5);  // gap 5
        f.put(1, 3, "X"); f.put(1, 4, "Y");
        f.put(3, 1, "P"); f.put(3, 6, "Q"); // keep columns 1 and 6 from being empty everywhere
        f.canvas->setSplitSectionPresent(3, true);
        f.canvas->setSplitCol(3, 0);
        f.canvas->setMergeCol(3, -1);
        auto node = [](const char* id) { return std::make_shared<TestNode>(id); };
        auto edges = [&]() { return topology(f.engine); };
        auto has = [&](const char* a, const char* b) {
            const auto e = edges();
            return std::find(e.begin(), e.end(), std::make_pair(std::string(a), std::string(b))) != e.end();
        };
        // Before the split: B slides left into column 1.
        assert(f.canvas->insertPluginBefore(M, 3, node("Pre"), NodeCanvas::InsertBefore));
        assert(f.at(M, 1) == "B" && f.at(M, 2) == "Pre" && f.canvas->getSplitCol(1) == 2);
        assert(has("B", "Pre") && has("Pre", "X") && has("Pre", "C"));
        // After the split on the main path: C slides right into column 4.
        assert(f.canvas->insertPluginBefore(M, 3, node("Post"), NodeCanvas::InsertAfter));
        assert(f.at(M, 3) == "Post" && f.at(M, 4) == "C");
        assert(has("Pre", "Post") && has("Post", "C") && has("Pre", "X"));
        // Main lane between the split and merge is now full; the branch is full too,
        // so after the merge D slides right into column 6.
        assert(f.canvas->insertPluginBefore(M, 5, node("Tail"), NodeCanvas::InsertAfter));
        assert(f.at(M, 5) == "Tail" && f.at(M, 6) == "D" && has("Tail", "D") && has("Y", "Tail"));
        assert(f.canvas->getNumCols() == 7);
        // Dragging Pre from right before the split to right after it crosses
        // the junction: it leaves the shared path and joins the main lane only.
        assert(f.canvas->movePluginToGap(M, 2, M, 3, NodeCanvas::InsertAfter));
        assert(has("B", "X") && has("Pre", "Post") && !has("Pre", "X"));
        // And back before the split.
        const int preCol = [&] { for (int c = 0; c < 7; ++c) if (f.at(M, c) == "Pre") return c; return -1; }();
        assert(preCol == f.canvas->getSplitCol(1) + 1); // first block after the split
        assert(f.canvas->movePluginToGap(M, preCol, M, preCol, NodeCanvas::InsertBefore));
        assert(has("Pre", "X") && has("Pre", "Post"));
    }
    {
        // A parallel path that merges back at the very end of the board: a block
        // can still go after the merge, right before the output.
        InsertFixture f;
        f.canvas->setNumCols(6);
        f.put(M, 0, "A"); f.put(M, 4, "B");
        f.canvas->setSplitSectionPresent(1, true);
        f.canvas->setSplitCol(1, 0);
        f.canvas->setMergeCol(1, -1);   // merges at the end of the board
        f.put(1, 2, "X");
        assert(f.canvas->getMergeCol(1) == -1);
        auto node = [](const char* id) { return std::make_shared<TestNode>(id); };
        assert(f.canvas->insertPluginBefore(M, 6, node("Last"), NodeCanvas::InsertAfter));
        const auto edges = topology(f.engine);
        auto has = [&](const char* a2, const char* b2) {
            return std::find(edges.begin(), edges.end(), std::make_pair(std::string(a2), std::string(b2))) != edges.end();
        };
        assert(has("B", "Last") && has("X", "Last") && has("Last", "system_output"));
        assert(f.canvas->getMergeCol(1) >= 0); // the merge moved in front of it
        assert(f.canvas->getNumCols() == 6);
    }
    {
        // Occupied slot: insertPluginBefore pushes later blocks right.
        InsertFixture f;
        f.put(M, 0, "A"); f.put(M, 1, "B");
        assert(f.canvas->insertPluginBefore(M, 1, std::make_shared<TestNode>("N")));
        assert(f.at(M, 0) == "A" && f.at(M, 1) == "N" && f.at(M, 2) == "B");

        // Dragging D between A and N: D lands there, its old slot stays empty.
        f.put(M, 4, "D");
        assert(f.canvas->movePluginToGap(M, 4, M, 1, true));
        assert(f.at(M, 0) == "A" && f.at(M, 1) == "D" && f.at(M, 2) == "N" && f.at(M, 3) == "B");
        assert(f.at(M, 4).empty() && f.at(M, 5).empty());
        // Appending after a block in the last column slides it left instead of growing.
        const int cols = f.canvas->getNumCols();
        f.put(M, cols - 1, "LAST");
        assert(f.canvas->insertPluginBefore(M, cols, std::make_shared<TestNode>("END"), true));
        assert(f.canvas->getNumCols() == cols && f.at(M, cols - 1) == "END" && f.at(M, cols - 2) == "LAST");
        // Next to itself is a no-op.
        assert(!f.canvas->movePluginToGap(M, 1, M, 2, true));
        assert(f.at(M, 1) == "D");
    }
}

// Hovering an insert marker previews where splits and merges will move, nested
// paths included. The preview animates, so give it a moment to settle.
static void settle(int ms = 400) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void testInsertPreviewMovesRoutes() {
    constexpr int M = NodeCanvas::MAIN_ROW;
    InsertFixture f;
    f.canvas->resize(1400, 600);
    f.put(M, 0, "A"); f.put(M, 5, "D");
    f.canvas->setSplitSectionPresent(1, true);
    f.canvas->setSplitCol(1, 0);   // gap 1
    f.canvas->setMergeCol(1, 4);   // gap 4
    f.put(1, 1, "X"); f.put(1, 2, "Y");
    assert(f.canvas->createSplitAtPathGap(1, 3));
    f.canvas->setSplitCol(0, 2);   // gap 3, the nested path
    f.canvas->setMergeCol(0, 4);
    f.put(0, 3, "Z");
    f.canvas->applyRoutingChange(true);
    settle(50);

    const qreal split1 = f.canvas->shownSplitX(1), merge1 = f.canvas->shownMergeX(1);
    const qreal split0 = f.canvas->shownSplitX(0), merge0 = f.canvas->shownMergeX(0);
    assert(split1 > 0.0 && split0 > 0.0);

    // Inserting on the main lane before the split pushes both paths right.
    f.canvas->setDragGap(M, 1, NodeCanvas::InsertBefore);
    settle();
    assert(f.canvas->shownSplitX(1) > split1);
    assert(f.canvas->shownSplitX(0) > split0);
    assert(f.canvas->shownMergeX(0) > merge0);
    assert(f.canvas->shownMergeX(1) > merge1);

    // Moving off the marker puts them back.
    f.canvas->clearDragGap();
    settle();
    assert(std::abs(f.canvas->shownSplitX(0) - split0) < 0.5);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Fixed-grid geometry contract: six columns fit a standard viewport, gaps
    // remain ordered, and all five lanes fit a compact laptop-height canvas.
    assert(CanvasMetrics::requiredWidth(6) <= 1200.0);
    assert(CanvasMetrics::requiredWidth(12) > 1200.0);
    const qreal trackLeft = CanvasMetrics::marginX + CanvasMetrics::cardWidth + CanvasMetrics::clearGap;
    for (int gap = 0; gap < 6; ++gap)
        assert(CanvasMetrics::gapX(trackLeft, gap, 6) < CanvasMetrics::gapX(trackLeft, gap + 1, 6));
    assert(CanvasMetrics::requiredHeight(5) <= 490.0);
    assert(CanvasMetrics::requiredHeight(5, CanvasMetrics::cardHeight,
                                         CanvasMetrics::minimumLaneHeight) <= 442.0);
    const qreal minimumJunctionClearance = CanvasMetrics::minimumLaneHeight / 2.0
        - CanvasMetrics::cardHeight / 2.0 - CanvasMetrics::junctionVisualHeight / 2.0;
    assert(minimumJunctionClearance >= 6.0);
    AudioEngine engine;
    // Preset loudness is intentionally separate from the global hardware output gain.
    engine.setOutputGain(-6.0f);
    engine.setPresetOutputLevel(3.0f);
    assert(std::abs(engine.getOutputGainDB() + 6.0f) < 0.01f);
    assert(std::abs(engine.getPresetOutputLevelDB() - 3.0f) < 0.01f);
    engine.setPresetOutputLevel(99.0f);
    assert(std::abs(engine.getPresetOutputLevelDB() - 12.0f) < 0.01f);
    engine.setPresetOutputLevel(-99.0f);
    assert(std::abs(engine.getPresetOutputLevelDB() + 24.0f) < 0.01f);
    assert(std::abs(engine.getOutputGainDB() + 6.0f) < 0.01f);
    engine.setPresetOutputLevel(0.0f);

    auto input = std::make_shared<SystemAudioNode>("System Input", true, 2);
    input->uniqueId = "system_input";
    auto output = std::make_shared<SystemAudioNode>("System Output", false, 2);
    output->uniqueId = "system_output";
    engine.addNode(input);
    engine.addNode(output);

    NodeCanvas canvas(&engine);
    canvas.insertPluginAt(NodeCanvas::MAIN_ROW, 0, std::make_shared<TestNode>("main-a"));
    canvas.insertPluginAt(NodeCanvas::MAIN_ROW, 3, std::make_shared<TestNode>("main-b"));
    const auto originNode = canvas.getPluginAt(NodeCanvas::MAIN_ROW, 0);
    canvas.movePluginToGap(NodeCanvas::MAIN_ROW, 0, NodeCanvas::MAIN_ROW, 0);
    assert(canvas.getPluginAt(NodeCanvas::MAIN_ROW, 0) == originNode);

    assert(canvas.createSplitAtMainGap(1));
    assert(canvas.createSplitAtMainGap(2));
    assert(canvas.getSplitParentRow(1) == NodeCanvas::MAIN_ROW);
    assert(canvas.getSplitParentRow(3) == NodeCanvas::MAIN_ROW);
    assert(!canvas.isSharedSplitJunction(1));
    assert(!canvas.isSharedSplitJunction(3));

    canvas.setSplitMode(1, GridRow::SplitMode::AB);
    canvas.setSplitMode(3, GridRow::SplitMode::AB);
    assert(canvas.getSplitMode(1) == GridRow::SplitMode::AB);
    assert(canvas.getSplitMode(3) == GridRow::SplitMode::AB);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 0.5f) < 0.0001f);

    canvas.setSplitCol(3, canvas.getSplitCol(1));
    assert(canvas.isSharedSplitJunction(1));
    assert(canvas.isSharedSplitJunction(3));
    assert(canvas.getSplitMode(1) == GridRow::SplitMode::Copy);
    assert(canvas.getSplitMode(3) == GridRow::SplitMode::Copy);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 1.0f) < 0.0001f);

    canvas.setBranchEnabled(3, false);
    assert(!canvas.isSharedSplitJunction(1));
    assert(!canvas.isSharedSplitJunction(3));
    assert(canvas.isSharedMixerJunction(1));
    assert(canvas.isSharedMixerJunction(3));
    assert(canvas.getMixerGroupRows(3) == std::vector<int>({1, 3}));
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 1.0f) < 0.0001f);
    canvas.setBranchEnabled(3, true);
    assert(canvas.isSharedSplitJunction(1));
    assert(canvas.isSharedSplitJunction(3));

    canvas.setSplitCol(3, 1);
    canvas.setMergeCol(1, 3);
    canvas.setMergeCol(3, 3);
    assert(canvas.isSharedMixerJunction(1));
    assert(canvas.isSharedMixerJunction(3));
    assert(canvas.getMixerGroupRows(1).size() == 2);
    assert(liveConnections(engine, "main-a", "main-b").size() == 4);
    canvas.setBranchEnabled(3, false);
    assert(canvas.isSharedMixerJunction(1));
    assert(canvas.isSharedMixerJunction(3));
    assert(canvas.getMixerGroupRows(1) == std::vector<int>({1, 3}));
    assert(canvas.getMixerGroupRows(3) == std::vector<int>({1, 3}));
    assert(liveConnections(engine, "main-a", "main-b").size() == 2);
    canvas.setBranchEnabled(3, true);
    assert(liveConnections(engine, "main-a", "main-b").size() == 4);
    canvas.setMainMix(1, 0.5f);
    assert(canvas.getMainMix(1) == 0.5f);
    assert(canvas.getMainMix(3) == 0.5f);
    canvas.setMix(1, 0.25f);
    canvas.setMix(3, 1.5f);
    assert(canvas.getMix(1) == 0.25f);
    assert(canvas.getMix(3) == 1.5f);

    canvas.setMergeCol(3, 4);
    assert(!canvas.isSharedMixerJunction(1));
    assert(!canvas.isSharedMixerJunction(3));
    canvas.setMainMix(3, 1.5f);
    assert(canvas.getMainMix(1) == 0.5f);
    assert(canvas.getMainMix(3) == 1.5f);

    canvas.setSplitMode(3, GridRow::SplitMode::AB);
    canvas.setSplitPosition(3, 0.0f);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 0.5f * std::sqrt(0.5f)) < 0.0001f);
    canvas.setBranchEnabled(3, false);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 0.5f) < 0.0001f);
    canvas.setBranchEnabled(3, true);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 0.5f * std::sqrt(0.5f)) < 0.0001f);

    assert(canvas.createSplitAtPathGap(1, 1));
    assert(canvas.getSplitParentRow(0) == 1);
    canvas.setSplitParentRow(0, 3);
    assert(canvas.getSplitParentRow(0) == 1);

    canvas.removeSplitSection(1);
    assert(!canvas.hasSplitSection(1));
    assert(!canvas.hasSplitSection(0));
    assert(canvas.hasSplitSection(3));

    AudioEngine monoReturnEngine;
    auto monoInput = std::make_shared<SystemAudioNode>("System Input", true, 2);
    monoInput->uniqueId = "system_input";
    auto monoOutput = std::make_shared<SystemAudioNode>("System Output", false, 2);
    monoOutput->uniqueId = "system_output";
    monoReturnEngine.addNode(monoInput);
    monoReturnEngine.addNode(monoOutput);

    NodeCanvas monoReturnCanvas(&monoReturnEngine);
    monoReturnCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 0,
                                    std::make_shared<TestNode>("stereo-source", 2, 2));
    monoReturnCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 3,
                                    std::make_shared<TestNode>("mono-destination", 1, 1));
    assert(monoReturnCanvas.createSplitAtMainGap(1));
    monoReturnCanvas.setMergeCol(1, 3);
    assert(monoReturnCanvas.getBranchStereoCollapseReason(1) == "mono-destination");

    auto downmix = liveConnections(monoReturnEngine, "stereo-source", "mono-destination");
    assert(downmix.size() == 2);
    for (const AudioConnection& connection : downmix) {
        assert(std::abs(connection.gain - 0.5f) < 0.0001f);
        assert(std::abs(connection.liveGain->load(std::memory_order_relaxed) - 1.0f) < 0.0001f);
    }

    monoReturnCanvas.setPan(1, 1.0f);
    downmix = liveConnections(monoReturnEngine, "stereo-source", "mono-destination");
    for (const AudioConnection& connection : downmix) {
        const float gain = connection.liveGain->load(std::memory_order_relaxed);
        assert(std::abs(gain - (connection.srcPortIdx == 0 ? 0.0f : 1.0f)) < 0.0001f);
    }

    monoReturnCanvas.setPan(1, -1.0f);
    downmix = liveConnections(monoReturnEngine, "stereo-source", "mono-destination");
    for (const AudioConnection& connection : downmix) {
        const float gain = connection.liveGain->load(std::memory_order_relaxed);
        assert(std::abs(gain - (connection.srcPortIdx == 0 ? 1.0f : 0.0f)) < 0.0001f);
    }

    AudioEngine monoPanEngine;
    auto panInput = std::make_shared<SystemAudioNode>("System Input", true, 2);
    panInput->uniqueId = "system_input";
    auto panOutput = std::make_shared<SystemAudioNode>("System Output", false, 2);
    panOutput->uniqueId = "system_output";
    monoPanEngine.addNode(panInput);
    monoPanEngine.addNode(panOutput);

    NodeCanvas monoPanCanvas(&monoPanEngine);
    monoPanCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 0,
                                 std::make_shared<TestNode>("mono-source", 1, 1));
    monoPanCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 3,
                                 std::make_shared<TestNode>("stereo-destination", 2, 2));
    assert(monoPanCanvas.createSplitAtMainGap(1));
    monoPanCanvas.setMergeCol(1, 3);
    assert(monoPanCanvas.getBranchStereoCollapseReason(1).isEmpty());
    monoPanCanvas.setPan(1, 1.0f);
    const auto panConnections = liveConnections(monoPanEngine, "mono-source", "stereo-destination");
    assert(panConnections.size() == 2);
    for (const AudioConnection& connection : panConnections) {
        const float gain = connection.liveGain->load(std::memory_order_relaxed);
        assert(std::abs(gain - (connection.dstPortIdx == 0 ? 0.0f : 1.0f)) < 0.0001f);
    }

    AudioEngine monoOnlyEngine;
    auto onlyInput = std::make_shared<SystemAudioNode>("System Input", true, 2);
    onlyInput->uniqueId = "system_input";
    auto onlyOutput = std::make_shared<SystemAudioNode>("System Output", false, 2);
    onlyOutput->uniqueId = "system_output";
    monoOnlyEngine.addNode(onlyInput);
    monoOnlyEngine.addNode(onlyOutput);

    NodeCanvas monoOnlyCanvas(&monoOnlyEngine);
    monoOnlyCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 0,
                                  std::make_shared<TestNode>("mono-only-source", 1, 1));
    monoOnlyCanvas.insertPluginAt(NodeCanvas::MAIN_ROW, 3,
                                  std::make_shared<TestNode>("mono-only-destination", 1, 1));
    assert(monoOnlyCanvas.createSplitAtMainGap(1));
    monoOnlyCanvas.setMergeCol(1, 3);
    monoOnlyCanvas.setPan(1, 1.0f);
    const auto monoOnlyConnections = liveConnections(
        monoOnlyEngine, "mono-only-source", "mono-only-destination");
    assert(monoOnlyConnections.size() == 1);
    assert(std::abs(monoOnlyConnections.front().liveGain->load(std::memory_order_relaxed) - 1.0f) < 0.0001f);
    testInsertColumn();
    testInsertPreviewMovesRoutes();
    return 0;
}
