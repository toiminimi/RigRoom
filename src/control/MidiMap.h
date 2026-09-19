#pragma once
#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

// MIDI mapping logic, kept free of Qt widgets and JACK so it can be tested.
//
// Two layers, like Helix/Quad Cortex:
//  - GlobalMidiConfig: device, channel, Program Change -> preset slot and the
//    performance commands (scene, bank, A-D, prev/next) on fixed CCs.
//  - PresetMidiMap: per-preset assignments made with MIDI Learn that switch a
//    block on/off or drive a parameter (e.g. an expression pedal).

struct MidiMessage {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;

    int channel() const { return (status & 0x0F) + 1; } // 1-16
    bool isCC() const { return (status & 0xF0) == 0xB0; }
    bool isPC() const { return (status & 0xF0) == 0xC0; }
    QString describe() const; // "CC 69 = 2 (ch 1)"
};

enum class MidiCommand {
    SceneSelect,
    PresetPrev, PresetNext,
    BankDown, BankUp,
    SelectA, SelectB, SelectC, SelectD, // one per preset in a bank
    ScenePrev, SceneNext,
    // One switch per scene (fires on press). Off by default.
    Scene1, Scene2, Scene3, Scene4, Scene5, Scene6, Scene7, Scene8,
    Count
};

constexpr int kMidiCommandCount = static_cast<int>(MidiCommand::Count);
QString midiCommandName(MidiCommand command);

struct GlobalMidiConfig {
    std::string inputPort;
    int channel = 0;              // 0 = omni, otherwise 1-16
    // What Program Change selects: a preset slot (PC 0 = 01A), a scene of the
    // loaded preset (PC 0 = scene 1, for PC-only footswitches), or nothing.
    enum class PcMode { Off, Presets, Scenes };
    PcMode pcMode = PcMode::Presets;
    bool bankSelect = true;       // CC0 (MSB) picks a block of 128 slots (Presets mode)
    int pcOffset = 0;             // 1 when the device's PC 1 should mean slot 01A
    std::array<int, kMidiCommandCount> commandCC{}; // -1 = unassigned

    static GlobalMidiConfig defaults();
    static int defaultCC(MidiCommand command);
    std::optional<MidiCommand> commandForCC(int cc) const;

    QJsonObject toJson() const;
    static GlobalMidiConfig fromJson(const QJsonObject& obj);
};

struct MidiAssignment {
    enum class Target { Bypass, Param };
    enum class Mode { Toggle, Follow }; // Bypass: toggle on press, or follow the value

    int cc = 0;
    std::string nodeId;
    Target target = Target::Bypass;
    uint32_t paramIndex = 0;
    Mode mode = Mode::Toggle;
    float min = 0.0f;   // Param: normalized range the CC sweeps over
    float max = 1.0f;
    bool invert = false;

    bool sameTarget(const MidiAssignment& other) const {
        return nodeId == other.nodeId && target == other.target &&
               (target == Target::Bypass || paramIndex == other.paramIndex);
    }
};

class PresetMidiMap {
public:
    const std::vector<MidiAssignment>& assignments() const { return m_assignments; }
    std::vector<MidiAssignment>& assignments() { return m_assignments; }

    // Replaces any existing assignment for the same target.
    void set(const MidiAssignment& assignment);
    void removeFor(const std::string& nodeId, MidiAssignment::Target target, uint32_t paramIndex = 0);
    const MidiAssignment* find(const std::string& nodeId, MidiAssignment::Target target, uint32_t paramIndex = 0) const;
    bool hasNode(const std::string& nodeId) const;
    std::set<std::string> nodeIds() const;
    // Drops assignments for blocks that no longer exist.
    void prune(const std::set<std::string>& liveNodeIds);
    void clear() { m_assignments.clear(); }

    QJsonObject toJson() const;
    static PresetMidiMap fromJson(const QJsonObject& obj);

private:
    std::vector<MidiAssignment> m_assignments;
};

struct MidiAction {
    enum class Kind {
        SelectSlot,       // value = slot
        StepPreset,       // value = +1/-1
        StepBank,         // value = +1/-1
        SelectInBank,     // value = index in bank (A = 0)
        SelectScene,      // value = scene index
        StepScene,        // value = +1/-1
        ToggleBlock,      // nodeId
        SetBlockEnabled,  // nodeId, enabled
        SetParam,         // nodeId, paramIndex, normalized
    };
    Kind kind = Kind::SelectSlot;
    int value = 0;
    std::string nodeId;
    uint32_t paramIndex = 0;
    float normalized = 0.0f;
    bool enabled = false;
};

// Turns incoming messages into actions. Stateful only for Bank Select (CC0).
class MidiResolver {
public:
    std::vector<MidiAction> resolve(const MidiMessage& msg, const GlobalMidiConfig& config,
                                    const PresetMidiMap& preset);
    void reset() { m_bankMsb = 0; }

private:
    int m_bankMsb = 0;
};
