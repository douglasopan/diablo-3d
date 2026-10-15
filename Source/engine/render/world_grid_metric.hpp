#pragma once

namespace devilution {
// Native cells and save coordinates stay authoritative. Physical actor/camera
// dimensions convert at the presentation boundary; environments stay in cells.
// Fixed for this executable, so no old frame can outlive a metric change.
inline constexpr float WorldMetresPerTile = 1.62F;
inline constexpr float PhysicalToNativeGrid(float metres) { return metres / WorldMetresPerTile; }
} // namespace devilution
