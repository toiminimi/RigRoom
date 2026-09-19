#pragma once
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Snapshots inside one board preset (Helix/Quad Cortex style).
// Every block's on/off state is per scene. Parameters only vary per scene
// once they are assigned; everything else stays global to the preset.
// Switching scenes never touches the graph, so tails ring on.
class SceneModel {
public:
    static constexpr int kMaxScenes = 8;

    using ParamMap = std::map<uint32_t, float>;

    // Live values of the board, as read from the canvas.
    struct BoardState {
        std::map<std::string, bool> bypass;              // node id -> bypassed
        std::map<std::string, ParamMap> params;          // node id -> index -> value
    };

    struct Scene {
        QString name;
        QString color;
        float levelDb = 0.0f;
        std::map<std::string, bool> bypass;
        std::map<std::string, ParamMap> params;          // assigned params only
    };

    // Changes needed to move the live board to a scene.
    struct Changes {
        std::vector<std::pair<std::string, bool>> bypass;
        std::vector<std::tuple<std::string, uint32_t, float>> params;
        float levelDb = 0.0f;
        bool empty() const { return bypass.empty() && params.empty(); }
    };

    SceneModel();

    // One default scene holding the live state.
    void reset(const BoardState& live);

    int count() const { return static_cast<int>(m_scenes.size()); }
    int activeIndex() const { return m_active; }
    const Scene& scene(int index) const { return m_scenes.at(index); }
    const Scene& active() const { return m_scenes.at(m_active); }
    static QString defaultColor(int index);
    struct NamedColor { const char* name; const char* hex; };
    // Curated scene colours, readable on the dark UI.
    static const std::vector<NamedColor>& palette();

    // Stores the live board into the active scene (bypass + assigned params).
    void captureActive(const BoardState& live);
    // Differences between the live board and scene `index`.
    Changes changesTo(int index, const BoardState& live) const;
    // Captures the active scene, marks `index` active and returns what to apply.
    Changes switchTo(int index, const BoardState& live);

    // Adds a copy of the active scene after the last one. Returns its index or -1.
    int addScene(const BoardState& live);
    int duplicateScene(int index, const BoardState& live);
    bool removeScene(int index);
    void renameScene(int index, const QString& name);
    void setSceneColor(int index, const QString& color);
    void setSceneLevel(int index, float db);
    void setActiveLevel(float db) { setSceneLevel(m_active, db); }
    // Copies the live board into scene `index` (not only the active one).
    void overwriteScene(int index, const BoardState& live);

    bool isAssigned(const std::string& nodeId, uint32_t index) const;
    bool hasAssignedParams(const std::string& nodeId) const;
    // Seeds every scene with `value`.
    void assignParam(const std::string& nodeId, uint32_t index, float value);
    void unassignParam(const std::string& nodeId, uint32_t index);
    const std::map<std::string, std::set<uint32_t>>& assignedParams() const { return m_assigned; }

    // Blocks a scene switch can change: an assigned parameter, or an on/off
    // state that differs between scenes. The active scene uses live values.
    std::set<std::string> sceneControlledBlocks(const BoardState& live) const;

    // Drops data for blocks that no longer exist.
    void prune(const BoardState& live);

    QJsonObject toJson() const;
    // Returns false (and resets from `live`) when the object has no usable scenes.
    bool fromJson(const QJsonObject& obj, const BoardState& live);

private:
    void captureInto(Scene& scene, const BoardState& live) const;

    std::vector<Scene> m_scenes;
    int m_active = 0;
    std::map<std::string, std::set<uint32_t>> m_assigned;
};
