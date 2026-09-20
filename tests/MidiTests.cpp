#include "control/MidiMap.h"
#include "control/RigController.h"
#include <QCoreApplication>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

namespace {

MidiMessage cc(int number, int value, int channel = 1) {
    return {static_cast<uint8_t>(0xB0 | (channel - 1)), static_cast<uint8_t>(number), static_cast<uint8_t>(value)};
}

MidiMessage pc(int program, int channel = 1) {
    return {static_cast<uint8_t>(0xC0 | (channel - 1)), static_cast<uint8_t>(program), 0};
}

void testDefaultsAndJson() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    assert(config.commandCC[static_cast<int>(MidiCommand::SceneSelect)] == 69);
    assert(config.commandCC[static_cast<int>(MidiCommand::SelectA)] == 106);
    assert(config.commandCC[static_cast<int>(MidiCommand::SelectD)] == 109);
    assert(!config.commandForCC(110)); // no E-H: banks hold 4 presets
    assert(config.commandForCC(105) == MidiCommand::BankUp);
    assert(!config.commandForCC(11)); // expression stays free
    // No default collides with another; per-scene switches start Off.
    for (int i = 0; i < kMidiCommandCount; ++i)
        for (int j = i + 1; j < kMidiCommandCount; ++j)
            assert(config.commandCC[i] < 0 || config.commandCC[i] != config.commandCC[j]);
    assert(config.commandCC[static_cast<int>(MidiCommand::Scene1)] == -1);
    assert(config.commandCC[static_cast<int>(MidiCommand::Scene8)] == -1);

    config.inputPort = "a2j:MC6 [20] (capture): MC6 MIDI 1";
    config.channel = 3;
    config.pcOffset = 1;
    config.bankSelect = false;
    config.commandCC[static_cast<int>(MidiCommand::SceneNext)] = -1;
    const GlobalMidiConfig back = GlobalMidiConfig::fromJson(config.toJson());
    assert(back.inputPort == config.inputPort && back.channel == 3 && back.pcOffset == 1 && !back.bankSelect);
    assert(back.commandCC[static_cast<int>(MidiCommand::SceneNext)] == -1);
    assert(back.commandCC[static_cast<int>(MidiCommand::SceneSelect)] == 69);

    // Missing object -> defaults.
    const GlobalMidiConfig empty = GlobalMidiConfig::fromJson(QJsonObject());
    assert(empty.pcMode == GlobalMidiConfig::PcMode::Presets && empty.channel == 0 && empty.commandCC[0] == 69);

    // PC mode round-trips; older configs stored a "programChange" bool.
    config.pcMode = GlobalMidiConfig::PcMode::Scenes;
    config.commandCC[static_cast<int>(MidiCommand::Scene3)] = 22;
    const GlobalMidiConfig scenes = GlobalMidiConfig::fromJson(config.toJson());
    assert(scenes.pcMode == GlobalMidiConfig::PcMode::Scenes);
    assert(scenes.commandCC[static_cast<int>(MidiCommand::Scene3)] == 22);
    assert(GlobalMidiConfig::fromJson(QJsonObject{{"programChange", false}}).pcMode == GlobalMidiConfig::PcMode::Off);
    assert(GlobalMidiConfig::fromJson(QJsonObject{{"programChange", true}}).pcMode == GlobalMidiConfig::PcMode::Presets);
}

void testProgramChangeAndBankSelect() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    PresetMidiMap preset;
    MidiResolver resolver;

    auto actions = resolver.resolve(pc(5), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::SelectSlot && actions[0].value == 5);

    assert(resolver.resolve(cc(0, 1), config, preset).empty()); // bank MSB 1
    actions = resolver.resolve(pc(2), config, preset);
    assert(actions[0].value == 130);

    config.bankSelect = false;
    resolver.reset();
    actions = resolver.resolve(pc(2), config, preset);
    assert(actions[0].value == 2);

    config.pcOffset = 1;
    assert(resolver.resolve(pc(0), config, preset).empty());
    actions = resolver.resolve(pc(1), config, preset);
    assert(actions[0].value == 0);

    config.pcMode = GlobalMidiConfig::PcMode::Off;
    assert(resolver.resolve(pc(3), config, preset).empty());

    // PC-only footswitches can pick scenes instead (bank select not involved).
    config.pcMode = GlobalMidiConfig::PcMode::Scenes;
    config.pcOffset = 0;
    config.bankSelect = true;
    resolver.resolve(cc(0, 2), config, preset);
    actions = resolver.resolve(pc(3), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::SelectScene && actions[0].value == 3);
    config.pcOffset = 1;
    actions = resolver.resolve(pc(1), config, preset);
    assert(actions[0].kind == MidiAction::Kind::SelectScene && actions[0].value == 0);
}

void testSceneSwitches() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    PresetMidiMap preset;
    MidiResolver resolver;
    // Off by default: the CC falls through to preset assignments.
    assert(resolver.resolve(cc(20, 127), config, preset).empty());

    config.commandCC[static_cast<int>(MidiCommand::Scene1)] = 20;
    config.commandCC[static_cast<int>(MidiCommand::Scene4)] = 23;
    auto actions = resolver.resolve(cc(23, 127), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::SelectScene && actions[0].value == 3);
    assert(resolver.resolve(cc(23, 0), config, preset).empty()); // release
    actions = resolver.resolve(cc(20, 100), config, preset);
    assert(actions[0].value == 0);
}

void testChannelFilter() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    PresetMidiMap preset;
    MidiResolver resolver;
    config.channel = 2;
    assert(resolver.resolve(pc(1, 1), config, preset).empty());
    assert(resolver.resolve(pc(1, 2), config, preset).size() == 1);
    config.channel = 0;
    assert(resolver.resolve(pc(1, 16), config, preset).size() == 1);
}

void testCommands() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    PresetMidiMap preset;
    MidiResolver resolver;

    auto actions = resolver.resolve(cc(69, 2), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::SelectScene && actions[0].value == 2);
    actions = resolver.resolve(cc(69, 0), config, preset); // scene 1 is value 0, not a "release"
    assert(actions.size() == 1 && actions[0].value == 0);

    // Momentary switches: press fires, release is ignored.
    actions = resolver.resolve(cc(103, 127), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::StepPreset && actions[0].value == 1);
    assert(resolver.resolve(cc(103, 0), config, preset).empty());

    actions = resolver.resolve(cc(104, 127), config, preset);
    assert(actions[0].kind == MidiAction::Kind::StepBank && actions[0].value == -1);
    actions = resolver.resolve(cc(108, 100), config, preset);
    assert(actions[0].kind == MidiAction::Kind::SelectInBank && actions[0].value == 2); // C
    actions = resolver.resolve(cc(114, 127), config, preset);
    assert(actions[0].kind == MidiAction::Kind::StepScene && actions[0].value == -1);

    // A global command owns its CC even if a preset assignment uses it.
    MidiAssignment clash;
    clash.cc = 69;
    clash.nodeId = "drive";
    preset.set(clash);
    actions = resolver.resolve(cc(69, 127), config, preset);
    assert(actions.size() == 1 && actions[0].kind == MidiAction::Kind::SelectScene);
}

void testPresetAssignments() {
    GlobalMidiConfig config = GlobalMidiConfig::defaults();
    PresetMidiMap preset;
    MidiResolver resolver;

    MidiAssignment toggle;
    toggle.cc = 80;
    toggle.nodeId = "drive";
    preset.set(toggle);

    MidiAssignment follow;
    follow.cc = 81;
    follow.nodeId = "delay";
    follow.mode = MidiAssignment::Mode::Follow;
    preset.set(follow);

    MidiAssignment wah;
    wah.cc = 11;
    wah.nodeId = "wah";
    wah.target = MidiAssignment::Target::Param;
    wah.paramIndex = 4;
    wah.min = 0.2f;
    wah.max = 0.8f;
    preset.set(wah);

    // Second target on the same CC.
    MidiAssignment reverb;
    reverb.cc = 80;
    reverb.nodeId = "reverb";
    preset.set(reverb);

    auto actions = resolver.resolve(cc(80, 127), config, preset);
    assert(actions.size() == 2 && actions[0].kind == MidiAction::Kind::ToggleBlock && actions[0].nodeId == "drive");
    assert(actions[1].nodeId == "reverb");
    assert(resolver.resolve(cc(80, 0), config, preset).empty());

    actions = resolver.resolve(cc(81, 127), config, preset);
    assert(actions[0].kind == MidiAction::Kind::SetBlockEnabled && actions[0].enabled);
    actions = resolver.resolve(cc(81, 0), config, preset);
    assert(actions[0].kind == MidiAction::Kind::SetBlockEnabled && !actions[0].enabled);

    actions = resolver.resolve(cc(11, 0), config, preset);
    assert(actions[0].kind == MidiAction::Kind::SetParam && actions[0].paramIndex == 4);
    assert(std::abs(actions[0].normalized - 0.2f) < 1e-5f);
    actions = resolver.resolve(cc(11, 127), config, preset);
    assert(std::abs(actions[0].normalized - 0.8f) < 1e-5f);

    preset.assignments()[2].invert = true;
    actions = resolver.resolve(cc(11, 127), config, preset);
    assert(std::abs(actions[0].normalized - 0.2f) < 1e-5f);

    // set() replaces an existing assignment for the same target.
    MidiAssignment relearn = toggle;
    relearn.cc = 90;
    preset.set(relearn);
    assert(preset.assignments().size() == 4);
    assert(preset.find("drive", MidiAssignment::Target::Bypass)->cc == 90);
    assert(preset.hasNode("wah") && !preset.hasNode("amp"));

    preset.removeFor("wah", MidiAssignment::Target::Param, 4);
    assert(!preset.hasNode("wah"));
}

void testPresetJsonAndPrune() {
    PresetMidiMap preset;
    MidiAssignment a;
    a.cc = 80;
    a.nodeId = "drive";
    a.mode = MidiAssignment::Mode::Follow;
    preset.set(a);
    MidiAssignment p;
    p.cc = 11;
    p.nodeId = "wah";
    p.target = MidiAssignment::Target::Param;
    p.paramIndex = 3;
    p.min = 0.25f;
    p.invert = true;
    preset.set(p);

    const PresetMidiMap back = PresetMidiMap::fromJson(preset.toJson());
    assert(back.assignments().size() == 2);
    const MidiAssignment* b = back.find("drive", MidiAssignment::Target::Bypass);
    assert(b && b->cc == 80 && b->mode == MidiAssignment::Mode::Follow);
    const MidiAssignment* w = back.find("wah", MidiAssignment::Target::Param, 3);
    assert(w && w->cc == 11 && std::abs(w->min - 0.25f) < 1e-5f && w->invert);

    // Older presets have no "midi" key.
    assert(PresetMidiMap::fromJson(QJsonObject()).assignments().empty());

    PresetMidiMap pruned = back;
    pruned.prune({"drive"});
    assert(pruned.assignments().size() == 1 && pruned.hasNode("drive"));
}

} // namespace

// A library of 3 banks x 4 slots. Occupied: 01A, 01C, 02B and 03D (slot 11).
RigController::Backend testBackend(int* currentSlot, int* viewBank, bool* withinBank) {
    static const std::set<int> occupied{0, 2, 5, 11};
    RigController::Backend backend;
    backend.currentSlot = [currentSlot]() { return *currentSlot; };
    backend.loadSlot = [currentSlot](int slot, bool) { *currentSlot = slot; return true; };
    backend.slotFor = [](int bank, int index) { return bank * 4 + index; };
    backend.slotOccupied = [](int slot) { return occupied.count(slot) > 0; };
    backend.slotsPerBank = []() { return 4; };
    backend.viewBank = [viewBank]() { return *viewBank; };
    backend.stepWithinBank = [withinBank]() { return *withinBank; };
    backend.nextOccupiedSlot = [](int from, int dir) {
        // Across the whole library, wrapping at the ends.
        for (int i = 1; i <= 12; ++i) {
            const int slot = ((from + dir * i) % 12 + 12) % 12;
            if (occupied.count(slot)) return slot;
        }
        return -1;
    };
    return backend;
}

// The pickup rule, as MainWindow applies it: the value only starts following
// once the controller has reached or passed it.
struct Pickup {
    bool engaged = false;
    float lastSeen = -1.0f;
    bool accepts(float incoming, float current) {
        constexpr float tolerance = 1.5f / 127.0f;
        if (!engaged) {
            const bool first = lastSeen < 0.0f;
            const bool close = std::abs(incoming - current) <= tolerance;
            const bool crossed = !first && ((lastSeen - current) * (incoming - current) <= 0.0f);
            engaged = close || crossed;
        }
        lastSeen = incoming;
        return engaged;
    }
};

void testParameterTakeover() {
    // The scene left the value at 0.8; the pedal is still down at 0.1.
    Pickup pickup;
    assert(!pickup.accepts(0.10f, 0.8f));   // moving does nothing yet
    assert(!pickup.accepts(0.40f, 0.8f));
    assert(!pickup.accepts(0.75f, 0.8f));   // still short of it
    assert(pickup.accepts(0.81f, 0.8f));    // passed it: takes over
    assert(pickup.accepts(0.20f, 0.81f));   // and follows from then on

    // Landing on the value, or within one controller step of it, counts.
    Pickup exact;
    assert(exact.accepts(0.5f, 0.5f));
    Pickup oneStep;
    assert(oneStep.accepts(0.79f, 0.8f));

    // Coming from above works the same way.
    Pickup above;
    assert(!above.accepts(0.9f, 0.3f));
    assert(above.accepts(0.2f, 0.3f));

    // Jump assignments do not use any of this; they always apply. The choice
    // survives a save and load.
    MidiAssignment assignment;
    assignment.cc = 11;
    assignment.nodeId = "drive";
    assignment.target = MidiAssignment::Target::Param;
    assignment.paramIndex = 3;
    assert(assignment.takeover == MidiAssignment::Takeover::Pickup);  // the default
    assignment.takeover = MidiAssignment::Takeover::Jump;
    PresetMidiMap map;
    map.set(assignment);
    const PresetMidiMap loaded = PresetMidiMap::fromJson(map.toJson());
    const MidiAssignment* back = loaded.find("drive", MidiAssignment::Target::Param, 3);
    assert(back && back->takeover == MidiAssignment::Takeover::Jump);
}

void testPresetStepping() {
    int current = 0;      // 01A
    int viewBank = 0;
    bool withinBank = false;
    RigController rig;
    rig.setBackend(testBackend(&current, &viewBank, &withinBank));

    // Across banks: 01A -> 01C -> 02B -> 03D and back.
    assert(rig.stepPreset(1) && current == 2);
    assert(rig.stepPreset(1) && current == 5);
    assert(rig.stepPreset(-1) && current == 2);

    // Within the shown bank: only 01A and 01C, wrapping between them.
    withinBank = true;
    current = 0;
    assert(rig.stepPreset(1) && current == 2);
    assert(rig.stepPreset(1) && current == 0);   // wraps back, never leaves bank 1
    assert(rig.stepPreset(-1) && current == 2);

    // Bank 2 holds one preset (02B): stepping into it lands there, and stays.
    viewBank = 1;
    assert(rig.stepPreset(1) && current == 5);
    assert(!rig.stepPreset(1) && current == 5);  // nothing else in this bank

    // Bank 3 holds 03D only; coming from another bank, forward lands on it.
    viewBank = 2;
    assert(rig.stepPreset(1) && current == 11);

    // An empty bank changes nothing.
    viewBank = 1;
    current = 5;
    RigController::Backend empty = testBackend(&current, &viewBank, &withinBank);
    empty.slotOccupied = [](int) { return false; };
    rig.setBackend(empty);
    assert(!rig.stepPreset(1) && current == 5);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testDefaultsAndJson();
    testProgramChangeAndBankSelect();
    testChannelFilter();
    testSceneSwitches();
    testCommands();
    testPresetAssignments();
    testPresetJsonAndPrune();
    testPresetStepping();
    testParameterTakeover();
    std::cout << "MIDI tests passed" << std::endl;
    return 0;
}
