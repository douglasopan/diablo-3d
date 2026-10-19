/**
 * @file itemlabels.h
 *
 * Adds item labels QoL feature
 */
#pragma once

#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

void ToggleItemLabelHighlight();
void HighlightKeyPressed(bool pressed);
bool IsItemLabelHighlighted();
void ResetItemlabelHighlighted();
bool IsHighlightingLabelsEnabled();
void AddItemToLabelQueue(int id, Point position);
void DrawItemNameLabels(const Surface &out);
/** Fresh hit test against the final label boxes of the current 3D frame.
 * Updates native item selection only for an eligible absolute UI pointer. */
bool SelectProjectedItemLabelAt(Point pointer);

} // namespace devilution
