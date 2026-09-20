#include "MidiRouter.h"
#include "../audio/AudioEngine.h"
#include <QTimer>
#include <map>

MidiRouter::MidiRouter(AudioEngine& engine, QObject* parent) : QObject(parent), m_engine(engine) {
    m_timer = new QTimer(this);
    // Short enough that an expression pedal feels immediate.
    m_timer->setInterval(3);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &MidiRouter::drain);
}

void MidiRouter::setActive(bool active) {
    if (active && !m_timer->isActive()) m_timer->start();
    else if (!active) m_timer->stop();
}

void MidiRouter::startLearn(LearnCallback callback) {
    m_learn = std::move(callback);
}

void MidiRouter::cancelLearn() {
    if (!m_learn) return;
    m_learn = nullptr;
    emit learnCancelled();
}

void MidiRouter::drain() {
    std::vector<MidiMessage> messages;
    AudioEngine::RawMidi raw;
    while (m_engine.readMidi(raw)) messages.push_back({raw.status, raw.data1, raw.data2});
    if (messages.empty()) return;

    // Keep only the last value of each continuous CC in this batch, so a pedal
    // sweep costs one parameter update per tick. Order is otherwise preserved.
    std::map<std::pair<uint8_t, uint8_t>, size_t> lastIndex;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].isCC()) lastIndex[{messages[i].status, messages[i].data1}] = i;
    }

    for (size_t i = 0; i < messages.size(); ++i) {
        const MidiMessage& msg = messages[i];
        if (m_learn && (msg.isCC() || msg.isPC())) {
            if (m_config && m_config->channel != 0 && msg.channel() != m_config->channel) continue;
            auto callback = std::move(m_learn);
            m_learn = nullptr;
            callback(msg);
            continue;
        }
        if (msg.isCC()) {
            const bool isLast = lastIndex[{msg.status, msg.data1}] == i;
            // Switch-like CCs (press/release) must all be seen; only drop
            // intermediate values of the same controller within the batch.
            if (!isLast && msg.data2 != 0 && msg.data2 != 127) continue;
        }
        if (!m_config || !m_preset || !m_handler) continue;
        for (const MidiAction& action : m_resolver.resolve(msg, *m_config, *m_preset)) m_handler(action);
    }
    emit activity(messages.back()); // once per batch; enough for an indicator
}
