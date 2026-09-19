#include "preset/PresetLibrary.h"
#include "preset/SceneModel.h"
#include "audio/AudioNode.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void touch(const QString& path) {
    QFile f(path);
    assert(f.open(QFile::WriteOnly));
    f.write("{}");
}

void testLibraryLabelsAndMigration() {
    QTemporaryDir tmp;
    assert(tmp.isValid());
    const QString presets = tmp.filePath("presets");
    QDir().mkpath(presets);
    for (const char* name : {"drive", "80s", "Blues", "acoustic"}) touch(presets + "/" + name + ".json");

    PresetLibrary lib(presets, tmp.filePath("library.json"));
    assert(lib.load()); // first run migrates and saves
    assert(QFile::exists(tmp.filePath("library.json")));
    assert(lib.occupiedCount() == 4);
    // Alphabetical, case-insensitive: 80s, acoustic, Blues, drive
    assert(lib.nameAt(0) == "80s");
    assert(lib.nameAt(1) == "acoustic");
    assert(lib.nameAt(2) == "Blues");
    assert(lib.nameAt(3) == "drive");

    assert(lib.slotLabel(0) == "01A");
    assert(lib.slotLabel(3) == "01D");
    assert(lib.slotLabel(4) == "02A");
    assert(lib.slotLabel(127) == "32D");
    assert(lib.slotCount() == 128);

    // Reload keeps the arrangement.
    lib.swap(0, 9);
    lib.save();
    PresetLibrary again(presets, tmp.filePath("library.json"));
    assert(!again.load());
    assert(again.nameAt(9) == "80s");
    assert(!again.isOccupied(0));
}

void testLibraryNavigationAndReconcile() {
    QTemporaryDir tmp;
    const QString presets = tmp.filePath("presets");
    QDir().mkpath(presets);
    for (const char* name : {"a", "b", "c"}) touch(presets + "/" + name + ".json");
    PresetLibrary lib(presets, tmp.filePath("library.json"));
    lib.load();

    lib.move(2, 10);      // c -> 03C
    touch(presets + "/d.json");
    lib.reconcileWithDisk(); // d lands in the first free slot (2)
    lib.move(2, 9);       // d -> 03B
    assert(lib.nameAt(10) == "c" && !lib.isOccupied(2));
    lib.swap(0, 1);       // b, a
    assert(lib.nameAt(0) == "b" && lib.nameAt(1) == "a");

    assert(lib.nextOccupied(1, 1) == 9);
    assert(lib.nextOccupied(10, 1) == 0);   // wraps
    assert(lib.nextOccupied(0, -1) == 10);  // wraps backwards
    assert(lib.nextOccupied(-1, 1) == 0);   // from "Untitled"

    assert(lib.firstFreeSlot() == 2);
    assert(lib.stepBank(9, 1) == 13 && lib.stepBank(1, -1) == 125); // same letter, wraps

    // Deleted file drops out, new file fills first free slot.
    QFile::remove(presets + "/a.json");
    touch(presets + "/new.json");
    assert(lib.reconcileWithDisk());
    assert(lib.slotOfName("new") == 1);
    assert(lib.slotOfName("a") == -1);

    lib.renameFile("new.json", "renamed.json");
    assert(lib.nameAt(1) == "renamed");

    // Bank count never drops below the highest bank in use, and never moves a preset.
    assert(lib.minBanks() == 3); // "c" is at 03C
    lib.setNumBanks(2);
    assert(lib.numBanks() == 3 && lib.slotOfName("c") == 10);
    lib.setNumBanks(10);
    assert(lib.numBanks() == 10 && lib.slotOfName("c") == 10);
}

void testLegacyBankSizeKeepsPositions() {
    // Older builds allowed 8 presets per bank but kept flat slot numbers;
    // loading them with 4 per bank puts every preset back where it was.
    QTemporaryDir tmp;
    const QString presets = tmp.filePath("presets");
    QDir().mkpath(presets);
    touch(presets + "/lead.json");
    touch(presets + "/clean.json");
    {
        QFile f(tmp.filePath("library.json"));
        assert(f.open(QFile::WriteOnly));
        f.write(R"({"version":1,"slotsPerBank":8,"numBanks":32,
                    "slots":{"12":"lead.json","3":"clean.json"},"bankNames":{"3":"Floyd"}})");
    }
    PresetLibrary lib(presets, tmp.filePath("library.json"));
    lib.load();
    assert(lib.slotsPerBank() == 4);
    assert(lib.slotLabel(lib.slotOfName("lead")) == "04A");
    assert(lib.slotLabel(lib.slotOfName("clean")) == "01D");
    assert(lib.bankLabel(3) == "04 Floyd");
}

SceneModel::BoardState board(bool driveOff, bool delayOff, float gain) {
    SceneModel::BoardState s;
    s.bypass["drive"] = driveOff;
    s.bypass["delay"] = delayOff;
    s.params["drive"][0] = gain;
    s.params["drive"][1] = 0.5f;
    s.params["delay"][0] = 0.3f;
    return s;
}

void applyTo(SceneModel::BoardState& live, const SceneModel::Changes& c) {
    for (const auto& [id, b] : c.bypass) live.bypass[id] = b;
    for (const auto& [id, idx, v] : c.params) live.params[id][idx] = v;
}

void testScenesSwitchAndRecall() {
    SceneModel scenes;
    auto live = board(false, true, 0.2f);
    scenes.reset(live);
    assert(scenes.count() == 1 && scenes.activeIndex() == 0);

    scenes.assignParam("drive", 0, 0.2f);
    assert(scenes.isAssigned("drive", 0) && !scenes.isAssigned("drive", 1));

    // Scene 2: lead - delay on, more gain.
    const int lead = scenes.addScene(live);
    assert(lead == 1);
    auto c = scenes.switchTo(lead, live);
    assert(c.empty()); // copy of scene 1
    live.bypass["delay"] = false;
    live.params["drive"][0] = 0.9f;
    live.params["drive"][1] = 0.7f; // unassigned: global
    scenes.setActiveLevel(3.0f);

    // Back to scene 1: delay off, gain back, unassigned param untouched.
    c = scenes.switchTo(0, live);
    assert(scenes.activeIndex() == 0);
    assert(c.bypass.size() == 1 && c.bypass[0].first == "delay" && c.bypass[0].second == true);
    assert(c.params.size() == 1 && std::get<0>(c.params[0]) == "drive" && std::abs(std::get<2>(c.params[0]) - 0.2f) < 1e-6f);
    assert(c.levelDb == 0.0f);
    applyTo(live, c);
    assert(live.params["drive"][1] == 0.7f);

    // Edits in scene 1 are recalled after a round trip.
    live.bypass["drive"] = true;
    applyTo(live, scenes.switchTo(1, live));
    assert(live.bypass["delay"] == false && std::abs(live.params["drive"][0] - 0.9f) < 1e-6f);
    assert(live.bypass["drive"] == false);
    c = scenes.switchTo(0, live);
    assert(c.levelDb == 0.0f);
    applyTo(live, c);
    assert(live.bypass["drive"] == true);
    assert(std::abs(scenes.scene(1).levelDb - 3.0f) < 1e-6f);

    // Unassigning stops the param from changing per scene.
    scenes.unassignParam("drive", 0);
    live.params["drive"][0] = 0.4f;
    c = scenes.switchTo(1, live);
    assert(c.params.empty());
}

void testScenesJsonRoundTripAndPrune() {
    SceneModel scenes;
    auto live = board(false, true, 0.2f);
    scenes.reset(live);
    scenes.assignParam("drive", 0, 0.2f);
    scenes.addScene(live);
    scenes.renameScene(1, "Lead");
    scenes.setSceneColor(1, "#FF9800");
    scenes.setSceneLevel(1, 2.5f);
    applyTo(live, scenes.switchTo(1, live));
    live.params["drive"][0] = 0.8f;
    scenes.captureActive(live);

    const QJsonObject json = scenes.toJson();
    SceneModel loaded;
    assert(loaded.fromJson(json, live));
    assert(loaded.count() == 2 && loaded.activeIndex() == 1);
    assert(loaded.scene(1).name == "Lead" && loaded.scene(1).color == "#FF9800");
    assert(std::abs(loaded.scene(1).levelDb - 2.5f) < 1e-6f);
    assert(loaded.isAssigned("drive", 0));
    assert(std::abs(loaded.scene(1).params.at("drive").at(0) - 0.8f) < 1e-6f);
    assert(std::abs(loaded.scene(0).params.at("drive").at(0) - 0.2f) < 1e-6f);

    // A removed block disappears from every scene on load.
    auto without = live;
    without.bypass.erase("delay");
    without.params.erase("delay");
    SceneModel pruned;
    pruned.fromJson(json, without);
    assert(pruned.scene(0).bypass.count("delay") == 0);

    // Older presets have no scenes: one scene from the live board.
    SceneModel legacy;
    assert(!legacy.fromJson(QJsonObject(), live));
    assert(legacy.count() == 1 && legacy.scene(0).bypass.at("delay") == live.bypass.at("delay"));

    // Limits.
    SceneModel many;
    many.reset(live);
    for (int i = 1; i < SceneModel::kMaxScenes; ++i) assert(many.addScene(live) == i);
    assert(many.addScene(live) == -1);
    assert(many.removeScene(0));
    assert(many.count() == SceneModel::kMaxScenes - 1);
    SceneModel single;
    single.reset(live);
    assert(!single.removeScene(0));
}

void testBankNamesAndSceneSummaries() {
    QTemporaryDir tmp;
    const QString presets = tmp.filePath("presets");
    QDir().mkpath(presets);
    touch(presets + "/old.json");
    {
        QFile f(presets + "/multi.json");
        assert(f.open(QFile::WriteOnly));
        f.write(R"({"scenes":{"list":[{"name":"Clean"},{"name":"Lead"},{"name":"Solo"}]}})");
    }
    {
        QFile f(presets + "/single.json");
        assert(f.open(QFile::WriteOnly));
        f.write(R"({"scenes":{"list":[{"name":"Scene 1"}]}})");
    }
    PresetLibrary lib(presets, tmp.filePath("library.json"));
    lib.load();

    assert(lib.bankLabel(3) == "04");
    lib.setBankName(3, "  Floyd ");
    assert(lib.bankName(3) == "Floyd" && lib.bankLabel(3) == "04 Floyd");
    lib.setBankName(0, "Gig");
    lib.save();

    PresetLibrary again(presets, tmp.filePath("library.json"));
    again.load();
    assert(again.bankLabel(3) == "04 Floyd" && again.bankName(0) == "Gig");
    again.setBankName(0, "");
    assert(again.bankLabel(0) == "01");
    again.setNumBanks(2); // empty library: allowed; the name is kept for later
    assert(again.numBanks() >= 1);

    assert(lib.sceneNamesAt(lib.slotOfName("multi")) == QStringList({"Clean", "Lead", "Solo"}));
    assert(lib.sceneNamesAt(lib.slotOfName("single")).isEmpty());
    assert(lib.sceneNamesAt(lib.slotOfName("old")).isEmpty());
    assert(lib.sceneNamesAt(100).isEmpty());
}

void testSceneControlledBlocks() {
    SceneModel scenes;
    auto live = board(false, true, 0.2f);
    scenes.reset(live);
    assert(scenes.sceneControlledBlocks(live).empty());

    scenes.addScene(live);
    assert(scenes.sceneControlledBlocks(live).empty()); // identical scenes

    // A live edit in the active scene shows up immediately.
    live.bypass["delay"] = false;
    auto marked = scenes.sceneControlledBlocks(live);
    assert(marked.size() == 1 && marked.count("delay"));

    // Assigned params mark the block even when values are equal.
    scenes.assignParam("drive", 0, 0.2f);
    marked = scenes.sceneControlledBlocks(live);
    assert(marked.count("drive") && marked.count("delay"));

    // Removed blocks are not reported.
    live.bypass.erase("drive");
    live.params.erase("drive");
    assert(!scenes.sceneControlledBlocks(live).count("drive"));
}

// Minimal node: output = input * 2, so wet and dry are distinguishable.
class DoublerNode : public AudioNode {
public:
    DoublerNode() {
        m_ports = {{"In", true, false, 0, nullptr}, {"Out", false, false, 0, nullptr}};
    }
    std::string getName() const override { return "Doubler"; }
    NodeType getType() const override { return NodeType::LV2Plugin; }
    void prepare(double, int maxBlockSize) override {
        in.assign(maxBlockSize, 1.0f);
        out.assign(maxBlockSize, 0.0f);
        m_ports[0].buffer = in.data();
        m_ports[1].buffer = out.data();
    }
    void process(int n) override {
        for (int i = 0; i < n; ++i) out[i] = in[i] * 2.0f;
    }
    std::vector<float> in, out;
};

void testBypassCrossfade() {
    DoublerNode node;
    node.prepareBlock(48000.0, 64);
    node.processBlock(64);
    assert(node.out[63] == 2.0f); // fully wet

    node.setBypassed(true);
    node.processBlock(64);
    // Ramping: starts near wet, moves toward dry, never jumps.
    assert(node.out[0] > 1.9f && node.out[0] <= 2.0f);
    for (int i = 1; i < 64; ++i) assert(node.out[i] <= node.out[i - 1] + 1e-6f);
    // 5 ms at 48 kHz = 240 samples; after enough blocks it is fully dry.
    for (int b = 0; b < 5; ++b) node.processBlock(64);
    assert(node.out[0] == 1.0f && node.out[63] == 1.0f);

    node.setBypassed(false);
    node.processBlock(64);
    assert(node.out[0] >= 1.0f && node.out[0] < 1.1f);
    for (int b = 0; b < 5; ++b) node.processBlock(64);
    assert(node.out[63] == 2.0f);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testLibraryLabelsAndMigration();
    testLibraryNavigationAndReconcile();
    testLegacyBankSizeKeepsPositions();
    testScenesSwitchAndRecall();
    testScenesJsonRoundTripAndPrune();
    testBankNamesAndSceneSummaries();
    testSceneControlledBlocks();
    testBypassCrossfade();
    std::cout << "Preset tests passed" << std::endl;
    return 0;
}
