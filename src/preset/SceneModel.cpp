#include "SceneModel.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kParamEpsilon = 1e-6f;
}

SceneModel::SceneModel() {
    reset({});
}

const std::vector<SceneModel::NamedColor>& SceneModel::palette() {
    static const std::vector<NamedColor> colors = {
        {"Blue", "#00B0FF"}, {"Green", "#4CAF50"}, {"Orange", "#FF9800"}, {"Pink", "#E91E63"},
        {"Purple", "#AB47BC"}, {"Yellow", "#FFD54F"}, {"Cyan", "#26C6DA"}, {"Red", "#EF5350"},
        {"Lime", "#C0CA33"}, {"Teal", "#26A69A"}, {"Indigo", "#5C6BC0"}, {"White", "#ECEFF1"},
    };
    return colors;
}

QString SceneModel::defaultColor(int index) {
    const auto& colors = palette();
    const int n = static_cast<int>(colors.size());
    return colors[((index % kMaxScenes) + kMaxScenes) % kMaxScenes % n].hex;
}

void SceneModel::reset(const BoardState& live) {
    m_scenes.clear();
    m_assigned.clear();
    Scene scene;
    scene.name = "Scene 1";
    scene.color = defaultColor(0);
    captureInto(scene, live);
    m_scenes.push_back(scene);
    m_active = 0;
}

void SceneModel::captureInto(Scene& scene, const BoardState& live) const {
    for (const auto& [nodeId, bypassed] : live.bypass) {
        scene.bypass[nodeId] = bypassed;
    }
    for (const auto& [nodeId, indices] : m_assigned) {
        auto liveNode = live.params.find(nodeId);
        if (liveNode == live.params.end()) continue;
        for (uint32_t index : indices) {
            auto value = liveNode->second.find(index);
            if (value != liveNode->second.end()) scene.params[nodeId][index] = value->second;
        }
    }
}

void SceneModel::captureActive(const BoardState& live) {
    captureInto(m_scenes[m_active], live);
}

SceneModel::Changes SceneModel::changesTo(int index, const BoardState& live) const {
    Changes changes;
    if (index < 0 || index >= count()) return changes;
    const Scene& target = m_scenes[index];
    changes.levelDb = target.levelDb;

    for (const auto& [nodeId, bypassed] : target.bypass) {
        auto liveIt = live.bypass.find(nodeId);
        if (liveIt != live.bypass.end() && liveIt->second != bypassed) {
            changes.bypass.emplace_back(nodeId, bypassed);
        }
    }
    for (const auto& [nodeId, values] : target.params) {
        auto liveNode = live.params.find(nodeId);
        if (liveNode == live.params.end()) continue;
        for (const auto& [paramIndex, value] : values) {
            if (!isAssigned(nodeId, paramIndex)) continue;
            auto liveValue = liveNode->second.find(paramIndex);
            if (liveValue != liveNode->second.end() && std::abs(liveValue->second - value) > kParamEpsilon) {
                changes.params.emplace_back(nodeId, paramIndex, value);
            }
        }
    }
    return changes;
}

SceneModel::Changes SceneModel::switchTo(int index, const BoardState& live) {
    if (index < 0 || index >= count()) return {};
    captureActive(live);
    Changes changes = changesTo(index, live);
    m_active = index;
    return changes;
}

int SceneModel::addScene(const BoardState& live) {
    return duplicateScene(m_active, live);
}

int SceneModel::duplicateScene(int index, const BoardState& live) {
    if (count() >= kMaxScenes || index < 0 || index >= count()) return -1;
    captureActive(live);
    Scene copy = m_scenes[index];
    const int newIndex = count();
    copy.name = QString("Scene %1").arg(newIndex + 1);
    copy.color = defaultColor(newIndex);
    m_scenes.push_back(copy);
    return newIndex;
}

bool SceneModel::removeScene(int index) {
    if (count() <= 1 || index < 0 || index >= count()) return false;
    m_scenes.erase(m_scenes.begin() + index);
    if (m_active > index || m_active >= count()) m_active = std::max(0, m_active - 1);
    return true;
}

void SceneModel::renameScene(int index, const QString& name) {
    if (index >= 0 && index < count() && !name.trimmed().isEmpty()) m_scenes[index].name = name.trimmed();
}

void SceneModel::setSceneColor(int index, const QString& color) {
    if (index >= 0 && index < count()) m_scenes[index].color = color;
}

void SceneModel::setSceneLevel(int index, float db) {
    if (index >= 0 && index < count()) m_scenes[index].levelDb = std::clamp(db, -24.0f, 12.0f);
}

void SceneModel::overwriteScene(int index, const BoardState& live) {
    if (index >= 0 && index < count()) captureInto(m_scenes[index], live);
}

bool SceneModel::isAssigned(const std::string& nodeId, uint32_t index) const {
    auto it = m_assigned.find(nodeId);
    return it != m_assigned.end() && it->second.count(index) > 0;
}

bool SceneModel::hasAssignedParams(const std::string& nodeId) const {
    auto it = m_assigned.find(nodeId);
    return it != m_assigned.end() && !it->second.empty();
}

void SceneModel::assignParam(const std::string& nodeId, uint32_t index, float value) {
    m_assigned[nodeId].insert(index);
    for (Scene& scene : m_scenes) scene.params[nodeId][index] = value;
}

void SceneModel::unassignParam(const std::string& nodeId, uint32_t index) {
    auto it = m_assigned.find(nodeId);
    if (it == m_assigned.end()) return;
    it->second.erase(index);
    if (it->second.empty()) m_assigned.erase(it);
    for (Scene& scene : m_scenes) {
        auto node = scene.params.find(nodeId);
        if (node == scene.params.end()) continue;
        node->second.erase(index);
        if (node->second.empty()) scene.params.erase(node);
    }
}

std::set<std::string> SceneModel::sceneControlledBlocks(const BoardState& live) const {
    std::set<std::string> result;
    for (const auto& [nodeId, indices] : m_assigned) {
        if (!indices.empty() && live.bypass.count(nodeId)) result.insert(nodeId);
    }
    if (count() < 2) return result;
    for (const auto& [nodeId, liveBypassed] : live.bypass) {
        for (int i = 0; i < count(); ++i) {
            if (i == m_active) continue;
            auto it = m_scenes[i].bypass.find(nodeId);
            if (it != m_scenes[i].bypass.end() && it->second != liveBypassed) {
                result.insert(nodeId);
                break;
            }
        }
    }
    return result;
}

void SceneModel::prune(const BoardState& live) {
    auto alive = [&live](const std::string& id) { return live.bypass.count(id) > 0; };
    for (auto it = m_assigned.begin(); it != m_assigned.end();) {
        it = alive(it->first) ? std::next(it) : m_assigned.erase(it);
    }
    for (Scene& scene : m_scenes) {
        for (auto it = scene.bypass.begin(); it != scene.bypass.end();) {
            it = alive(it->first) ? std::next(it) : scene.bypass.erase(it);
        }
        for (auto it = scene.params.begin(); it != scene.params.end();) {
            it = alive(it->first) ? std::next(it) : scene.params.erase(it);
        }
    }
}

QJsonObject SceneModel::toJson() const {
    QJsonObject assigned;
    for (const auto& [nodeId, indices] : m_assigned) {
        QJsonArray arr;
        for (uint32_t index : indices) arr.append(static_cast<int>(index));
        assigned[QString::fromStdString(nodeId)] = arr;
    }

    QJsonArray list;
    for (const Scene& scene : m_scenes) {
        QJsonObject blocks;
        for (const auto& [nodeId, bypassed] : scene.bypass) {
            blocks[QString::fromStdString(nodeId)] = QJsonObject{{"bypassed", bypassed}};
        }
        QJsonObject params;
        for (const auto& [nodeId, values] : scene.params) {
            QJsonObject valuesObj;
            for (const auto& [index, value] : values) valuesObj[QString::number(index)] = value;
            params[QString::fromStdString(nodeId)] = valuesObj;
        }
        list.append(QJsonObject{
            {"name", scene.name},
            {"color", scene.color},
            {"levelDb", scene.levelDb},
            {"blocks", blocks},
            {"params", params},
        });
    }

    return QJsonObject{
        {"active", m_active},
        {"assignedParams", assigned},
        {"list", list},
    };
}

bool SceneModel::fromJson(const QJsonObject& obj, const BoardState& live) {
    const QJsonArray list = obj["list"].toArray();
    if (list.isEmpty()) {
        reset(live);
        return false;
    }

    m_scenes.clear();
    m_assigned.clear();

    const QJsonObject assigned = obj["assignedParams"].toObject();
    for (auto it = assigned.constBegin(); it != assigned.constEnd(); ++it) {
        for (const QJsonValue& v : it.value().toArray()) {
            m_assigned[it.key().toStdString()].insert(static_cast<uint32_t>(v.toInt()));
        }
    }

    for (const QJsonValue& value : list) {
        if (static_cast<int>(m_scenes.size()) >= kMaxScenes) break;
        const QJsonObject sceneObj = value.toObject();
        Scene scene;
        const int index = static_cast<int>(m_scenes.size());
        scene.name = sceneObj["name"].toString(QString("Scene %1").arg(index + 1));
        scene.color = sceneObj["color"].toString(defaultColor(index));
        scene.levelDb = std::clamp(static_cast<float>(sceneObj["levelDb"].toDouble(0.0)), -24.0f, 12.0f);
        const QJsonObject blocks = sceneObj["blocks"].toObject();
        for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
            scene.bypass[it.key().toStdString()] = it.value().toObject()["bypassed"].toBool();
        }
        const QJsonObject params = sceneObj["params"].toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const QJsonObject values = it.value().toObject();
            for (auto v = values.constBegin(); v != values.constEnd(); ++v) {
                bool ok = false;
                const uint32_t paramIndex = v.key().toUInt(&ok);
                if (ok && isAssigned(it.key().toStdString(), paramIndex)) {
                    scene.params[it.key().toStdString()][paramIndex] = static_cast<float>(v.value().toDouble());
                }
            }
        }
        m_scenes.push_back(scene);
    }

    m_active = std::clamp(obj["active"].toInt(0), 0, count() - 1);
    prune(live);
    return true;
}
