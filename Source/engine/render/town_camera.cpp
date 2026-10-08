#include "engine/render/town_camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float NativeHeightScale = 0.816496580927726F;

bool Finite(TownCameraPoint point)
{
	return std::isfinite(point.x) && std::isfinite(point.height) && std::isfinite(point.z);
}

float Dot(TownCameraPoint a, TownCameraPoint b)
{
	return a.x * b.x + a.height * b.height + a.z * b.z;
}

float Bounded(float value, float fallback, float minimum, float maximum)
{
	return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

TownCameraPose Sanitize(TownCameraPose pose, TownCameraMode mode)
{
	pose.yaw = std::isfinite(pose.yaw) ? std::remainder(pose.yaw, 2 * Pi) : Pi / 4;
	const bool isometric = mode == TownCameraMode::Isometric;
	const bool first = mode == TownCameraMode::FirstPerson;
	pose.pitch = Bounded(pose.pitch, first ? 0 : Pi / 6, isometric ? 0.35F : first ? -1.40F : 0.005F, 1.40F);
	pose.distance = first ? 0 : Bounded(pose.distance, 22, isometric ? 10.0F : 0.5F, isometric ? 52.0F : 80.0F);
	pose.pan = { Bounded(pose.pan.x, 0, -80, 80), 0, Bounded(pose.pan.z, 0, -80, 80) };
	const float panLength = std::sqrt(Dot(pose.pan, pose.pan));
	if (isometric && panLength > 20)
		pose.pan = pose.pan * (20 / panLength);
	if (mode == TownCameraMode::ThirdPerson || first)
		pose.pan = {};
	return pose;
}

TownCameraPreferences Sanitize(TownCameraPreferences preferences)
{
	preferences.verticalFovDegrees = Bounded(preferences.verticalFovDegrees, 60, 35, 100);
	preferences.nearClip = Bounded(preferences.nearClip, 0.08F, 0.02F, 0.5F);
	preferences.farClip = Bounded(preferences.farClip, 320, 64, 1024);
	preferences.eyeHeight = Bounded(preferences.eyeHeight, 1.1F, 0.4F, 2);
	preferences.orbitRadiansPerPixel = Bounded(preferences.orbitRadiansPerPixel, 0.006F, 0.001F, 0.02F);
	preferences.zoomExponentPerStep = Bounded(preferences.zoomExponentPerStep, 0.12783337F, 0.01F, 0.5F);
	return preferences;
}

TownCameraProjectedVertex Project(const TownCameraFrame &frame, TownCameraVertex vertex, TownCameraPoint view)
{
	const float reciprocalW = frame.perspective ? 1 / view.z : 1;
	return { frame.centerX + frame.focalPixels * view.x * reciprocalW,
		frame.centerY - frame.focalPixels * view.height * reciprocalW,
		view.z, reciprocalW, vertex };
}

struct ClipVertex {
	TownCameraVertex source;
	TownCameraPoint view;
};

// Distances may have arbitrary magnitude, but each plane's inside half-space
// is >=0. Perspective side planes are homogeneous; division happens last.
float PlaneDistance(const TownCameraFrame &frame, TownCameraPoint view, size_t plane)
{
	const float w = frame.perspective ? view.z : 1;
	switch (plane) {
	case 0: return view.z - frame.nearClip;
	case 1: return frame.farClip - view.z;
	case 2: return frame.centerX * w + frame.focalPixels * view.x;
	case 3: return (frame.width - frame.centerX) * w - frame.focalPixels * view.x;
	case 4: return frame.centerY * w - frame.focalPixels * view.height;
	default: return (frame.height - frame.centerY) * w + frame.focalPixels * view.height;
	}
}

ClipVertex Lerp(const ClipVertex &a, const ClipVertex &b, double t)
{
	const auto scalar = [t](float start, float end) {
		return static_cast<float>(static_cast<double>(start) * (1 - t) + static_cast<double>(end) * t);
	};
	const auto point = [&scalar](TownCameraPoint start, TownCameraPoint end) {
		return TownCameraPoint { scalar(start.x, end.x), scalar(start.height, end.height), scalar(start.z, end.z) };
	};
	return { { point(a.source.world, b.source.world), scalar(a.source.u, b.source.u), scalar(a.source.v, b.source.v) }, point(a.view, b.view) };
}

void SnapToPlane(const TownCameraFrame &frame, ClipVertex &vertex, size_t plane)
{
	const float w = frame.perspective ? vertex.view.z : 1;
	switch (plane) {
	case 0: vertex.view.z = frame.nearClip; break;
	case 1: vertex.view.z = frame.farClip; break;
	case 2: vertex.view.x = -frame.centerX * w / frame.focalPixels; break;
	case 3: vertex.view.x = (frame.width - frame.centerX) * w / frame.focalPixels; break;
	case 4: vertex.view.height = frame.centerY * w / frame.focalPixels; break;
	default: vertex.view.height = -(frame.height - frame.centerY) * w / frame.focalPixels; break;
	}
	vertex.source.world = TownCameraToWorld(frame, vertex.view);
}

} // namespace

TownCameraPoint TownCameraPoint::operator+(TownCameraPoint other) const
{
	return { x + other.x, height + other.height, z + other.z };
}

TownCameraPoint TownCameraPoint::operator-(TownCameraPoint other) const
{
	return { x - other.x, height - other.height, z - other.z };
}

TownCameraPoint TownCameraPoint::operator*(float scale) const
{
	return { x * scale, height * scale, z * scale };
}

TownCameraRig::TownCameraRig()
{
	poses_[1] = poses_[0];
	poses_[2].distance = 5;
	poses_[2].pitch = 0.3F;
	poses_[3].distance = 0;
	poses_[3].pitch = 0;
}

const TownCameraPose &TownCameraRig::pose() const
{
	return poses_[static_cast<size_t>(mode_)];
}

bool TownCameraRig::SetMode(TownCameraMode mode)
{
	if (static_cast<size_t>(mode) >= poses_.size())
		return false;
	if (mode_ != mode) {
		mode_ = mode;
		++revision_;
	}
	return true;
}

void TownCameraRig::SetPose(TownCameraPose pose)
{
	poses_[static_cast<size_t>(mode_)] = Sanitize(pose, mode_);
	++revision_;
}

void TownCameraRig::SetPreferences(TownCameraPreferences preferences)
{
	preferences_ = Sanitize(preferences);
	++revision_;
}

void TownCameraRig::Orbit(float yawDelta, float pitchDelta)
{
	if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta))
		return;
	TownCameraPose next = pose();
	next.yaw += std::remainder(yawDelta, 2 * Pi);
	next.pitch += std::clamp(pitchDelta, -Pi, Pi);
	SetPose(next);
}

void TownCameraRig::Zoom(float steps)
{
	if (!std::isfinite(steps) || mode_ == TownCameraMode::FirstPerson)
		return;
	TownCameraPose next = pose();
	next.distance *= std::exp(-preferences_.zoomExponentPerStep * std::clamp(steps, -50.0F, 50.0F));
	SetPose(next);
}

void TownCameraRig::Pan(float x, float z)
{
	if (!std::isfinite(x) || !std::isfinite(z) || mode_ == TownCameraMode::ThirdPerson || mode_ == TownCameraMode::FirstPerson)
		return;
	TownCameraPose next = pose();
	next.pan.x += std::clamp(x, -80.0F, 80.0F);
	next.pan.z += std::clamp(z, -80.0F, 80.0F);
	SetPose(next);
}

void TownCameraRig::Suspend(bool suspended)
{
	if (suspended_ != suspended) {
		suspended_ = suspended;
		++revision_;
	}
}

void TownCameraRig::RestoreIsometric()
{
	mode_ = TownCameraMode::Isometric;
	poses_[0] = {};
	++revision_;
}

bool TownCameraRig::HideLocalPlayer() const
{
	return !suspended_ && mode_ == TownCameraMode::FirstPerson;
}

TownCameraFrame BuildTownCameraFrame(const TownCameraRig &rig, TownCameraPoint anchor,
	int width, int height, float centerX, float centerY, float nativeZoom)
{
	TownCameraFrame frame;
	if (rig.suspended() || !Finite(anchor) || width <= 0 || height <= 0 || width > 16384 || height > 16384
	    || !std::isfinite(centerX) || !std::isfinite(centerY) || centerX < 0 || centerX > width || centerY < 0 || centerY > height
	    || !std::isfinite(nativeZoom) || nativeZoom <= 0 || nativeZoom > 4)
		return frame;
	const TownCameraPose &pose = rig.pose();
	const TownCameraPreferences &preferences = rig.preferences();
	frame.width = width;
	frame.height = height;
	frame.centerX = centerX;
	frame.centerY = centerY;
	frame.perspective = rig.mode() != TownCameraMode::Isometric;
	frame.heightScale = frame.perspective ? 1 : NativeHeightScale;
	const float cy = std::cos(pose.yaw), sy = std::sin(pose.yaw);
	const float cp = std::cos(pose.pitch), sp = std::sin(pose.pitch);
	const TownCameraPoint outward { cy * cp, sp, sy * cp };
	frame.forward = outward * -1;
	frame.right = { sy, 0, -cy };
	frame.up = { -cy * sp, cp, -sy * sp };
	TownCameraPoint target = anchor + pose.pan;
	if (rig.mode() == TownCameraMode::ThirdPerson || rig.mode() == TownCameraMode::FirstPerson)
		target.height += preferences.eyeHeight;
	const float distance = !frame.perspective ? 256 : pose.distance;
	frame.eye = target + TownCameraPoint { outward.x, outward.height / frame.heightScale, outward.z } * distance;
	frame.focalPixels = frame.perspective
	    ? height / (2 * std::tan(preferences.verticalFovDegrees * Pi / 360))
	    : 45.25483399593904F * 22 / pose.distance * nativeZoom;
	frame.nearClip = frame.perspective ? preferences.nearClip : 0.4F;
	frame.farClip = frame.perspective ? preferences.farClip : 4096;
	frame.fogDepthOffset = frame.perspective ? 0 : 256 - pose.distance;
	frame.valid = Finite(frame.eye) && std::isfinite(frame.focalPixels);
	return frame;
}

TownCameraPoint TownCameraToView(const TownCameraFrame &frame, TownCameraPoint world)
{
	TownCameraPoint relative = world - frame.eye;
	relative.height *= frame.heightScale;
	return { Dot(relative, frame.right), Dot(relative, frame.up), Dot(relative, frame.forward) };
}

TownCameraPoint TownCameraToWorld(const TownCameraFrame &frame, TownCameraPoint view)
{
	TownCameraPoint relative = frame.right * view.x + frame.up * view.height + frame.forward * view.z;
	relative.height /= frame.heightScale;
	return frame.eye + relative;
}

bool ProjectTownCameraPoint(const TownCameraFrame &frame, TownCameraPoint world,
	TownCameraProjectedVertex &output)
{
	if (!frame.valid || !Finite(world))
		return false;
	const TownCameraPoint view = TownCameraToView(frame, world);
	if (!Finite(view))
		return false;
	for (size_t plane = 0; plane < 6; ++plane) {
		const float distance = PlaneDistance(frame, view, plane);
		if (!std::isfinite(distance) || distance < 0)
			return false;
	}
	output = Project(frame, { world, 0, 0 }, view);
	return true;
}

float NormalizeTownCameraDepth(const TownCameraFrame &frame, float depth)
{
	if (!frame.valid || !std::isfinite(depth) || depth < frame.nearClip || depth > frame.farClip)
		return std::numeric_limits<float>::quiet_NaN();
	const double near = frame.nearClip, far = frame.farClip;
	const double normalized = frame.perspective ? far * (depth - near) / ((far - near) * depth) : (depth - near) / (far - near);
	return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

float TownCameraFogDepth(const TownCameraFrame &frame, float depth)
{
	if (!frame.valid || !std::isfinite(depth))
		return std::numeric_limits<float>::quiet_NaN();
	return std::max(0.0F, depth - frame.fogDepthOffset);
}

TownCameraClippedTriangles ClipTownCameraTriangle(const TownCameraFrame &frame,
	const std::array<TownCameraVertex, 3> &triangle)
{
	TownCameraClippedTriangles result;
	if (!frame.valid)
		return result;
	std::array<ClipVertex, 10> polygon {}, scratch {};
	size_t count = 3;
	for (size_t i = 0; i < count; ++i) {
		if (!Finite(triangle[i].world) || !std::isfinite(triangle[i].u) || !std::isfinite(triangle[i].v))
			return result;
		polygon[i] = { triangle[i], TownCameraToView(frame, triangle[i].world) };
		if (!Finite(polygon[i].view))
			return result;
	}
	for (size_t plane = 0; plane < 6; ++plane) {
		size_t nextCount = 0;
		ClipVertex previous = polygon[count - 1];
		float previousDistance = PlaneDistance(frame, previous.view, plane);
		for (size_t i = 0; i < count; ++i) {
			const ClipVertex current = polygon[i];
			const float currentDistance = PlaneDistance(frame, current.view, plane);
			if (!std::isfinite(previousDistance) || !std::isfinite(currentDistance))
				return {};
			if ((previousDistance >= 0) != (currentDistance >= 0)) {
				if (nextCount >= scratch.size())
					return {};
				const double ratio = static_cast<double>(previousDistance) / (static_cast<double>(previousDistance) - currentDistance);
				scratch[nextCount] = Lerp(previous, current, std::clamp(ratio, 0.0, 1.0));
				SnapToPlane(frame, scratch[nextCount], plane);
				++nextCount;
			}
			if (currentDistance >= 0) {
				if (nextCount >= scratch.size())
					return {};
				scratch[nextCount++] = current;
			}
			previous = current;
			previousDistance = currentDistance;
		}
		if (nextCount < 3)
			return {};
		polygon.swap(scratch);
		count = nextCount;
	}
	if (count > 9)
		return {};
	std::array<TownCameraProjectedVertex, 9> projected;
	for (size_t i = 0; i < count; ++i) {
		polygon[i].view.z = std::clamp(polygon[i].view.z, frame.nearClip, frame.farClip);
		projected[i] = Project(frame, polygon[i].source, polygon[i].view);
		if (!std::isfinite(projected[i].x) || !std::isfinite(projected[i].y) || !std::isfinite(projected[i].reciprocalW)
		    || !Finite(projected[i].source.world))
			return {};
		// Roundoff at a side intersection must not escape the raster bounds.
		projected[i].x = std::clamp(projected[i].x, 0.0F, static_cast<float>(frame.width));
		projected[i].y = std::clamp(projected[i].y, 0.0F, static_cast<float>(frame.height));
	}
	for (size_t i = 1; i + 1 < count; ++i) {
		result.triangles[result.count++] = { projected[0], projected[i], projected[i + 1] };
	}
	return result;
}

bool InterpolateTownCameraSample(const std::array<TownCameraProjectedVertex, 3> &triangle,
	const std::array<float, 3> &barycentric, TownCameraSample &output)
{
	float denominator = 0, sum = 0;
	std::array<float, 3> weights;
	for (size_t i = 0; i < 3; ++i) {
		if (!std::isfinite(barycentric[i]) || barycentric[i] < -0.00001F
		    || !std::isfinite(triangle[i].reciprocalW) || triangle[i].reciprocalW <= 0)
			return false;
		// Tiny negative coverage errors belong to the boundary, not to an
		// extrapolated point which could escape the clipped near/far interval.
		weights[i] = std::max(0.0F, barycentric[i]) * triangle[i].reciprocalW;
		denominator += weights[i];
		sum += barycentric[i];
	}
	if (!std::isfinite(denominator) || denominator <= 0 || std::abs(sum - 1) > 0.0001F)
		return false;
	TownCameraSample sample;
	for (size_t i = 0; i < 3; ++i) {
		const float weight = weights[i] / denominator;
		sample.depth += triangle[i].depth * weight;
		sample.world = sample.world + triangle[i].source.world * weight;
		sample.u += triangle[i].source.u * weight;
		sample.v += triangle[i].source.v * weight;
	}
	if (!Finite(sample.world) || !std::isfinite(sample.depth) || !std::isfinite(sample.u) || !std::isfinite(sample.v))
		return false;
	output = sample;
	return true;
}

TownCameraRay TownCameraScreenRay(const TownCameraFrame &frame, float x, float y)
{
	TownCameraRay ray;
	if (!frame.valid || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x >= frame.width || y < 0 || y >= frame.height)
		return ray;
	const float vx = (x - frame.centerX) / frame.focalPixels;
	const float vy = (frame.centerY - y) / frame.focalPixels;
	ray.origin = frame.perspective ? frame.eye : TownCameraToWorld(frame, { vx, vy, 0 });
	TownCameraPoint direction = frame.perspective ? frame.right * vx + frame.up * vy + frame.forward : frame.forward;
	direction.height /= frame.heightScale;
	const float length = std::sqrt(Dot(direction, direction));
	if (!std::isfinite(length) || length <= 0)
		return ray;
	ray.direction = direction * (1 / length);
	ray.valid = true;
	return ray;
}

} // namespace devilution
