#pragma once

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

#include "engine/render/town_camera_gameplay.hpp"

namespace devilution {

/** Prepared pure checks. The principal supplies its check(bool, const char*)
 * lambda and links town_camera.cpp; no main, SDL, player, assets or live profile.
 * Preparing this header is not evidence that its checks were executed. */
template <typename Check>
void RunTownCameraGameplayChecks(Check &&check)
{
	const auto near = [](float actual, float expected) {
		return std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= 0.00001F;
	};
	const auto samePoint = [&near](TownCameraPoint actual, TownCameraPoint expected) {
		return near(actual.x, expected.x) && near(actual.height, expected.height) && near(actual.z, expected.z);
	};
	check(static_cast<int>(TownCameraMode::Isometric) == 0
	        && static_cast<int>(TownCameraMode::FreeOrbit) == 1
	        && static_cast<int>(TownCameraMode::ThirdPerson) == 2
	        && static_cast<int>(TownCameraMode::FirstPerson) == 3,
	    "gameplay policy preserves all four historical enum IDs");
	constexpr std::array<int, 10> savedValues {
		std::numeric_limits<int>::min(), -1, 0, 1, 2, 3, 4, 255, 256, std::numeric_limits<int>::max()
	};
	for (const int saved : savedValues) {
		const auto expected = saved == 3 ? TownCameraMode::FirstPerson : TownCameraMode::ThirdPerson;
		check(NormalizeTownGameplayCameraMode(saved) == expected,
		    "saved legacy or hostile integer migrates before narrowing; valid TPP/FPP remains stable");
	}
	check(NextTownGameplayCameraMode(TownCameraMode::ThirdPerson) == TownCameraMode::FirstPerson,
	    "gameplay cycle advances third person to first person");
	check(NextTownGameplayCameraMode(TownCameraMode::FirstPerson) == TownCameraMode::ThirdPerson,
	    "gameplay cycle advances first person to third person");
	for (const auto mode : { TownCameraMode::Isometric, TownCameraMode::FreeOrbit, static_cast<TownCameraMode>(255) }) {
		check(!IsTownGameplayCameraMode(mode) && NextTownGameplayCameraMode(mode) == TownCameraMode::ThirdPerson,
		    "diagnostic or invalid mode cannot enter the gameplay cycle as a third selectable mode");
	}
	check(IsTownGameplayCameraMode(TownCameraMode::ThirdPerson) && IsTownGameplayCameraMode(TownCameraMode::FirstPerson),
	    "both follow modes remain gameplay modes");

	const TownCameraPreferences diagnosticDefaults;
	const auto fallback = ConfigureTownGameplayEyeHeight(diagnosticDefaults);
	check(near(fallback.eyeHeight, 1.7F) && near(fallback.firstPersonEyeHeight, 1.7F),
	    "uncalibrated caller receives one 1.7 standing eye anchor");
	check(near(diagnosticDefaults.eyeHeight, 1.1F) && near(diagnosticDefaults.firstPersonEyeHeight, 1.7F),
	    "gameplay calibration leaves diagnostic defaults unmodified");
	for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		const auto preferences = ConfigureTownGameplayEyeHeight(diagnosticDefaults, invalid);
		check(near(preferences.eyeHeight, 1.7F) && near(preferences.firstPersonEyeHeight, 1.7F),
		    "nonfinite authored height uses the finite common fallback");
	}
	for (const float low : { -100.0F, 0.0F, 0.39F }) {
		const auto preferences = ConfigureTownGameplayEyeHeight(diagnosticDefaults, low);
		check(near(preferences.eyeHeight, 0.4F) && near(preferences.firstPersonEyeHeight, 0.4F),
		    "finite low authored height clamps to the rig minimum in both modes");
	}
	for (const float high : { 2.01F, 100.0F }) {
		const auto preferences = ConfigureTownGameplayEyeHeight(diagnosticDefaults, high);
		check(near(preferences.eyeHeight, 2.0F) && near(preferences.firstPersonEyeHeight, 2.0F),
		    "finite high authored height clamps to the rig maximum in both modes");
	}
	TownCameraPreferences authored = diagnosticDefaults;
	authored.verticalFovDegrees = 74;
	authored.nearClip = 0.2F;
	authored.farClip = 256;
	authored.orbitRadiansPerPixel = 0.012F;
	authored.zoomExponentPerStep = 0.2F;
	const auto calibrated = ConfigureTownGameplayEyeHeight(authored, 1.83F);
	check(near(calibrated.eyeHeight, 1.83F) && near(calibrated.firstPersonEyeHeight, 1.83F)
	        && calibrated.verticalFovDegrees == authored.verticalFovDegrees
	        && calibrated.nearClip == authored.nearClip && calibrated.farClip == authored.farClip
	        && calibrated.orbitRadiansPerPixel == authored.orbitRadiansPerPixel
	        && calibrated.zoomExponentPerStep == authored.zoomExponentPerStep,
	    "caller calibration retains FOV, clipping, sensitivity and zoom preferences");

	TownCameraRig diagnostic;
	std::array<TownCameraPose, 4> savedPoses;
	for (unsigned index = 0; index < savedPoses.size(); ++index) {
		const auto mode = static_cast<TownCameraMode>(index);
		check(diagnostic.SetMode(mode), "all four diagnostic modes remain accepted by the pure rig");
		diagnostic.SetPose({ 0.1F * (index + 1), 0.5F, 7.0F + index, {} });
		savedPoses[index] = diagnostic.pose();
	}
	for (unsigned index = 0; index < savedPoses.size(); ++index) {
		diagnostic.SetMode(static_cast<TownCameraMode>(index));
		const auto &saved = savedPoses[index];
		check(near(diagnostic.pose().yaw, saved.yaw) && near(diagnostic.pose().pitch, saved.pitch)
		        && near(diagnostic.pose().distance, saved.distance) && samePoint(diagnostic.pose().pan, saved.pan),
		    "diagnostic modes retain their independent saved poses");
	}
	diagnostic.RestoreIsometric();
	check(diagnostic.mode() == TownCameraMode::Isometric && near(diagnostic.pose().distance, 22)
	        && near(diagnostic.preferences().eyeHeight, 1.1F),
	    "native diagnostic reset retains the original isometric contract");

	TownCameraRig rig;
	rig.SetPreferences(calibrated);
	rig.SetMode(TownCameraMode::ThirdPerson);
	const TownCameraPose pose { 0.65F, -0.35F, 4, {} };
	rig.SetPose(pose);
	const TownCameraPoint anchor { 10, 0.25F, 20 };
	const TownCameraPoint head { anchor.x, anchor.height + 1.83F, anchor.z };
	const float cp = std::cos(pose.pitch), sp = std::sin(pose.pitch);
	const TownCameraPoint outward { std::cos(pose.yaw) * cp, sp, std::sin(pose.yaw) * cp };
	const auto thirdFrame = BuildTownCameraFrame(rig, anchor, 640, 480, 320, 240);
	check(thirdFrame.valid && samePoint(thirdFrame.eye, head + outward * 4),
	    "third-person eye lies on the analytical ray from the caller-authored head anchor");
	check(samePoint(TownCameraToView(thirdFrame, head), { 0, 0, 4 }),
	    "shared head anchor lies on the center ray at exactly the third-person boom depth");
	rig.SetMode(TownCameraMode::FirstPerson);
	rig.SetPose(pose);
	const auto firstFrame = BuildTownCameraFrame(rig, anchor, 640, 480, 320, 240);
	check(firstFrame.valid && samePoint(firstFrame.eye, head) && samePoint(firstFrame.forward, thirdFrame.forward),
	    "first person reaches the same head anchor without changing the view direction");

	rig.SetMode(TownCameraMode::ThirdPerson);
	rig.SetPose({ pose.yaw, pose.pitch, 0.61F, {} });
	rig.Zoom(0.01F);
	check(rig.mode() == TownCameraMode::ThirdPerson && rig.pose().distance > 0.6F,
	    "positive wheel above the entry threshold remains third person");
	rig.Zoom(0.25F);
	check(rig.mode() == TownCameraMode::FirstPerson && rig.pose().distance == 0,
	    "positive wheel crossing 0.6 enters first person");
	check(near(rig.pose().yaw, pose.yaw) && near(rig.pose().pitch, pose.pitch)
	        && near(rig.VisualEyeHeight(), 1.83F),
	    "wheel entry retains yaw, upward pitch and the same authored head height");
	rig.AdvanceVisual(0.025F);
	const float displayed = rig.VisualDistance();
	const auto beforeReverse = BuildTownCameraFrame(rig, anchor, 640, 480, 320, 240);
	rig.Zoom(-0.001F);
	const auto afterReverse = BuildTownCameraFrame(rig, anchor, 640, 480, 320, 240);
	check(rig.mode() == TownCameraMode::ThirdPerson && rig.pose().distance >= 1.0F,
	    "any negative first-person wheel exits with a third-person target of at least one unit");
	check(near(rig.VisualDistance(), displayed) && near(rig.VisualEyeHeight(), 1.83F)
	        && samePoint(afterReverse.eye, beforeReverse.eye) && samePoint(afterReverse.forward, beforeReverse.forward),
	    "reversing a live blend preserves its displayed eye without a height or pose jump");
	check(near(rig.pose().yaw, pose.yaw) && near(rig.pose().pitch, pose.pitch),
	    "wheel exit preserves yaw and negative pitch");
	for (unsigned frame = 0; frame < 200; ++frame)
		rig.AdvanceVisual(0.1F);
	check(!rig.IsVisualTransitionActive() && near(rig.VisualDistance(), rig.pose().distance)
	        && near(rig.VisualEyeHeight(), 1.83F),
	    "visual interpolation converges to the authored target while remaining on one head axis");
	const auto revision = rig.revision();
	for (const float ignored : { 0.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
		rig.Zoom(ignored);
	check(rig.revision() == revision, "zero or nonfinite wheel cannot mutate a rig pose or revision");
	rig.SetMode(TownCameraMode::FirstPerson);
	const auto firstRevision = rig.revision();
	rig.Zoom(1);
	check(rig.mode() == TownCameraMode::FirstPerson && rig.revision() == firstRevision,
	    "positive wheel cannot move beyond the first-person eye endpoint");
	rig.Suspend(true);
	const auto suspendedRevision = rig.revision();
	rig.Zoom(-1);
	check(rig.mode() == TownCameraMode::FirstPerson && rig.revision() == suspendedRevision,
	    "suspension prevents delayed wheel exit while preserving the intended follow mode");
	(void)rig.VisualDistance();
	(void)rig.VisualEyeHeight();
	(void)rig.IsVisualTransitionActive();
	check(rig.revision() == suspendedRevision, "visual getters are read-only and cannot advance the camera");
}

} // namespace devilution
