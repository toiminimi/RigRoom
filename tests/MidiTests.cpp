#include "control/MidiMap.h"
#include <QCoreApplication>
#include <cassert>
#include <cmath>
#include <iostream>

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

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testDefaultsAndJson();
    testProgramChangeAndBankSelect();
    testChannelFilter();
    testSceneSwitches();
    testCommands();
    testPresetAssignments();
    testPresetJsonAndPrune();
    std::cout << "MIDI tests passed" << std::endl;
    return 0;
}
