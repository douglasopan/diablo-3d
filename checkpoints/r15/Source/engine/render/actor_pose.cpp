#include "engine/render/actor_pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace devilution {
namespace {

constexpr ActorMatrix IdentityMatrix {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1,
};

bool Fail(std::string &error, const char *description)
{
	error = description;
	return false;
}

bool Finite(float value)
{
	return std::isfinite(value);
}

bool Finite(ActorVec3 value)
{
	return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

bool Finite(ActorVec4 value)
{
	return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
}

bool Finite(const ActorMatrix &matrix)
{
	return std::all_of(matrix.begin(), matrix.end(), [](float value) { return Finite(value); });
}

bool NormalizeQuaternion(ActorVec4 &value)
{
	if (!Finite(value))
		return false;
	const double lengthSquared = static_cast<double>(value.x) * value.x
	    + static_cast<double>(value.y) * value.y + static_cast<double>(value.z) * value.z
	    + static_cast<double>(value.w) * value.w;
	if (!std::isfinite(lengthSquared) || lengthSquared <= 0)
		return false;
	const double inverseLength = 1 / std::sqrt(lengthSquared);
	value.x = static_cast<float>(value.x * inverseLength);
	value.y = static_cast<float>(value.y * inverseLength);
	value.z = static_cast<float>(value.z * inverseLength);
	value.w = static_cast<float>(value.w * inverseLength);
	return Finite(value);
}

bool ValidTransform(const ActorTransform &transform)
{
	return Finite(transform.translation) && Finite(transform.rotation) && Finite(transform.scale)
	    && transform.scale.x > 0 && transform.scale.y > 0 && transform.scale.z > 0;
}

bool TransformMatrix(const ActorTransform &transform, ActorMatrix &matrix)
{
	if (!ValidTransform(transform))
		return false;
	ActorVec4 rotation = transform.rotation;
	if (!NormalizeQuaternion(rotation))
		return false;
	const double x = rotation.x;
	const double y = rotation.y;
	const double z = rotation.z;
	const double w = rotation.w;
	const double sx = transform.scale.x;
	const double sy = transform.scale.y;
	const double sz = transform.scale.z;
	matrix = {
		static_cast<float>((1 - 2 * (y * y + z * z)) * sx),
		static_cast<float>((2 * (x * y + z * w)) * sx),
		static_cast<float>((2 * (x * z - y * w)) * sx), 0,
		static_cast<float>((2 * (x * y - z * w)) * sy),
		static_cast<float>((1 - 2 * (x * x + z * z)) * sy),
		static_cast<float>((2 * (y * z + x * w)) * sy), 0,
		static_cast<float>((2 * (x * z + y * w)) * sz),
		static_cast<float>((2 * (y * z - x * w)) * sz),
		static_cast<float>((1 - 2 * (x * x + y * y)) * sz), 0,
		transform.translation.x, transform.translation.y, transform.translation.z, 1,
	};
	return Finite(matrix);
}

bool Multiply(const ActorMatrix &left, const ActorMatrix &right, ActorMatrix &output)
{
	ActorMatrix result {};
	for (size_t column = 0; column < 4; ++column) {
		for (size_t row = 0; row < 4; ++row) {
			double sum = 0;
			for (size_t k = 0; k < 4; ++k)
				sum += static_cast<double>(left[k * 4 + row]) * right[column * 4 + k];
			result[column * 4 + row] = static_cast<float>(sum);
		}
	}
	if (!Finite(result))
		return false;
	output = result;
	return true;
}

bool Slerp(ActorVec4 from, ActorVec4 to, double amount, ActorVec4 &output)
{
	if (!NormalizeQuaternion(from) || !NormalizeQuaternion(to))
		return false;
	double dot = static_cast<double>(from.x) * to.x + static_cast<double>(from.y) * to.y
	    + static_cast<double>(from.z) * to.z + static_cast<double>(from.w) * to.w;
	if (dot < 0) {
		to = { -to.x, -to.y, -to.z, -to.w };
		dot = -dot;
	}
	dot = std::clamp(dot, 0.0, 1.0);
	double fromWeight = 1 - amount;
	double toWeight = amount;
	if (dot < 0.9995) {
		const double angle = std::acos(dot);
		const double denominator = std::sin(angle);
		fromWeight = std::sin((1 - amount) * angle) / denominator;
		toWeight = std::sin(amount * angle) / denominator;
	}
	output = {
		static_cast<float>(from.x * fromWeight + to.x * toWeight),
		static_cast<float>(from.y * fromWeight + to.y * toWeight),
		static_cast<float>(from.z * fromWeight + to.z * toWeight),
		static_cast<float>(from.w * fromWeight + to.w * toWeight),
	};
	return NormalizeQuaternion(output);
}

bool SampleTrack(const ActorTrack &track, float seconds, std::array<float, 4> &value)
{
	if (track.times.empty() || track.times.size() != track.values.size()
	    || !Finite(track.times.front()) || !Finite(track.times.back()))
		return false;
	const auto after = std::upper_bound(track.times.begin(), track.times.end(), seconds);
	const size_t first = after == track.times.begin() ? 0 : static_cast<size_t>(after - track.times.begin() - 1);
	const size_t second = std::min(first + 1, track.times.size() - 1);
	if (track.interpolation != ActorTrackInterpolation::Linear && track.interpolation != ActorTrackInterpolation::Step)
		return false;
	value = track.values[first];
	const size_t components = track.path == ActorTrackPath::Rotation ? 4 : 3;
	for (size_t component = 0; component < components; ++component)
		if (!Finite(value[component]) || !Finite(track.values[second][component]))
			return false;
	if (first == second || seconds <= track.times.front() || track.interpolation == ActorTrackInterpolation::Step)
		return true;
	const double span = static_cast<double>(track.times[second]) - track.times[first];
	if (!std::isfinite(span) || span <= 0)
		return false;
	const double amount = std::clamp((static_cast<double>(seconds) - track.times[first]) / span, 0.0, 1.0);
	if (track.path == ActorTrackPath::Rotation) {
		const auto &next = track.values[second];
		ActorVec4 interpolated;
		if (!Slerp({ value[0], value[1], value[2], value[3] }, { next[0], next[1], next[2], next[3] }, amount, interpolated))
			return false;
		value = { interpolated.x, interpolated.y, interpolated.z, interpolated.w };
	} else {
		for (size_t component = 0; component < components; ++component)
			value[component] = static_cast<float>(value[component] * (1 - amount) + track.values[second][component] * amount);
	}
	return true;
}

bool SampleClip(const ActorModel &model, size_t clipIndex, float seconds,
    std::vector<ActorTransform> &locals, std::string &error)
{
	if (clipIndex >= model.clips.size() || !Finite(seconds))
		return Fail(error, "Actor clip index or sample time is invalid");
	const ActorClip &clip = model.clips[clipIndex];
	if (!Finite(clip.startTime) || !Finite(clip.endTime) || clip.startTime < 0 || clip.endTime < clip.startTime)
		return Fail(error, "Actor clip time range is invalid");
	seconds = std::clamp(seconds, clip.startTime, clip.endTime);
	std::vector<uint8_t> paths(model.nodes.size(), 0);
	for (const ActorTrack &track : clip.tracks) {
		if (track.node >= locals.size())
			return Fail(error, "Actor track refers to an invalid node");
		const unsigned path = static_cast<unsigned>(track.path);
		if (path > static_cast<unsigned>(ActorTrackPath::Scale))
			return Fail(error, "Actor track path is unsupported");
		const uint8_t bit = static_cast<uint8_t>(1U << path);
		if ((paths[track.node] & bit) != 0)
			return Fail(error, "Actor clip repeats a node transform path");
		paths[track.node] |= bit;
		std::array<float, 4> value {};
		if (!SampleTrack(track, seconds, value))
			return Fail(error, "Actor track cannot be sampled");
		ActorTransform &local = locals[track.node];
		switch (track.path) {
		case ActorTrackPath::Translation:
			local.translation = { value[0], value[1], value[2] };
			break;
		case ActorTrackPath::Rotation:
			local.rotation = { value[0], value[1], value[2], value[3] };
			break;
		case ActorTrackPath::Scale:
			local.scale = { value[0], value[1], value[2] };
			break;
		}
	}
	return true;
}

bool BuildWorldNode(size_t node, const ActorModel &model, const std::vector<ActorTransform> &locals,
    std::vector<uint8_t> &visit, std::vector<ActorMatrix> &world, std::string &error)
{
	if (visit[node] == 2)
		return true;
	if (visit[node] == 1)
		return Fail(error, "Actor node hierarchy contains a cycle");
	visit[node] = 1;
	ActorMatrix local;
	if (!TransformMatrix(locals[node], local))
		return Fail(error, "Actor local transform is nonfinite, singular or invalid");
	const int32_t parent = model.nodes[node].parent;
	if (parent < -1 || parent >= static_cast<int32_t>(model.nodes.size()) || parent == static_cast<int32_t>(node))
		return Fail(error, "Actor parent node index is invalid");
	if (parent == -1) {
		world[node] = local;
	} else {
		if (!BuildWorldNode(static_cast<size_t>(parent), model, locals, visit, world, error))
			return false;
		if (!Multiply(world[parent], local, world[node]))
			return Fail(error, "Actor world transform overflowed");
	}
	visit[node] = 2;
	return true;
}

using NormalMatrix = std::array<double, 9>; // Row-major inverse-transpose.

bool InverseTranspose(const ActorMatrix &matrix, NormalMatrix &output)
{
	const double a00 = matrix[0];
	const double a01 = matrix[4];
	const double a02 = matrix[8];
	const double a10 = matrix[1];
	const double a11 = matrix[5];
	const double a12 = matrix[9];
	const double a20 = matrix[2];
	const double a21 = matrix[6];
	const double a22 = matrix[10];
	output = {
		a11 * a22 - a12 * a21, a12 * a20 - a10 * a22, a10 * a21 - a11 * a20,
		a02 * a21 - a01 * a22, a00 * a22 - a02 * a20, a01 * a20 - a00 * a21,
		a01 * a12 - a02 * a11, a02 * a10 - a00 * a12, a00 * a11 - a01 * a10,
	};
	const double determinant = a00 * output[0] + a01 * output[1] + a02 * output[2];
	if (!std::isfinite(determinant) || determinant == 0)
		return false;
	for (double &value : output) {
		value /= determinant;
		if (!std::isfinite(value))
			return false;
	}
	return true;
}

bool Skin(const ActorModel &model, ActorPose &pose, std::string &error)
{
	pose.jointPalette.resize(model.joints.size());
	std::vector<NormalMatrix> normalPalette(model.joints.size());
	for (size_t index = 0; index < model.joints.size(); ++index) {
		const ActorJoint &joint = model.joints[index];
		if (joint.node >= pose.worldNodes.size() || !Finite(joint.inverseBind))
			return Fail(error, "Actor joint node or inverse bind matrix is invalid");
		if (std::abs(joint.inverseBind[3]) > 0.00001F || std::abs(joint.inverseBind[7]) > 0.00001F
		    || std::abs(joint.inverseBind[11]) > 0.00001F || std::abs(joint.inverseBind[15] - 1) > 0.00001F)
			return Fail(error, "Actor inverse bind matrix must be affine");
		if (!Multiply(pose.worldNodes[joint.node], joint.inverseBind, pose.jointPalette[index])
		    || !InverseTranspose(pose.jointPalette[index], normalPalette[index]))
			return Fail(error, "Actor skin palette is nonfinite or singular");
	}
	pose.positions.resize(model.vertices.size());
	pose.normals.resize(model.vertices.size());
	for (size_t index = 0; index < model.vertices.size(); ++index) {
		const ActorSkinVertex &vertex = model.vertices[index];
		if (!Finite(vertex.position) || !Finite(vertex.normal))
			return Fail(error, "Actor vertex position or normal is nonfinite");
		std::array<double, 3> position {};
		std::array<double, 3> normal {};
		double totalWeight = 0;
		for (size_t influence = 0; influence < 4; ++influence) {
			const size_t joint = vertex.joints[influence];
			const float weight = vertex.weights[influence];
			if (joint >= pose.jointPalette.size() || !Finite(weight) || weight < 0)
				return Fail(error, "Actor vertex skin index or weight is invalid");
			totalWeight += weight;
			if (weight == 0)
				continue;
			const ActorMatrix &matrix = pose.jointPalette[joint];
			const NormalMatrix &inverse = normalPalette[joint];
			for (size_t row = 0; row < 3; ++row) {
				position[row] += weight * (static_cast<double>(matrix[row]) * vertex.position.x
				    + static_cast<double>(matrix[4 + row]) * vertex.position.y
				    + static_cast<double>(matrix[8 + row]) * vertex.position.z + matrix[12 + row]);
				normal[row] += weight * (inverse[row * 3] * vertex.normal.x
				    + inverse[row * 3 + 1] * vertex.normal.y + inverse[row * 3 + 2] * vertex.normal.z);
			}
		}
		if (!std::isfinite(totalWeight) || std::abs(totalWeight - 1) > 0.001)
			return Fail(error, "Actor vertex skin weights do not sum to one");
		const double normalLength = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		if (!std::isfinite(normalLength) || normalLength <= 0)
			return Fail(error, "Actor blended normal is nonfinite or zero");
		pose.positions[index] = { static_cast<float>(position[0]), static_cast<float>(position[1]), static_cast<float>(position[2]) };
		pose.normals[index] = { static_cast<float>(normal[0] / normalLength), static_cast<float>(normal[1] / normalLength), static_cast<float>(normal[2] / normalLength) };
		if (!Finite(pose.positions[index]) || !Finite(pose.normals[index]))
			return Fail(error, "Actor skinned vertex overflowed");
	}
	return true;
}

bool Evaluate(const ActorModel &model, const std::vector<ActorTransform> &locals,
    ActorPose &output, std::string &error)
{
	if (model.nodes.empty() || model.nodes.size() > 256 || model.joints.empty() || model.joints.size() > 96
	    || model.vertices.empty() || model.vertices.size() > 100000)
		return Fail(error, "Actor pose input exceeds the validated model limits");
	ActorPose candidate;
	candidate.worldNodes.resize(model.nodes.size(), IdentityMatrix);
	std::vector<uint8_t> visit(model.nodes.size(), 0);
	for (size_t node = 0; node < model.nodes.size(); ++node)
		if (!BuildWorldNode(node, model, locals, visit, candidate.worldNodes, error))
			return false;
	if (!Skin(model, candidate, error))
		return false;
	output = std::move(candidate);
	error.clear();
	return true;
}

std::vector<ActorTransform> RestLocals(const ActorModel &model)
{
	std::vector<ActorTransform> locals;
	locals.reserve(model.nodes.size());
	for (const ActorNode &node : model.nodes)
		locals.push_back(node.rest);
	return locals;
}

} // namespace

bool EvaluateActorPose(const ActorModel &model, size_t clip, float seconds,
    ActorPose &output, std::string &error)
{
	try {
		auto locals = RestLocals(model);
		if (!SampleClip(model, clip, seconds, locals, error))
			return false;
		return Evaluate(model, locals, output, error);
	} catch (const std::bad_alloc &) {
		return Fail(error, "Actor pose allocation failed");
	}
}

bool EvaluateActorBindPose(const ActorModel &model, ActorPose &output, std::string &error)
{
	try {
		return Evaluate(model, RestLocals(model), output, error);
	} catch (const std::bad_alloc &) {
		return Fail(error, "Actor bind pose allocation failed");
	}
}

} // namespace devilution
