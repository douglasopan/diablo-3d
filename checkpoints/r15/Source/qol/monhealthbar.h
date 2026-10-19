/**
 * @file monhealthbar.h
 *
 * Adds monster health bar QoL feature
 */
#pragma once

namespace devilution {

struct Surface;
struct Monster;
// Read-only HUD focus: combat observers never mutate simulation or saves.
void RecordMonsterHealthInteraction(const Monster &monster);
void ResetMonsterHealthTarget();
void ForgetMonsterHealthTarget(const Monster &monster);
int GetTrackedMonsterHealthTarget();

void InitMonsterHealthBar();
void FreeMonsterHealthBar();

void DrawMonsterHealthBar(const Surface &out);

} // namespace devilution
