#pragma once
#include <vector>
#include <memory>
#include <string>
#include "../audio/AudioNode.h"

// Most columns a board can have (NodeCanvas::MAX_COLS).
inline constexpr int kMaxGridColumns = 16;

// A row is an ordered sequence of plugin stages (up to kMaxGridColumns slots per row).
// "plugins" avoids the Qt reserved word "slots".
struct GridRow {
    enum class SplitMode { Copy, AB };

    std::vector<std::shared_ptr<AudioNode>> plugins; // indexed 0..NUM_COLS-1
    // Grid gaps are the canonical routing topology. IDs are refreshed from the
    // effective adjacent nodes for display and legacy-preset compatibility.
    std::string splitAfterNodeId;
    std::string mergeBeforeNodeId;
    int splitCol = -1;
    int mergeCol = -1;
    int parentRow = 1;
    bool hasSplitSection = false;
    SplitMode splitMode = SplitMode::Copy;
    float splitPosition = 0.0f; // -1 main/A, +1 branch/B
    bool mainInputEnabled = true;
    float mainMix = 1.0f;
    float mix = 1.0f;  // Path level (0.0 to 2.0)
    float pan = 0.0f;  // Stereo balance (-1.0 left to 1.0 right)
    bool enabled = false;
    bool levelConfigured = false;
    bool polarityInverted = false;
    std::string name;
    double parentSplitX = 0.0;
    double parentMergeX = 0.0;
    
    GridRow() {
        plugins.resize(kMaxGridColumns, nullptr);
    }
    
    bool isEmpty() const {
        for (const auto& s : plugins) if (s) return false;
        return true;
    }
    
    int count() const {
        int c = 0;
        for (const auto& s : plugins) if (s) ++c;
        return c;
    }
    
    // Indices of occupied columns
    std::vector<int> occupiedCols() const {
        std::vector<int> v;
        for (int i = 0; i < (int)plugins.size(); ++i)
            if (plugins[i]) v.push_back(i);
        return v;
    }
};
