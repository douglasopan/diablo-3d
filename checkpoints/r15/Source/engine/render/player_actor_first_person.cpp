#include "engine/render/player_actor_first_person.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

#include "engine/render/actor_player_binding.hpp"

namespace devilution {
namespace {

constexpr size_t MaxVertices = 100000;
constexpr size_t MaxIndices = 300000;

bool Fail(std::string &error, const char *message)
{
	error = message;
	return false;
}

bool Finite(ActorVec3 value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Bounded(ActorVec3 value)
{
	return Finite(value) && std::abs(value.x) <= 4096 && std::abs(value.y) <= 4096 && std::abs(value.z) <= 4096;
}

bool ValidHand(PlayerActorHand hand)
{
	return hand == PlayerActorHand::Left || hand == PlayerActorHand::Right;
}

bool ValidEquipment(const PlayerActorEquippedHand &equipment)
{
	if (!ValidHand(equipment.hand)) return false;
	if (!equipment.occupied) return true;
	// Native InvBody hand slots are 4/5; caller maps native slots to rig hands.
	return (equipment.nativeSlot == 4 || equipment.nativeSlot == 5)
	    && equipment.nativeItemId >= 0 && equipment.inventoryCursor >= 0 && equipment.inventoryCursor <= 4096;
}

bool ValidConfig(const PlayerActorFirstPersonConfig &config)
{
	return !config.leftForeArm.empty() && config.leftForeArm.size() <= 127
	    && !config.rightForeArm.empty() && config.rightForeArm.size() <= 127
	    && config.leftForeArm != config.rightForeArm && Finite(config.fit.authoredFootpoint)
	    && std::isfinite(config.fit.uniformScale) && config.fit.uniformScale > 0 && config.fit.uniformScale <= 64
	    && std::isfinite(config.fit.authoredYawOffset) && std::isfinite(config.nativeYawRadians)
	    && Bounded(config.authoritativeFootpoint);
}

bool ValidModelFrame(const ActorModel &model, const ActorPlayerFrame &frame, std::string &error)
{
	const auto &body = frame.body;
	if (model.nodes.empty() || model.nodes.size() > 256 || model.joints.empty() || model.joints.size() > 96
	    || model.vertices.empty() || model.vertices.size() > MaxVertices || model.indices.empty()
	    || model.indices.size() > MaxIndices || model.indices.size() % 3 != 0
	    || body.vertices.size() != model.vertices.size() || body.indices != model.indices
	    || body.authoredWorldNodes.size() != model.nodes.size()
	    || body.assetId != model.assetId || body.variant != model.variant || body.revision != model.revision
	    || body.sourceSha256 != model.sourceSha256 || frame.attachments.size() > 8)
		return Fail(error, "First-person selection requires the same bounded model and already posed body");
	for (size_t i = 0; i < model.nodes.size(); ++i) {
		if (model.nodes[i].parent < -1 || model.nodes[i].parent >= static_cast<int32_t>(model.nodes.size())
		    || model.nodes[i].parent == static_cast<int32_t>(i))
			return Fail(error, "First-person rig hierarchy is invalid or cyclic");
	}
	for (size_t i = 0; i < model.nodes.size(); ++i) {
		int32_t node = static_cast<int32_t>(i);
		for (size_t depth = 0; node != -1; ++depth) {
			if (depth >= model.nodes.size()) return Fail(error, "First-person rig hierarchy is cyclic");
			node = model.nodes[static_cast<size_t>(node)].parent;
		}
	}
	for (const auto &joint : model.joints) {
		if (joint.node >= model.nodes.size()) return Fail(error, "First-person joint refers outside the same rig");
	}
	for (uint32_t index : model.indices) {
		if (index >= model.vertices.size()) return Fail(error, "First-person body index is outside posed vertices");
	}
	for (const auto &vertex : body.vertices) {
		if (!Bounded(vertex.worldPosition) || !Finite(vertex.worldNormal)
		    || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
			return Fail(error, "First-person posed body contains invalid geometry");
	}
	return true;
}

std::optional<size_t> UniqueNode(const ActorModel &model, std::string_view name)
{
	std::optional<size_t> result;
	for (size_t i = 0; i < model.nodes.size(); ++i) {
		if (model.nodes[i].name != name) continue;
		if (result) return std::nullopt;
		result = i;
	}
	return result;
}

bool DescendsFrom(const ActorModel &model, size_t node, size_t ancestor)
{
	// ValidModelFrame already checked every chain terminates; native node order is unrestricted.
	for (;;) {
		if (node == ancestor) return true;
		const int32_t parent = model.nodes[node].parent;
		if (parent < 0) return false;
		node = static_cast<size_t>(parent);
	}
}

bool SocketBelongsToHand(const ActorModel &model, const PlayerActorFirstPersonSocket &socket,
	const PlayerActorFirstPersonConfig &config)
{
	const auto root = UniqueNode(model, socket.hand == PlayerActorHand::Left ? config.leftForeArm : config.rightForeArm);
	const auto hand = UniqueNode(model, socket.bone);
	return root && hand && DescendsFrom(model, *hand, *root)
	    && std::any_of(model.joints.begin(), model.joints.end(), [hand](const ActorJoint &joint) { return joint.node == *hand; });
}

ActorMatrix Multiply(const ActorMatrix &a, const ActorMatrix &b)
{
	ActorMatrix result {};
	for (size_t column = 0; column < 4; ++column) {
		for (size_t row = 0; row < 4; ++row) {
			double value = 0;
			for (size_t k = 0; k < 4; ++k) value += static_cast<double>(a[k * 4 + row]) * b[column * 4 + k];
			result[column * 4 + row] = static_cast<float>(value);
		}
	}
	return result;
}

ActorMatrix WorldFit(const PlayerActorFirstPersonConfig &config)
{
	// Exactly the placement convention of actor_player_frame.cpp::WorldFit.
	const double angle = static_cast<double>(config.nativeYawRadians) + config.fit.authoredYawOffset;
	const double c = std::cos(angle) * config.fit.uniformScale;
	const double s = std::sin(angle) * config.fit.uniformScale;
	const auto &p = config.authoritativeFootpoint;
	const auto &f = config.fit.authoredFootpoint;
	return { static_cast<float>(c), 0, static_cast<float>(-s), 0,
		0, config.fit.uniformScale, 0, 0, static_cast<float>(s), 0, static_cast<float>(c), 0,
		static_cast<float>(p.x - c * f.x - s * f.z), p.y - config.fit.uniformScale * f.y,
		static_cast<float>(p.z + s * f.x - c * f.z), 1 };
}

bool QuadNormal(const ActorMatrix &matrix, ActorVec3 &normal)
{
	const double a = matrix[0], b = matrix[4], c = matrix[8];
	const double d = matrix[1], e = matrix[5], f = matrix[9];
	const double g = matrix[2], h = matrix[6], i = matrix[10];
	const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (!std::isfinite(determinant) || determinant <= 1e-12) return false;
	// Inverse-transpose times the quad's authored +Z normal.
	const double x = (d * h - e * g) / determinant;
	const double y = (b * g - a * h) / determinant;
	const double z = (a * e - b * d) / determinant;
	const double length = std::sqrt(x * x + y * y + z * z);
	if (!std::isfinite(length) || length <= 1e-12) return false;
	normal = { static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length) };
	return Finite(normal);
}

ActorVec3 TransformPoint(const ActorMatrix &matrix, ActorVec3 point)
{
	return { matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
		matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
		matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14] };
}

bool ValidRigid(const ActorPlayerAttachmentGeometry &attachment)
{
	if (attachment.parts.empty() || attachment.parts.size() > 32) return false;
	size_t vertices = 0, indices = 0;
	for (const auto &part : attachment.parts) {
		if (part.vertices.empty() || part.vertices.size() > MaxVertices - vertices
		    || part.indices.empty() || part.indices.size() % 3 != 0 || part.indices.size() > MaxIndices - indices) return false;
		vertices += part.vertices.size(); indices += part.indices.size();
		for (uint32_t index : part.indices) if (index >= part.vertices.size()) return false;
		for (const auto &vertex : part.vertices) {
			if (!Bounded(vertex.worldPosition) || !Finite(vertex.worldNormal)
			    || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) return false;
		}
	}
	ActorVec3 normal;
	return std::all_of(attachment.worldMatrix.begin(), attachment.worldMatrix.end(), [](float value) { return std::isfinite(value); })
	    && std::abs(attachment.worldMatrix[3]) <= 0.00001F && std::abs(attachment.worldMatrix[7]) <= 0.00001F
	    && std::abs(attachment.worldMatrix[11]) <= 0.00001F && std::abs(attachment.worldMatrix[15] - 1) <= 0.00001F
	    && QuadNormal(attachment.worldMatrix, normal);
}

} // namespace

bool BuildPlayerActorInventorySpriteQuad(const ActorModel &model, const ActorPlayerFrame &posedFrame,
	const PlayerActorEquippedHand &equipment, const PlayerActorFirstPersonSocket &socket,
	const PlayerActorInventorySpriteSource &source, const PlayerActorFirstPersonConfig &config,
	PlayerActorInventorySpriteAttachment &output, std::string &error)
{
	if (!ValidConfig(config) || !ValidEquipment(equipment) || !equipment.occupied || !equipment.usable
	    || socket.hand != equipment.hand || socket.bone.empty() || socket.bone.size() > 127
	    || !source.sprite || source.sprite->width() == 0 || source.sprite->height() == 0
	    || source.sprite->width() > 2048 || source.sprite->height() > 2048
	    || !std::isfinite(source.pivotPixels.x) || !std::isfinite(source.pivotPixels.y)
	    || source.pivotPixels.x < 0 || source.pivotPixels.x > source.sprite->width()
	    || source.pivotPixels.y < 0 || source.pivotPixels.y > source.sprite->height()
	    || !std::isfinite(source.rigUnitsPerPixel) || source.rigUnitsPerPixel <= 0 || source.rigUnitsPerPixel > 1)
		return Fail(error, "Inventory attachment needs exact equipped identity, sprite, grip pivot and pixel-to-rig scale");
	if (!ValidModelFrame(model, posedFrame, error)) return false;
	if (!SocketBelongsToHand(model, socket, config)) return Fail(error, "Inventory sprite socket belongs to another rig hand");
	try {
		ActorPose pose;
		pose.worldNodes = posedFrame.body.authoredWorldNodes;
		ActorMatrix authoredSocket;
		if (!ActorPlayerAttachment(model, pose, socket.bone, socket.local, authoredSocket, error)) return false;
		PlayerActorInventorySpriteAttachment next;
		next.equipment = equipment; next.sprite = source.sprite; next.pivotPixels = source.pivotPixels;
		next.rigUnitsPerPixel = source.rigUnitsPerPixel;
		next.worldMatrix = Multiply(WorldFit(config), authoredSocket);
		if (!std::all_of(next.worldMatrix.begin(), next.worldMatrix.end(), [](float value) { return std::isfinite(value); }))
			return Fail(error, "Inventory sprite socket matrix is nonfinite");
		ActorVec3 normal;
		if (!QuadNormal(next.worldMatrix, normal)) return Fail(error, "Inventory sprite socket has singular or mirrored basis");
		const float left = -source.pivotPixels.x * source.rigUnitsPerPixel;
		const float right = (source.sprite->width() - source.pivotPixels.x) * source.rigUnitsPerPixel;
		const float top = source.pivotPixels.y * source.rigUnitsPerPixel;
		const float bottom = (source.pivotPixels.y - source.sprite->height()) * source.rigUnitsPerPixel;
		const std::array<ActorVec3, 4> corners { ActorVec3 { left, top, 0 }, ActorVec3 { right, top, 0 },
			ActorVec3 { right, bottom, 0 }, ActorVec3 { left, bottom, 0 } };
		constexpr std::array<ActorVec2, 4> uv { ActorVec2 { 0, 0 }, ActorVec2 { 1, 0 }, ActorVec2 { 1, 1 }, ActorVec2 { 0, 1 } };
		for (size_t i = 0; i < next.quad.size(); ++i) {
			next.quad[i].worldPosition = TransformPoint(next.worldMatrix, corners[i]);
			if (!Bounded(next.quad[i].worldPosition)) return Fail(error, "Inventory sprite quad exceeds visual world bounds");
			next.quad[i].worldNormal = normal; next.quad[i].uv = uv[i];
		}
		output = std::move(next); error.clear(); return true;
	} catch (const std::bad_alloc &) {
		return Fail(error, "Inventory sprite socket preparation allocation failed");
	}
}

bool PreparePlayerActorFirstPerson(const ActorModel &model, const ActorPlayerFrame &posedFrame,
	std::span<const PlayerActorEquippedHand> equipment, std::span<const PlayerActorFirstPersonSocket> sockets,
	const PlayerActorInventorySpriteResolver &spriteResolver, const PlayerActorFirstPersonConfig &config,
	PlayerActorFirstPersonFrame &output, std::string &error)
{
	if (!ValidConfig(config) || equipment.size() > 2 || sockets.size() > 2)
		return Fail(error, "First-person configuration/equipment/socket count is invalid");
	if (!ValidModelFrame(model, posedFrame, error)) return false;
	for (size_t i = 0; i < equipment.size(); ++i) {
		if (!ValidEquipment(equipment[i])) return Fail(error, "First-person equipment is not an exact native hand-slot descriptor");
		for (size_t j = 0; j < i; ++j) {
			if (equipment[i].hand == equipment[j].hand
			    || (equipment[i].occupied && equipment[j].occupied && equipment[i].nativeSlot == equipment[j].nativeSlot))
				return Fail(error, "First-person equipped hand/slot is duplicated");
		}
	}
	for (size_t i = 0; i < sockets.size(); ++i) {
		if (!ValidHand(sockets[i].hand) || sockets[i].bone.empty() || sockets[i].bone.size() > 127)
			return Fail(error, "First-person hand socket identity is invalid");
		if (!SocketBelongsToHand(model, sockets[i], config)) return Fail(error, "First-person socket belongs to another rig hand");
		for (size_t j = 0; j < i; ++j) {
			if (sockets[i].hand == sockets[j].hand || sockets[i].bone == sockets[j].bone)
				return Fail(error, "First-person hand/socket is duplicated");
		}
	}
	const auto left = UniqueNode(model, config.leftForeArm), right = UniqueNode(model, config.rightForeArm);
	if (!left || !right || DescendsFrom(model, *left, *right) || DescendsFrom(model, *right, *left))
		return Fail(error, "First-person forearms are absent, ambiguous or overlap in the same rig");
	try {
		std::vector<uint8_t> jointSide(model.joints.size(), 0);
		for (size_t i = 0; i < model.joints.size(); ++i) {
			if (DescendsFrom(model, model.joints[i].node, *left)) jointSide[i] = 1;
			else if (DescendsFrom(model, model.joints[i].node, *right)) jointSide[i] = 2;
		}
		std::vector<uint8_t> vertexSide(model.vertices.size(), 0);
		for (size_t i = 0; i < model.vertices.size(); ++i) {
			double total = 0, leftWeight = 0, rightWeight = 0;
			for (size_t influence = 0; influence < 4; ++influence) {
				const uint16_t joint = model.vertices[i].joints[influence];
				const float weight = model.vertices[i].weights[influence];
				if (joint >= jointSide.size() || !std::isfinite(weight) || weight < 0 || weight > 1)
					return Fail(error, "First-person source vertex has an invalid skin influence");
				total += weight;
				if (jointSide[joint] == 1) leftWeight += weight;
				else if (jointSide[joint] == 2) rightWeight += weight;
			}
			if (std::abs(total - 1) > 0.001) return Fail(error, "First-person source skin weights are not normalized");
			// A .5/.5 seam belongs to neither side; never duplicate it or leak torso.
			const bool isLeft = leftWeight >= 0.5, isRight = rightWeight >= 0.5;
			if (isLeft != isRight) vertexSide[i] = isLeft ? 1 : 2;
		}
		PlayerActorFirstPersonFrame next;
		for (size_t i = 0; i < model.indices.size(); i += 3) {
			const auto a = model.indices[i], b = model.indices[i + 1], c = model.indices[i + 2];
			const uint8_t side = vertexSide[a];
			if (side == 0 || vertexSide[b] != side || vertexSide[c] != side) continue;
			auto &indices = side == 1 ? next.leftArmIndices : next.rightArmIndices;
			indices.insert(indices.end(), { a, b, c });
		}
		if (next.leftArmIndices.empty() || next.rightArmIndices.empty())
			return Fail(error, "First-person mask has no complete triangles for both posed forearms/hands");
		size_t selectedVertices = posedFrame.body.vertices.size(), selectedIndices = posedFrame.body.indices.size();
		for (const auto &item : equipment) {
			if (!item.occupied || !item.usable) continue;
			const PlayerActorFirstPersonSocket *socket = nullptr;
			for (const auto &candidate : sockets) if (candidate.hand == item.hand) socket = &candidate;
			if (socket == nullptr) return Fail(error, "Equipped first-person item has no explicit same-rig hand socket");
			std::optional<size_t> rigid;
			for (size_t i = 0; i < posedFrame.attachments.size(); ++i) {
				const auto &candidate = posedFrame.attachments[i];
				if (candidate.nativeItemId != item.nativeItemId || candidate.bone != socket->bone) continue;
				if (rigid) return Fail(error, "Equipped first-person 3D attachment is ambiguous");
				rigid = i;
			}
			if (rigid) {
				if (!ValidRigid(posedFrame.attachments[*rigid])) return Fail(error, "Equipped first-person 3D attachment is invalid");
				for (const auto &part : posedFrame.attachments[*rigid].parts) {
					if (part.vertices.size() > MaxVertices - selectedVertices || part.indices.size() > MaxIndices - selectedIndices)
						return Fail(error, "First-person body and selected attachments exceed aggregate geometry budget");
					selectedVertices += part.vertices.size(); selectedIndices += part.indices.size();
				}
				next.rigidAttachments.push_back({ item, *rigid });
				continue;
			}
			if (!spriteResolver) return Fail(error, "Equipped item without posed 3D geometry needs its exact inventory sprite");
			std::optional<PlayerActorInventorySpriteSource> source;
			try { source = spriteResolver(item); }
			catch (...) { return Fail(error, "Equipped inventory sprite resolver failed"); }
			if (!source) return Fail(error, "Exact equipped inventory sprite is unavailable");
			PlayerActorInventorySpriteAttachment sprite;
			if (!BuildPlayerActorInventorySpriteQuad(model, posedFrame, item, *socket, *source, config, sprite, error)) return false;
			if (4 > MaxVertices - selectedVertices || 6 > MaxIndices - selectedIndices)
				return Fail(error, "First-person body and sprite attachment exceed aggregate geometry budget");
			selectedVertices += 4; selectedIndices += 6;
			next.spriteAttachments.push_back(std::move(sprite));
		}
		output = std::move(next); error.clear(); return true;
	} catch (const std::bad_alloc &) {
		return Fail(error, "First-person selection allocation failed");
	}
}

} // namespace devilution
