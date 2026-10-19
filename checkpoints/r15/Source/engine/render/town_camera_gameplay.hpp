#pragma once

#include <algorithm>
#include <cmath>

#include "town_camera.hpp"

namespace devilution {

/** Gameplay preferences retain their historical persisted IDs. The four-mode
 * rig and its poses remain available to rendering diagnostics. */
inline constexpr bool IsTownGameplayCameraMode(TownCameraMode mode)
{
	return mode == TownCameraMode::ThirdPerson || mode == TownCameraMode::FirstPerson;
}

/** Validate the integer before any narrowing to the underlying uint8_t enum. */
inline constexpr TownCameraMode NormalizeTownGameplayCameraMode(int saved)
{
	return saved == static_cast<int>(TownCameraMode::FirstPerson)
	    ? TownCameraMode::FirstPerson : TownCameraMode::ThirdPerson;
}

/** A diagnostic/invalid starting mode returns to third-person gameplay. */
inline constexpr TownCameraMode NextTownGameplayCameraMode(TownCameraMode mode)
{
	return mode == TownCameraMode::ThirdPerson
	    ? TownCameraMode::FirstPerson : TownCameraMode::ThirdPerson;
}

/** The caller may supply a calibrated standing eye height for its selected
 * class/body. This module never guesses anatomy or reads player/animation data.
 * Both follow modes share one authored eye anchor; only the boom distance changes.
 * Diagnostic defaults and the caller's other projection preferences are retained. */
inline TownCameraPreferences ConfigureTownGameplayEyeHeight(TownCameraPreferences preferences, float eyeHeight = 1.7F)
{
	const float height = std::isfinite(eyeHeight) ? std::clamp(eyeHeight, 0.4F, 2.0F) : 1.7F;
	preferences.eyeHeight = height;
	preferences.firstPersonEyeHeight = height;
	return preferences;
}

} // namespace devilution
