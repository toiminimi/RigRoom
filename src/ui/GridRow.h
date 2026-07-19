#pragma once
#include <vector>
#include <memory>
#include "../audio/AudioNode.h"

// A row is an ordered sequence of plugin stages (up to 12 slots per row).
// "plugins" avoids the Qt reserved word "slots".
struct GridRow {
    std::vector<std::shared_ptr<AudioNode>> plugins; // indexed 0..NUM_COLS-1
    int splitCol = -1; // -1 means System Input, otherwise column index of row 1 node to split from
    int mergeCol = -1; // -1 means System Output, otherwise column index of row 1 node to merge before
    float mix = 1.0f;  // Path level (0.0 to 1.0)
    float pan = 0.0f;  // Stereo balance (-1.0 left to 1.0 right)
    bool enabled = false;
    bool levelConfigured = false;
    
    GridRow() {
        plugins.resize(12, nullptr);
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
