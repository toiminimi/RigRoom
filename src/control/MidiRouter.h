#pragma once
#include "MidiMap.h"
#include <QObject>
#include <functional>

class AudioEngine;
class QTimer;

// Drains MIDI from the engine on the GUI thread, resolves it against the
// global config and the loaded preset's assignments, and hands the actions to
// the owner (which applies them through RigController).
class MidiRouter : public QObject {
    Q_OBJECT
public:
    using LearnCallback = std::function<void(const MidiMessage&)>;
    using ActionHandler = std::function<void(const MidiAction&)>;

    MidiRouter(AudioEngine& engine, QObject* parent = nullptr);

    void setConfig(const GlobalMidiConfig* config) { m_config = config; }
    void setPresetMap(const PresetMidiMap* preset) { m_preset = preset; }
    void setActionHandler(ActionHandler handler) { m_handler = std::move(handler); }

    // The next CC or PC goes to `callback` instead of being applied.
    void startLearn(LearnCallback callback);
    void cancelLearn();
    bool isLearning() const { return static_cast<bool>(m_learn); }

    // Polls only while a MIDI input is connected.
    void setActive(bool active);

signals:
    void activity(const MidiMessage& message);
    void learnCancelled();

private:
    void drain();

    AudioEngine& m_engine;
    QTimer* m_timer = nullptr;
    const GlobalMidiConfig* m_config = nullptr;
    const PresetMidiMap* m_preset = nullptr;
    ActionHandler m_handler;
    LearnCallback m_learn;
    MidiResolver m_resolver;
};
