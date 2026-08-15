#include <QApplication>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>
#include "audio/AudioEngine.h"
#include "ui/NodeCanvas.h"
#include "ui/CanvasMetrics.h"

class TestNode final : public AudioNode {
public:
    explicit TestNode(std::string id) {
        uniqueId = std::move(id);
        m_ports.resize(4);
        for (int channel = 0; channel < 2; ++channel) {
            m_ports[channel] = {"In", true, true, channel, nullptr};
            m_ports[channel + 2] = {"Out", false, true, channel, nullptr};
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

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Pure fixed-grid geometry contract: six columns fit a standard viewport,
    // gaps remain ordered, and five lanes include their complete vertical span.
    assert(CanvasMetrics::requiredWidth(6) <= 1200.0);
    assert(CanvasMetrics::requiredWidth(12) > 1200.0);
    const qreal trackLeft = CanvasMetrics::marginX + CanvasMetrics::cardWidth + CanvasMetrics::clearGap;
    for (int gap = 0; gap < 6; ++gap)
        assert(CanvasMetrics::gapX(trackLeft, gap, 6) < CanvasMetrics::gapX(trackLeft, gap + 1, 6));
    assert(CanvasMetrics::requiredHeight(5) >= CanvasMetrics::cardHeight + 4 * CanvasMetrics::laneHeight);
    AudioEngine engine;

    auto input = std::make_shared<SystemAudioNode>("System Input", true, 2);
    input->uniqueId = "system_input";
    auto output = std::make_shared<SystemAudioNode>("System Output", false, 2);
    output->uniqueId = "system_output";
    engine.addNode(input);
    engine.addNode(output);

    NodeCanvas canvas(&engine);
    canvas.insertPluginAt(NodeCanvas::MAIN_ROW, 0, std::make_shared<TestNode>("main-a"));
    canvas.insertPluginAt(NodeCanvas::MAIN_ROW, 3, std::make_shared<TestNode>("main-b"));

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
    assert(canvas.getMixerGroupRows(3) == std::vector<int>{3});
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
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - std::sqrt(0.5f)) < 0.0001f);
    canvas.setBranchEnabled(3, false);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - 1.0f) < 0.0001f);
    canvas.setBranchEnabled(3, true);
    assert(std::abs(fixedGain(engine, "main-a", "main-b") - std::sqrt(0.5f)) < 0.0001f);

    assert(canvas.createSplitAtPathGap(1, 1));
    assert(canvas.getSplitParentRow(0) == 1);
    canvas.setSplitParentRow(0, 3);
    assert(canvas.getSplitParentRow(0) == 1);

    canvas.removeSplitSection(1);
    assert(!canvas.hasSplitSection(1));
    assert(!canvas.hasSplitSection(0));
    assert(canvas.hasSplitSection(3));
    return 0;
}
