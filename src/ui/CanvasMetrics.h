#pragma once
#include <QtGlobal>

// Visual-only geometry for the fixed signal-path canvas. Grid indices stay in GridRow.
namespace CanvasMetrics {
inline constexpr qreal cardWidth = 120.0, cardHeight = 50.0, columnPitch = 136.0;
inline constexpr qreal clearGap = 16.0, laneHeight = 128.0;
inline constexpr qreal junctionWidth = 68.0, junctionHeight = 26.0;
inline constexpr qreal splitToolWidth = 72.0, splitToolHeight = 28.0;
inline constexpr qreal marginX = 16.0, marginY = 28.0;
constexpr qreal columnX(qreal trackLeft, int column) { return trackLeft + column * columnPitch; }
constexpr qreal gapX(qreal trackLeft, int gap, int columns) {
    return gap <= 0 ? trackLeft - clearGap / 2.0 : gap >= columns ? columnX(trackLeft, columns - 1) + cardWidth + clearGap / 2.0 : columnX(trackLeft, gap) - clearGap / 2.0;
}
constexpr qreal requiredWidth(int columns, qreal systemWidth = cardWidth) {
    return 2 * marginX + 2 * systemWidth + 2 * clearGap + (columns - 1) * columnPitch + cardWidth;
}
constexpr qreal requiredHeight(int activeLanes = 5, qreal tallestCard = cardHeight) {
    return 2 * marginY + tallestCard + (activeLanes - 1) * laneHeight;
}
}
