#include "MidiMap.h"
#include <QJsonArray>
#include <algorithm>

namespace {
const char* commandKey(MidiCommand command) {
    switch (command) {
    case MidiCommand::SceneSelect: return "sceneSelect";
    case MidiCommand::PresetPrev: return "presetPrev";
    case MidiCommand::PresetNext: return "presetNext";
    case MidiCommand::BankDown: return "bankDown";
    case MidiCommand::BankUp: return "bankUp";
    case MidiCommand::SelectA: return "selectA";
    case MidiCommand::SelectB: return "selectB";
    case MidiCommand::SelectC: return "selectC";
    case MidiCommand::SelectD: return "selectD";
    case MidiCommand::ScenePrev: return "scenePrev";
    case MidiCommand::SceneNext: return "sceneNext";
    case MidiCommand::Scene1: return "scene1";
    case MidiCommand::Scene2: return "scene2";
    case MidiCommand::Scene3: return "scene3";
    case MidiCommand::Scene4: return "scene4";
    case MidiCommand::Scene5: return "scene5";
    case MidiCommand::Scene6: return "scene6";
    case MidiCommand::Scene7: return "scene7";
    case MidiCommand::Scene8: return "scene8";
    case MidiCommand::Count: break;
    }
    return "";
}

constexpr int kPressThreshold = 64; // footswitches send 127 on press, 0 on release
}

QString MidiMessage::describe() const {
    const int ch = channel();
    switch (status & 0xF0) {
    case 0xB0: return QString("CC %1 = %2 (ch %3)").arg(data1).arg(data2).arg(ch);
    case 0xC0: return QString("PC %1 (ch %2)").arg(data1).arg(ch);
    case 0x90: return QString("Note On %1 (ch %2)").arg(data1).arg(ch);
    case 0x80: return QString("Note Off %1 (ch %2)").arg(data1).arg(ch);
    case 0xE0: return QString("Pitch Bend (ch %1)").arg(ch);
    default: return QString("0x%1 (ch %2)").arg(status, 2, 16, QChar('0')).arg(ch);
    }
}

QString midiCommandName(MidiCommand command) {
    switch (command) {
    case MidiCommand::SceneSelect: return "Scene select (value)";
    case MidiCommand::PresetPrev: return "Previous preset";
    case MidiCommand::PresetNext: return "Next preset";
    case MidiCommand::BankDown: return "Bank down";
    case MidiCommand::BankUp: return "Bank up";
    case MidiCommand::SelectA: return "Preset A";
    case MidiCommand::SelectB: return "Preset B";
    case MidiCommand::SelectC: return "Preset C";
    case MidiCommand::SelectD: return "Preset D";
    case MidiCommand::ScenePrev: return "Previous scene";
    case MidiCommand::SceneNext: return "Next scene";
    case MidiCommand::Scene1: case MidiCommand::Scene2: case MidiCommand::Scene3: case MidiCommand::Scene4:
    case MidiCommand::Scene5: case MidiCommand::Scene6: case MidiCommand::Scene7: case MidiCommand::Scene8:
        return QString("Scene %1").arg(static_cast<int>(command) - static_cast<int>(MidiCommand::Scene1) + 1);
    case MidiCommand::Count: break;
    }
    return QString();
}

int GlobalMidiConfig::defaultCC(MidiCommand command) {
    // CC69 matches Helix snapshot select; the rest sit in the MIDI spec's
    // undefined range (102-119) so they don't clash with mod wheel, volume,
    // expression or sustain.
    switch (command) {
    case MidiCommand::SceneSelect: return 69;
    case MidiCommand::PresetPrev: return 102;
    case MidiCommand::PresetNext: return 103;
    case MidiCommand::BankDown: return 104;
    case MidiCommand::BankUp: return 105;
    case MidiCommand::ScenePrev: return 114;
    case MidiCommand::SceneNext: return 115;
    case MidiCommand::Scene1: case MidiCommand::Scene2: case MidiCommand::Scene3: case MidiCommand::Scene4:
    case MidiCommand::Scene5: case MidiCommand::Scene6: case MidiCommand::Scene7: case MidiCommand::Scene8:
        return -1; // opt-in, so they don't claim CCs you may want per preset
    case MidiCommand::Count: return -1;
    default:
        return 106 + (static_cast<int>(command) - static_cast<int>(MidiCommand::SelectA));
    }
}

GlobalMidiConfig GlobalMidiConfig::defaults() {
    GlobalMidiConfig config;
    for (int i = 0; i < kMidiCommandCount; ++i) config.commandCC[i] = defaultCC(static_cast<MidiCommand>(i));
    return config;
}

std::optional<MidiCommand> GlobalMidiConfig::commandForCC(int cc) const {
    if (cc < 0) return std::nullopt;
    for (int i = 0; i < kMidiCommandCount; ++i) {
        if (commandCC[i] == cc) return static_cast<MidiCommand>(i);
    }
    return std::nullopt;
}

QJsonObject GlobalMidiConfig::toJson() const {
    QJsonObject commands;
    for (int i = 0; i < kMidiCommandCount; ++i) commands[commandKey(static_cast<MidiCommand>(i))] = commandCC[i];
    return QJsonObject{
        {"inputPort", QString::fromStdString(inputPort)},
        {"channel", channel},
        {"pcMode", pcMode == PcMode::Scenes ? "scenes" : pcMode == PcMode::Off ? "off" : "presets"},
        {"bankSelect", bankSelect},
        {"pcOffset", pcOffset},
        {"commands", commands},
    };
}

GlobalMidiConfig GlobalMidiConfig::fromJson(const QJsonObject& obj) {
    GlobalMidiConfig config = defaults();
    config.inputPort = obj["inputPort"].toString().toStdString();
    config.channel = std::clamp(obj["channel"].toInt(0), 0, 16);
    const QString pcMode = obj["pcMode"].toString();
    if (pcMode == "scenes") config.pcMode = PcMode::Scenes;
    else if (pcMode == "off") config.pcMode = PcMode::Off;
    else if (pcMode.isEmpty() && !obj["programChange"].toBool(true)) config.pcMode = PcMode::Off; // older config
    else config.pcMode = PcMode::Presets;
    config.bankSelect = obj["bankSelect"].toBool(true);
    config.pcOffset = std::clamp(obj["pcOffset"].toInt(0), 0, 1);
    const QJsonObject commands = obj["commands"].toObject();
    for (int i = 0; i < kMidiCommandCount; ++i) {
        const QString key = commandKey(static_cast<MidiCommand>(i));
        if (commands.contains(key)) config.commandCC[i] = std::clamp(commands[key].toInt(-1), -1, 127);
    }
    return config;
}

void PresetMidiMap::set(const MidiAssignment& assignment) {
    for (auto& existing : m_assignments) {
        if (existing.sameTarget(assignment)) {
            existing = assignment;
            return;
        }
    }
    m_assignments.push_back(assignment);
}

void PresetMidiMap::removeFor(const std::string& nodeId, MidiAssignment::Target target, uint32_t paramIndex) {
    MidiAssignment probe;
    probe.nodeId = nodeId;
    probe.target = target;
    probe.paramIndex = paramIndex;
    m_assignments.erase(std::remove_if(m_assignments.begin(), m_assignments.end(),
                                       [&probe](const MidiAssignment& a) { return a.sameTarget(probe); }),
                        m_assignments.end());
}

const MidiAssignment* PresetMidiMap::find(const std::string& nodeId, MidiAssignment::Target target,
                                          uint32_t paramIndex) const {
    MidiAssignment probe;
    probe.nodeId = nodeId;
    probe.target = target;
    probe.paramIndex = paramIndex;
    for (const auto& a : m_assignments) {
        if (a.sameTarget(probe)) return &a;
    }
    return nullptr;
}

bool PresetMidiMap::hasNode(const std::string& nodeId) const {
    return std::any_of(m_assignments.begin(), m_assignments.end(),
                       [&nodeId](const MidiAssignment& a) { return a.nodeId == nodeId; });
}

std::set<std::string> PresetMidiMap::nodeIds() const {
    std::set<std::string> ids;
    for (const auto& a : m_assignments) ids.insert(a.nodeId);
    return ids;
}

void PresetMidiMap::prune(const std::set<std::string>& liveNodeIds) {
    m_assignments.erase(std::remove_if(m_assignments.begin(), m_assignments.end(),
                                       [&liveNodeIds](const MidiAssignment& a) { return !liveNodeIds.count(a.nodeId); }),
                        m_assignments.end());
}

QJsonObject PresetMidiMap::toJson() const {
    QJsonArray list;
    for (const auto& a : m_assignments) {
        QJsonObject obj{
            {"cc", a.cc},
            {"node", QString::fromStdString(a.nodeId)},
            {"target", a.target == MidiAssignment::Target::Bypass ? "bypass" : "param"},
        };
        if (a.target == MidiAssignment::Target::Bypass) {
            obj["mode"] = a.mode == MidiAssignment::Mode::Toggle ? "toggle" : "follow";
        } else {
            obj["param"] = static_cast<int>(a.paramIndex);
            obj["min"] = a.min;
            obj["max"] = a.max;
            obj["invert"] = a.invert;
        }
        list.append(obj);
    }
    return QJsonObject{{"assignments", list}};
}

PresetMidiMap PresetMidiMap::fromJson(const QJsonObject& obj) {
    PresetMidiMap map;
    for (const QJsonValue& value : obj["assignments"].toArray()) {
        const QJsonObject a = value.toObject();
        MidiAssignment assignment;
        assignment.cc = a["cc"].toInt(-1);
        assignment.nodeId = a["node"].toString().toStdString();
        if (assignment.cc < 0 || assignment.cc > 127 || assignment.nodeId.empty()) continue;
        if (a["target"].toString() == "param") {
            assignment.target = MidiAssignment::Target::Param;
            assignment.paramIndex = static_cast<uint32_t>(a["param"].toInt());
            assignment.min = std::clamp(static_cast<float>(a["min"].toDouble(0.0)), 0.0f, 1.0f);
            assignment.max = std::clamp(static_cast<float>(a["max"].toDouble(1.0)), 0.0f, 1.0f);
            assignment.invert = a["invert"].toBool(false);
        } else {
            assignment.target = MidiAssignment::Target::Bypass;
            assignment.mode = a["mode"].toString() == "follow" ? MidiAssignment::Mode::Follow
                                                               : MidiAssignment::Mode::Toggle;
        }
        map.set(assignment);
    }
    return map;
}

std::vector<MidiAction> MidiResolver::resolve(const MidiMessage& msg, const GlobalMidiConfig& config,
                                              const PresetMidiMap& preset) {
    std::vector<MidiAction> actions;
    if (config.channel != 0 && msg.channel() != config.channel) return actions;

    if (msg.isPC()) {
        if (config.pcMode == GlobalMidiConfig::PcMode::Off) return actions;
        const int program = static_cast<int>(msg.data1) - config.pcOffset;
        if (program < 0) return actions;
        MidiAction action;
        if (config.pcMode == GlobalMidiConfig::PcMode::Scenes) {
            action.kind = MidiAction::Kind::SelectScene;
            action.value = program;
        } else {
            action.kind = MidiAction::Kind::SelectSlot;
            action.value = (config.bankSelect ? m_bankMsb * 128 : 0) + program;
        }
        actions.push_back(action);
        return actions;
    }
    if (!msg.isCC()) return actions;

    const int cc = msg.data1;
    const int value = msg.data2;
    if (config.bankSelect && cc == 0) {  // Bank Select MSB, applies to the next PC
        m_bankMsb = value;
        return actions;
    }
    if (config.bankSelect && cc == 32) return actions; // LSB: not used

    // Global performance commands own their CC.
    if (const auto command = config.commandForCC(cc)) {
        MidiAction action;
        const bool pressed = value >= kPressThreshold;
        switch (*command) {
        case MidiCommand::SceneSelect:
            action.kind = MidiAction::Kind::SelectScene;
            action.value = value;
            actions.push_back(action);
            break;
        case MidiCommand::PresetPrev:
        case MidiCommand::PresetNext:
            if (!pressed) break;
            action.kind = MidiAction::Kind::StepPreset;
            action.value = *command == MidiCommand::PresetNext ? 1 : -1;
            actions.push_back(action);
            break;
        case MidiCommand::BankDown:
        case MidiCommand::BankUp:
            if (!pressed) break;
            action.kind = MidiAction::Kind::StepBank;
            action.value = *command == MidiCommand::BankUp ? 1 : -1;
            actions.push_back(action);
            break;
        case MidiCommand::ScenePrev:
        case MidiCommand::SceneNext:
            if (!pressed) break;
            action.kind = MidiAction::Kind::StepScene;
            action.value = *command == MidiCommand::SceneNext ? 1 : -1;
            actions.push_back(action);
            break;
        case MidiCommand::Scene1: case MidiCommand::Scene2: case MidiCommand::Scene3: case MidiCommand::Scene4:
        case MidiCommand::Scene5: case MidiCommand::Scene6: case MidiCommand::Scene7: case MidiCommand::Scene8:
            if (!pressed) break;
            action.kind = MidiAction::Kind::SelectScene;
            action.value = static_cast<int>(*command) - static_cast<int>(MidiCommand::Scene1);
            actions.push_back(action);
            break;
        case MidiCommand::Count:
            break;
        default:
            if (!pressed) break;
            action.kind = MidiAction::Kind::SelectInBank;
            action.value = static_cast<int>(*command) - static_cast<int>(MidiCommand::SelectA);
            actions.push_back(action);
            break;
        }
        return actions;
    }

    // Per-preset assignments; one CC may drive several targets.
    for (const auto& a : preset.assignments()) {
        if (a.cc != cc) continue;
        MidiAction action;
        action.nodeId = a.nodeId;
        if (a.target == MidiAssignment::Target::Bypass) {
            if (a.mode == MidiAssignment::Mode::Toggle) {
                if (value < kPressThreshold) continue;
                action.kind = MidiAction::Kind::ToggleBlock;
            } else {
                action.kind = MidiAction::Kind::SetBlockEnabled;
                action.enabled = value >= kPressThreshold;
            }
        } else {
            float t = static_cast<float>(value) / 127.0f;
            if (a.invert) t = 1.0f - t;
            action.kind = MidiAction::Kind::SetParam;
            action.paramIndex = a.paramIndex;
            action.normalized = a.min + (a.max - a.min) * t;
        }
        actions.push_back(action);
    }
    return actions;
}
