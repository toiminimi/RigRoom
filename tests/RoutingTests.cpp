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
    return 0;
}
