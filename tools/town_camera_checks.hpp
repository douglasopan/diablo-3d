#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>

#include "engine/render/town_camera.hpp"

namespace devilution {

/** Pure camera-contract checks. No assets, game state, SDL, save or RNG access. */
inline bool RunTownCameraChecks(std::ostream &out, size_t &checks)
{
	size_t failures = 0;
	const size_t firstCheck = checks;
	const auto check = [&](bool condition, const std::string &message) {
		++checks;
		if (!condition) {
			++failures;
			out << "FAIL camera: " << message << '\n';
		}
	};
	const auto close = [](float actual, float expected, float tolerance = 0.0001F) {
		return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
	};
	const auto samePoint = [&](TownCameraPoint actual, TownCameraPoint expected, float tolerance = 0.0001F) {
		return close(actual.x, expected.x, tolerance) && close(actual.height, expected.height, tolerance)
		    && close(actual.z, expected.z, tolerance);
	};
	const auto samePose = [&](const TownCameraPose &actual, const TownCameraPose &expected) {
		return close(actual.yaw, expected.yaw) && close(actual.pitch, expected.pitch)
		    && close(actual.distance, expected.distance) && samePoint(actual.pan, expected.pan);
	};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();

	// These deltas come directly from the native [32,-32;16,16] tile transform.
	TownCameraRig nativeRig;
	const TownCameraFrame native = BuildTownCameraFrame(nativeRig, {}, 640, 480, 320, 240);
	TownCameraProjectedVertex origin, tileX, tileZ, elevated;
	check(native.valid && !native.perspective, "native frame remains orthographic");
	check(ProjectTownCameraPoint(native, {}, origin), "native anchor projects");
	check(ProjectTownCameraPoint(native, { 1, 0, 0 }, tileX), "native X tile projects");
	check(ProjectTownCameraPoint(native, { 0, 0, 1 }, tileZ), "native Z tile projects");
	check(ProjectTownCameraPoint(native, { 0, 1, 0 }, elevated), "native world height projects");
	check(close(origin.x, 320, 0.002F) && close(origin.y, 240, 0.002F), "native anchor uses supplied viewport center");
	check(close(tileX.x - origin.x, 32, 0.002F) && close(tileX.y - origin.y, 16, 0.002F), "native X tile delta is (32,16)");
	check(close(tileZ.x - origin.x, -32, 0.002F) && close(tileZ.y - origin.y, 16, 0.002F), "native Z tile delta is (-32,16)");
	check(close(elevated.x - origin.x, 0, 0.002F) && close(elevated.y - origin.y, -32, 0.002F), "native unit height delta is (0,-32)");
	const TownCameraFrame native2x = BuildTownCameraFrame(nativeRig, {}, 1280, 960, 640, 480, 2);
	TownCameraProjectedVertex native2xX;
	check(ProjectTownCameraPoint(native2x, { 1, 0, 0 }, native2xX)
	        && close(native2xX.x, 704, 0.004F) && close(native2xX.y, 512, 0.004F),
	    "native zoom doubles world scale and uses the supplied logical anchor");
	check(close(origin.depth, 256, 0.002F) && close(TownCameraFogDepth(native, 256), 22),
	    "native anchor fog distance is twenty-two instead of the artificial eye distance");
	check(close(TownCameraFogDepth(native, 276), 42), "twenty units behind the native anchor add twenty fog units");
	check(close(TownCameraFogDepth(native, 100), 0), "orthographic fog distance cannot become negative");
	TownCameraFrame nativeWithoutFogOffset = native;
	nativeWithoutFogOffset.fogDepthOffset = 0;
	check(close(NormalizeTownCameraDepth(native, 256), NormalizeTownCameraDepth(nativeWithoutFogOffset, 256)),
	    "fog offset does not alter normalized raster depth");
	TownCameraProjectedVertex unfoggedOrigin;
	check(ProjectTownCameraPoint(nativeWithoutFogOffset, {}, unfoggedOrigin)
	        && close(unfoggedOrigin.depth, origin.depth) && close(unfoggedOrigin.reciprocalW, origin.reciprocalW)
	        && close(unfoggedOrigin.x, origin.x) && close(unfoggedOrigin.y, origin.y),
	    "fog offset does not change projected coverage or raw depth used by selection");
	check(std::isnan(TownCameraFogDepth(native, nan)) && std::isnan(TownCameraFogDepth(native, infinity))
	        && std::isnan(TownCameraFogDepth(TownCameraFrame {}, 256)), "fog depth rejects nonfinite distances and invalid frames");

	// A hand-authored identity basis isolates projection from rig construction.
	TownCameraFrame analytical;
	analytical.right = { 1, 0, 0 };
	analytical.up = { 0, 1, 0 };
	analytical.forward = { 0, 0, 1 };
	analytical.focalPixels = 100;
	analytical.centerX = analytical.centerY = 100;
	analytical.nearClip = 1;
	analytical.farClip = 10;
	analytical.width = analytical.height = 200;
	analytical.valid = true;
	TownCameraProjectedVertex nearSize, farSize;
	check(ProjectTownCameraPoint(analytical, { 0.25F, 0.25F, 2 }, nearSize)
	        && ProjectTownCameraPoint(analytical, { 0.25F, 0.25F, 4 }, farSize), "orthographic depth fixtures project");
	check(close(nearSize.x, 125) && close(nearSize.y, 75) && close(farSize.x, 125) && close(farSize.y, 75),
	    "orthographic size does not change with depth");
	analytical.perspective = true;
	check(ProjectTownCameraPoint(analytical, { 0.25F, 0.25F, 2 }, nearSize)
	        && ProjectTownCameraPoint(analytical, { 0.25F, 0.25F, 4 }, farSize), "perspective depth fixtures project");
	check(close(nearSize.x, 112.5F) && close(nearSize.y, 87.5F)
	        && close(farSize.x, 106.25F) && close(farSize.y, 93.75F), "doubling perspective depth halves projected displacement");
	check(close(nearSize.reciprocalW, 0.5F) && close(farSize.reciprocalW, 0.25F), "perspective publishes reciprocal view depth");

	TownCameraRig eyeRig;
	eyeRig.SetMode(TownCameraMode::FirstPerson);
	eyeRig.SetPose({ 0, 0, 0, {} });
	TownCameraPreferences eyePreferences;
	eyePreferences.verticalFovDegrees = 90;
	eyePreferences.firstPersonEyeHeight = 1.4F;
	eyeRig.SetPreferences(eyePreferences);
	const TownCameraFrame eye = BuildTownCameraFrame(eyeRig, { 10, 2, 20 }, 320, 180, 160, 90);
	check(eye.valid && eye.perspective && close(eye.heightScale, 1), "first person uses perspective without native height compression");
	check(samePoint(eye.eye, { 10, 3.4F, 20 }), "first person eye is at anchor plus configured eye height");
	check(samePoint(eye.forward, { -1, 0, 0 }) && samePoint(eye.up, { 0, 1, 0 }), "first person yaw zero looks along negative world X");
	check(close(eye.focalPixels, 90), "90 degree vertical FOV spans twice the half viewport height");
	check(close(eye.fogDepthOffset, 0) && close(TownCameraFogDepth(eye, 3), 3),
	    "first person fog uses the physical view depth without a legacy offset");
	const TownCameraRay eyeCenter = TownCameraScreenRay(eye, 160, 90);
	check(eyeCenter.valid && samePoint(eyeCenter.origin, { 10, 3.4F, 20 })
	        && samePoint(eyeCenter.direction, { -1, 0, 0 }), "first person center ray starts at the physical eye");
	const TownCameraRay eyeRight = TownCameraScreenRay(eye, 250, 90);
	const float inverseSqrt2 = 0.7071067811865475F;
	check(eyeRight.valid && samePoint(eyeRight.direction, { -inverseSqrt2, 0, -inverseSqrt2 }), "90 degree FOV right ray has equal forward and lateral components");

	TownCameraRig heightRig;
	const TownCameraPoint heightAnchor { 10, 2, 20 };
	check(close(heightRig.preferences().eyeHeight, 1.1F)
	        && close(heightRig.preferences().firstPersonEyeHeight, 1.7F),
	    "separate defaults preserve the third-person target and calibrate the first-person eye");
	heightRig.SetMode(TownCameraMode::FirstPerson);
	heightRig.SetPose({ 0, 0, 23, { 4, 9, -3 } });
	const TownCameraFrame defaultHeightFrame = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
	check(defaultHeightFrame.valid && samePoint(defaultHeightFrame.eye, { 10, 3.7F, 20 })
	        && close(heightRig.pose().distance, 0) && samePoint(heightRig.pose().pan, {}),
	    "first-person default eye is 1.7 above the anchor with no orbit or pan displacement");
	TownCameraProjectedVertex equalHeightPoint, groundHeightPoint;
	check(ProjectTownCameraPoint(defaultHeightFrame, { 6, 3.7F, 20 }, equalHeightPoint)
	        && close(equalHeightPoint.x, 160) && close(equalHeightPoint.y, 90),
	    "a point four units ahead at the default eye height lies on the horizontal center ray");
	check(ProjectTownCameraPoint(defaultHeightFrame, { 6, 2, 20 }, groundHeightPoint)
	        && close(groundHeightPoint.y, 90 + defaultHeightFrame.focalPixels * 1.7F / 4),
	    "ground displacement follows focal length times physical eye height over depth");

	heightRig.SetMode(TownCameraMode::ThirdPerson);
	heightRig.SetPose({ 0, 0.3F, 5, {} });
	const TownCameraFrame thirdHeightBaseline = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
	check(thirdHeightBaseline.valid && samePoint(thirdHeightBaseline.eye,
	          { 10 + 5 * std::cos(0.3F), 3.1F + 5 * std::sin(0.3F), 20 }),
	    "third-person baseline still orbits the existing 1.1-high target");
	TownCameraPreferences heightPreferences = heightRig.preferences();
	heightPreferences.firstPersonEyeHeight = 2;
	heightRig.SetPreferences(heightPreferences);
	const TownCameraFrame thirdHeightAfterFppChange = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
	check(samePoint(thirdHeightAfterFppChange.eye, thirdHeightBaseline.eye)
	        && samePoint(thirdHeightAfterFppChange.forward, thirdHeightBaseline.forward)
	        && close(thirdHeightAfterFppChange.focalPixels, thirdHeightBaseline.focalPixels),
	    "changing only first-person eye height leaves the third-person frame unchanged");

	heightPreferences.eyeHeight = 0.8F;
	heightPreferences.firstPersonEyeHeight = 1.4F;
	heightRig.SetPreferences(heightPreferences);
	heightRig.SetMode(TownCameraMode::FirstPerson);
	const TownCameraFrame customHeightFrame = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
	check(customHeightFrame.valid && samePoint(customHeightFrame.eye, { 10, 3.4F, 20 }),
	    "first person uses its custom 1.4 eye height independently of the third-person target height");
	heightRig.SetMode(TownCameraMode::ThirdPerson);
	const TownCameraFrame customThirdHeightFrame = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
	check(customThirdHeightFrame.valid && samePoint(customThirdHeightFrame.eye,
	          { 10 + 5 * std::cos(0.3F), 2.8F + 5 * std::sin(0.3F), 20 }),
	    "third person still honors its independent custom target height");

	heightRig.SetMode(TownCameraMode::FirstPerson);
	for (const float invalidFirstHeight : { nan, infinity, -infinity }) {
		heightPreferences.firstPersonEyeHeight = invalidFirstHeight;
		heightRig.SetPreferences(heightPreferences);
		const TownCameraFrame fallbackHeightFrame = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
		check(close(heightRig.preferences().firstPersonEyeHeight, 1.7F)
		        && fallbackHeightFrame.valid && samePoint(fallbackHeightFrame.eye, { 10, 3.7F, 20 }),
		    "nonfinite first-person eye height falls back to a finite 1.7 above the anchor");
	}
	for (const std::array<float, 2> heightClamp : { std::array<float, 2> { -10, 0.4F }, std::array<float, 2> { 10, 2 } }) {
		heightPreferences.firstPersonEyeHeight = heightClamp[0];
		heightRig.SetPreferences(heightPreferences);
		const TownCameraFrame clampedHeightFrame = BuildTownCameraFrame(heightRig, heightAnchor, 320, 180, 160, 90);
		check(close(heightRig.preferences().firstPersonEyeHeight, heightClamp[1])
		        && clampedHeightFrame.valid && samePoint(clampedHeightFrame.eye, { 10, 2 + heightClamp[1], 20 }),
		    "finite first-person eye height is bounded to 0.4 through 2 without moving the floor anchor");
	}

	heightPreferences.firstPersonEyeHeight = 1.7F;
	heightRig.SetPreferences(heightPreferences);
	heightRig.SetPose({ -1.2F, -0.8F, 17, { 5, 4, 3 } });
	const TownCameraFrame highAnchorHeightFrame = BuildTownCameraFrame(heightRig, { 31, 17.25F, 44 }, 960, 540, 480, 270);
	check(highAnchorHeightFrame.valid && samePoint(highAnchorHeightFrame.eye, { 31, 18.95F, 44 }),
	    "first-person height adds to an elevated anchor without a yaw, pitch, viewport or orbit offset");

	// Round trips include an offset anchor, pitched basis and legacy height scale.
	TownCameraRig roundTripRig;
	for (const TownCameraMode mode : { TownCameraMode::Isometric, TownCameraMode::FreeOrbit,
	         TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson }) {
		roundTripRig.SetMode(mode);
		roundTripRig.SetPose({ -0.9F, 0.41F, 13, { 2, 0, -3 } });
		const TownCameraFrame frame = BuildTownCameraFrame(roundTripRig, { 37, 0.7F, 58 }, 960, 540, 430, 230);
		for (const TownCameraPoint point : { TownCameraPoint { 36, 1.2F, 57 }, TownCameraPoint { -2, 7, 31 }, TownCameraPoint { 76, -1, 88 } }) {
			check(samePoint(TownCameraToWorld(frame, TownCameraToView(frame, point)), point, 0.0002F), "world/view round trip preserves authored coordinates");
			check(samePoint(TownCameraToView(frame, TownCameraToWorld(frame, point)), point, 0.0002F), "view/world round trip preserves view coordinates");
		}
	}

	const float rayLength = std::sqrt(1.3125F);
	TownCameraRay ray = TownCameraScreenRay(analytical, 150, 75);
	check(ray.valid && samePoint(ray.origin, {})
	        && samePoint(ray.direction, { 0.5F / rayLength, 0.25F / rayLength, 1 / rayLength }), "analytical perspective ray matches screen slopes");
	TownCameraProjectedVertex rayProjection;
	check(ProjectTownCameraPoint(analytical, ray.origin + ray.direction * 3, rayProjection)
	        && close(rayProjection.x, 150) && close(rayProjection.y, 75), "point on perspective ray projects to its original pixel");
	const TownCameraRay pixelCenterRay = TownCameraScreenRay(analytical, 150.5F, 75.5F);
	const float pixelCenterLength = std::sqrt(1 + 0.505F * 0.505F + 0.245F * 0.245F);
	check(pixelCenterRay.valid && samePoint(pixelCenterRay.direction,
	          { 0.505F / pixelCenterLength, 0.245F / pixelCenterLength, 1 / pixelCenterLength }),
	    "raster sample ray uses the supplied pixel center without a hidden half-pixel offset");
	TownCameraFrame orthographic = analytical;
	orthographic.perspective = false;
	ray = TownCameraScreenRay(orthographic, 150, 75);
	check(ray.valid && samePoint(ray.origin, { 0.5F, 0.25F, 0 }) && samePoint(ray.direction, { 0, 0, 1 }),
	    "orthographic ray moves its origin and keeps parallel direction");
	const TownCameraRay otherOrthographicRay = TownCameraScreenRay(orthographic, 25, 150);
	check(otherOrthographicRay.valid && samePoint(otherOrthographicRay.direction, ray.direction)
	        && !samePoint(otherOrthographicRay.origin, ray.origin), "orthographic rays at different pixels remain parallel");

	// Each case has one outside vertex and two inside vertices. Intersections
	// lie one third along either edge, except the far-plane case (one half).
	for (const bool perspective : { false, true }) {
		TownCameraFrame frame = analytical;
		frame.perspective = perspective;
		const float side = perspective ? 2.0F : 1.0F;
		struct ClipCase {
			const char *name;
			TownCameraPoint outside, b, c;
			float t;
		};
		const std::array<ClipCase, 6> cases { {
			{ "near", { 0, 0, 0.5F }, { -0.2F, -0.2F, 2 }, { 0.2F, -0.2F, 2 }, 1.0F / 3 },
			{ "far", { 0, 0, 12 }, { -0.2F, -0.2F, 8 }, { 0.2F, -0.2F, 8 }, 0.5F },
			{ "left", { -1.5F * side, 0, 2 }, { 0, -0.5F, 2 }, { 0, 0.5F, 2 }, 1.0F / 3 },
			{ "right", { 1.5F * side, 0, 2 }, { 0, -0.5F, 2 }, { 0, 0.5F, 2 }, 1.0F / 3 },
			{ "top", { 0, 1.5F * side, 2 }, { -0.5F, 0, 2 }, { 0.5F, 0, 2 }, 1.0F / 3 },
			{ "bottom", { 0, -1.5F * side, 2 }, { -0.5F, 0, 2 }, { 0.5F, 0, 2 }, 1.0F / 3 },
		} };
		for (const ClipCase &fixture : cases) {
			const std::string label = std::string(perspective ? "perspective " : "orthographic ") + fixture.name;
			const std::array<TownCameraVertex, 3> input { { { fixture.outside, 0, 0 }, { fixture.b, 1, 0 }, { fixture.c, 0, 1 } } };
			const TownCameraClippedTriangles clipped = ClipTownCameraTriangle(frame, input);
			check(clipped.count == 2, label + " crossing forms two triangles");
			bool allInside = clipped.count > 0, edgeBFound = false, edgeCFound = false;
			const TownCameraPoint edgeB = fixture.outside + (fixture.b - fixture.outside) * fixture.t;
			const TownCameraPoint edgeC = fixture.outside + (fixture.c - fixture.outside) * fixture.t;
			for (size_t i = 0; i < clipped.count; ++i) {
				for (const TownCameraProjectedVertex &vertex : clipped.triangles[i]) {
					allInside &= std::isfinite(vertex.x) && std::isfinite(vertex.y)
					    && vertex.x >= -0.001F && vertex.x <= frame.width + 0.001F
					    && vertex.y >= -0.001F && vertex.y <= frame.height + 0.001F
					    && vertex.depth >= frame.nearClip && vertex.depth <= frame.farClip;
					edgeBFound |= samePoint(vertex.source.world, edgeB)
					    && close(vertex.source.u, fixture.t) && close(vertex.source.v, 0);
					edgeCFound |= samePoint(vertex.source.world, edgeC)
					    && close(vertex.source.u, 0) && close(vertex.source.v, fixture.t);
				}
			}
			check(allInside, label + " output lies inside all six frustum planes");
			check(edgeBFound && edgeCFound, label + " intersections preserve analytical world and UV coordinates");
			check(!ProjectTownCameraPoint(frame, fixture.outside, rayProjection), label + " outside point is rejected");
			const std::array<TownCameraVertex, 3> outside { { { fixture.outside, 0, 0 },
				{ fixture.outside + TownCameraPoint { 0, 0.05F, 0.02F }, 1, 0 },
				{ fixture.outside + TownCameraPoint { 0.05F, -0.05F, 0.02F }, 0, 1 } } };
			check(ClipTownCameraTriangle(frame, outside).count == 0, label + " entirely outside triangle is rejected");
		}
		const std::array<TownCameraVertex, 3> behind { { { { -0.2F, -0.2F, -2 }, 0, 0 },
			{ { 0.2F, -0.2F, -1 }, 1, 0 }, { { 0, 0.2F, -0.5F }, 0, 1 } } };
		check(ClipTownCameraTriangle(frame, behind).count == 0, "triangle entirely behind the eye is rejected");

		check(close(NormalizeTownCameraDepth(frame, frame.nearClip), 0), "normalized near-plane depth is zero");
		check(close(NormalizeTownCameraDepth(frame, frame.farClip), 1), "normalized far-plane depth is one");
		const float depth2 = NormalizeTownCameraDepth(frame, 2);
		const float depth4 = NormalizeTownCameraDepth(frame, 4);
		const float depth8 = NormalizeTownCameraDepth(frame, 8);
		check(depth2 > 0 && depth2 < depth4 && depth4 < depth8 && depth8 < 1, "normalized depth increases monotonically");
		check(close(depth2, perspective ? 5.0F / 9 : 1.0F / 9), "normalized depth at distance two matches analytical projection");
		check(std::isnan(NormalizeTownCameraDepth(frame, nan))
		        && std::isnan(NormalizeTownCameraDepth(frame, infinity))
		        && std::isnan(NormalizeTownCameraDepth(frame, 0.5F))
		        && std::isnan(NormalizeTownCameraDepth(frame, 11)), "normalized depth rejects nonfinite and unclipped distances");
	}

	// Near/far spans deliberately exceed the clip range by four orders of
	// magnitude. Both windings exercise cancellation at the near intersection.
	TownCameraFrame longSpanFrame = analytical;
	longSpanFrame.nearClip = 0.08F;
	longSpanFrame.farClip = 320;
	std::array<TownCameraVertex, 3> longSpan { { { { -0.02F, -0.02F, -1000 }, 0, 0 },
		{ { 0.02F, -0.02F, 1000 }, 1, 0 }, { { 0, 0.02F, 1000 }, 0, 1 } } };
	for (int winding = 0; winding < 2; ++winding) {
		const TownCameraClippedTriangles clipped = ClipTownCameraTriangle(longSpanFrame, longSpan);
		check(clipped.count > 0, "large near/far crossing survives clipping in either winding");
		bool valid = clipped.count > 0, hasNear = false, hasFar = false;
		for (size_t i = 0; i < clipped.count; ++i) {
			for (const TownCameraProjectedVertex &vertex : clipped.triangles[i]) {
				valid &= std::isfinite(vertex.x) && std::isfinite(vertex.y)
				    && vertex.depth >= longSpanFrame.nearClip && vertex.depth <= longSpanFrame.farClip
				    && std::isfinite(NormalizeTownCameraDepth(longSpanFrame, vertex.depth));
				hasNear |= vertex.depth == longSpanFrame.nearClip;
				hasFar |= vertex.depth == longSpanFrame.farClip;
			}
		}
		check(valid && hasNear && hasFar, "large crossings snap to publishable near/far depths without rounding outside");
		std::reverse(longSpan.begin(), longSpan.end());
	}

	// Equal screen weights at depths 1,2,4 correct to world weights 4/7,2/7,1/7.
	std::array<TownCameraProjectedVertex, 3> sampleTriangle { {
		{ 100, 100, 1, 1, { { 0, 0, 1 }, 0, 0 } },
		{ 200, 100, 2, 0.5F, { { 2, 0, 2 }, 1, 0 } },
		{ 100, 0, 4, 0.25F, { { 0, 4, 4 }, 0, 1 } },
	} };
	const std::array<float, 3> centroid { 1.0F / 3, 1.0F / 3, 1.0F / 3 };
	TownCameraSample sample;
	check(InterpolateTownCameraSample(sampleTriangle, centroid, sample), "perspective centroid sample is accepted");
	check(close(sample.depth, 12.0F / 7) && samePoint(sample.world, { 4.0F / 7, 4.0F / 7, 12.0F / 7 }), "perspective depth and world position share reciprocal-depth weights");
	check(close(sample.u, 2.0F / 7) && close(sample.v, 1.0F / 7), "perspective UV uses corrected weights instead of affine thirds");
	check(ProjectTownCameraPoint(analytical, sample.world, rayProjection)
	        && close(rayProjection.x, 400.0F / 3) && close(rayProjection.y, 200.0F / 3), "interpolated world sample reprojects to screen centroid");
	std::array<TownCameraProjectedVertex, 3> edgeTriangle = sampleTriangle;
	edgeTriangle[0].depth = 0.08F;
	edgeTriangle[0].reciprocalW = 12.5F;
	edgeTriangle[1].depth = edgeTriangle[2].depth = 320;
	edgeTriangle[1].reciprocalW = edgeTriangle[2].reciprocalW = 1.0F / 320;
	check(InterpolateTownCameraSample(edgeTriangle, { -0.000009F, 0.5000045F, 0.5000045F }, sample)
	        && sample.depth >= 0.08F && sample.depth <= 320 && sample.u >= 0 && sample.v >= 0
	        && sample.u + sample.v <= 1.000001F, "negative edge roundoff cannot extrapolate reciprocal depth beyond the far plane or UV outside the triangle");
	for (TownCameraProjectedVertex &vertex : sampleTriangle)
		vertex.reciprocalW = 1;
	check(InterpolateTownCameraSample(sampleTriangle, centroid, sample)
	        && close(sample.depth, 7.0F / 3) && samePoint(sample.world, { 2.0F / 3, 4.0F / 3, 7.0F / 3 })
	        && close(sample.u, 1.0F / 3) && close(sample.v, 1.0F / 3), "orthographic interpolation remains affine");
	check(!InterpolateTownCameraSample(sampleTriangle, { 0.5F, 0.5F, 0.5F }, sample), "sample rejects weights which do not sum to one");
	check(!InterpolateTownCameraSample(sampleTriangle, { -0.1F, 0.6F, 0.5F }, sample), "sample rejects points outside the triangle");
	check(!InterpolateTownCameraSample(sampleTriangle, { nan, 0, 1 }, sample), "sample rejects nonfinite barycentric weights");
	sampleTriangle[0].reciprocalW = 0;
	check(!InterpolateTownCameraSample(sampleTriangle, centroid, sample), "sample rejects zero reciprocal depth");
	sampleTriangle[0].reciprocalW = 1;
	sampleTriangle[1].source.world.x = infinity;
	check(!InterpolateTownCameraSample(sampleTriangle, centroid, sample), "sample rejects nonfinite source world coordinates");

	const std::array<TownCameraMode, 4> modes { TownCameraMode::Isometric, TownCameraMode::FreeOrbit,
		TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson };
	const std::array<TownCameraPose, 4> saved { {
		{ 0.25F, 0.6F, 25, { 2, 0, -3 } }, { -1, 0.2F, 8, { 4, 0, 6 } },
		{ 1.4F, 0.2F, 6, {} }, { -0.8F, -0.1F, 0, {} },
	} };
	TownCameraRig rig;
	for (size_t i = 0; i < modes.size(); ++i) {
		check(rig.SetMode(modes[i]), "valid camera mode is accepted");
		rig.SetPose(saved[i]);
	}
	for (size_t i = 0; i < modes.size(); ++i) {
		rig.SetMode(modes[i]);
		check(samePose(rig.pose(), saved[i]), "switching modes restores that mode's saved pose");
		check(rig.HideLocalPlayer() == (modes[i] == TownCameraMode::FirstPerson), "only active first person hides the local player");
	}
	rig.Suspend(true);
	check(rig.suspended() && rig.mode() == TownCameraMode::FirstPerson && samePose(rig.pose(), saved[3]), "F4 suspension preserves mode and pose");
	check(!rig.HideLocalPlayer() && !BuildTownCameraFrame(rig, {}, 640, 480, 320, 240).valid, "suspended 3D camera does not hide the native hero or publish a frame");
	rig.Suspend(false);
	check(!rig.suspended() && rig.HideLocalPlayer() && samePose(rig.pose(), saved[3]), "F4 resume restores first person without resetting orientation");
	rig.RestoreIsometric();
	check(rig.mode() == TownCameraMode::Isometric && samePose(rig.pose(), TownCameraPose {}) && !rig.HideLocalPlayer(), "Home restores the exact original pose and visible hero");
	for (size_t i = 1; i < modes.size(); ++i) {
		rig.SetMode(modes[i]);
		check(samePose(rig.pose(), saved[i]), "Home preserves the other mode's saved pose");
	}
	const uint64_t stableRevision = rig.revision();
	check(!rig.SetMode(static_cast<TownCameraMode>(255)) && rig.mode() == TownCameraMode::FirstPerson
	        && rig.revision() == stableRevision, "invalid mode does not change camera state");
	rig.Zoom(1);
	rig.Pan(1, 1);
	check(samePose(rig.pose(), saved[3]) && rig.revision() == stableRevision, "first person cannot become a close orbit through zoom or pan");
	rig.Orbit(nan, 1);
	rig.Orbit(1, infinity);
	check(samePose(rig.pose(), saved[3]) && rig.revision() == stableRevision, "nonfinite input does not corrupt saved orientation");
	rig.SetMode(TownCameraMode::ThirdPerson);
	rig.Pan(1, 1);
	check(samePose(rig.pose(), saved[2]), "third person keeps the eye anchored to the followed actor");
	TownCameraRig floorRig;
	for (const TownCameraMode mode : { TownCameraMode::FreeOrbit, TownCameraMode::ThirdPerson }) {
		floorRig.SetMode(mode);
		floorRig.SetPose({ 0, -1, 5, {} });
		const TownCameraFrame frame = BuildTownCameraFrame(floorRig, { 0, 2, 0 }, 640, 480, 320, 240);
		check(floorRig.pose().pitch > 0 && frame.valid && frame.eye.height > 2,
		    "orbit and third person do not place the eye below the followed ground height");
	}
	floorRig.SetMode(TownCameraMode::FirstPerson);
	floorRig.SetPose({ 0, -1, 0, {} });
	check(close(floorRig.pose().pitch, -1), "first person retains upward looking freedom at a fixed eye height");
	floorRig.SetMode(TownCameraMode::Isometric);
	floorRig.Pan(80, 80);
	const TownCameraPoint limitedPan = floorRig.pose().pan;
	check(close(std::sqrt(limitedPan.x * limitedPan.x + limitedPan.z * limitedPan.z), 20),
	    "isometric pan preserves the legacy radial twenty-tile limit");

	TownCameraRig invalidRig;
	check(!BuildTownCameraFrame(invalidRig, {}, 0, 480, 0, 240).valid
	        && !BuildTownCameraFrame(invalidRig, {}, 640, -1, 320, 0).valid, "empty or negative viewport is rejected");
	check(!BuildTownCameraFrame(invalidRig, {}, 20000, 480, 320, 240).valid, "unbounded viewport dimensions are rejected");
	check(!BuildTownCameraFrame(invalidRig, { nan, 0, 0 }, 640, 480, 320, 240).valid, "nonfinite anchor is rejected");
	check(!BuildTownCameraFrame(invalidRig, {}, 640, 480, nan, 240).valid
	        && !BuildTownCameraFrame(invalidRig, {}, 640, 480, -1, 240).valid
	        && !BuildTownCameraFrame(invalidRig, {}, 640, 480, 320, 481).valid, "nonfinite or out-of-viewport center is rejected");
	check(!BuildTownCameraFrame(invalidRig, {}, 640, 480, 320, 240, 0).valid
	        && !BuildTownCameraFrame(invalidRig, {}, 640, 480, 320, 240, nan).valid, "invalid native zoom is rejected");
	TownCameraFrame invalidFrame;
	check(!ProjectTownCameraPoint(invalidFrame, {}, rayProjection) && !TownCameraScreenRay(invalidFrame, 0, 0).valid
	        && std::isnan(NormalizeTownCameraDepth(invalidFrame, 1)), "invalid frame rejects projection, ray and normalized depth");
	check(!ProjectTownCameraPoint(analytical, { nan, 0, 2 }, rayProjection), "nonfinite world point is rejected");
	const std::array<std::array<float, 2>, 6> invalidPixels { {
		{ -1, 50 }, { 200, 50 }, { 50, -1 }, { 50, 200 }, { nan, 50 }, { 50, infinity },
	} };
	for (const std::array<float, 2> pixel : invalidPixels)
		check(!TownCameraScreenRay(analytical, pixel[0], pixel[1]).valid, "offscreen or nonfinite pixel has no ray");
	std::array<TownCameraVertex, 3> invalidTriangle { { { { 0, 0, 2 }, 0, 0 }, { { 0.2F, 0, 2 }, 1, 0 }, { { 0, 0.2F, 2 }, 0, 1 } } };
	check(ClipTownCameraTriangle(invalidFrame, invalidTriangle).count == 0, "invalid frame cannot publish clipped triangles");
	invalidTriangle[1].u = nan;
	check(ClipTownCameraTriangle(analytical, invalidTriangle).count == 0, "nonfinite UV rejects the complete triangle");
	invalidTriangle[1].u = 1;
	invalidTriangle[0].world.z = infinity;
	check(ClipTownCameraTriangle(analytical, invalidTriangle).count == 0, "nonfinite vertex rejects the complete triangle");
	TownCameraPreferences invalidPreferences;
	invalidPreferences.verticalFovDegrees = nan;
	invalidPreferences.nearClip = infinity;
	invalidPreferences.farClip = nan;
	invalidPreferences.eyeHeight = nan;
	invalidPreferences.firstPersonEyeHeight = nan;
	invalidRig.SetPreferences(invalidPreferences);
	invalidRig.SetMode(TownCameraMode::FirstPerson);
	invalidRig.SetPose({ nan, infinity, nan, { infinity, nan, nan } });
	const TownCameraFrame sanitized = BuildTownCameraFrame(invalidRig, {}, 640, 480, 320, 240);
	check(sanitized.valid && std::isfinite(sanitized.eye.height) && sanitized.nearClip > 0
	        && sanitized.farClip > sanitized.nearClip && sanitized.focalPixels > 0, "invalid preferences and pose recover a finite usable camera");

	out << "Camera contract: " << checks - firstCheck << " checks, " << failures << " failures\n";
	return failures == 0;
}

} // namespace devilution
