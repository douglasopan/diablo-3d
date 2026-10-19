extends RefCounted
## Read-only v2 metadata and embedded GLB loader. No import cache or source writes.

const COMPATIBILITY := ["native-unchanged", "external-code-required", "baked-reviewed"]

static func read(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return _failure("Manifesto não encontrado.")
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK:
		return _failure("JSON inválido na linha %d: %s" % [parser.get_error_line(), parser.get_error_message()])
	var result := validate(parser.data, path.get_base_dir())
	if result.ok:
		result["path"] = path
		result["sha256"] = FileAccess.get_sha256(path)
	return result

static func validate(data: Variant, base_dir: String = "") -> Dictionary:
	if not data is Dictionary:
		return _failure("O manifesto precisa ser um objeto JSON.")
	var errors: Array[String] = []
	if data.get("schemaVersion") != 2:
		errors.append("schemaVersion deve ser 2.")
	for key in ["assetId", "variant", "revision"]:
		if not data.get(key) is String or str(data.get(key, "")).strip_edges().is_empty():
			errors.append("%s deve identificar explicitamente esta revisão." % key)
	var anchor: Variant = data.get("anchor")
	if not anchor is Dictionary or not _vector3_valid(anchor.get("position")) or anchor.get("upAxis") != "Y":
		errors.append("anchor requer position finita com três números e upAxis Y.")
	var fit: Variant = data.get("fit", {})
	if not fit is Dictionary:
		errors.append("fit deve ser um objeto com escala uniforme opcional.")
	else:
		for key in fit:
			if key not in ["uniformScale", "yawRadians", "translation"]:
				errors.append("fit permite somente uniformScale, yawRadians e translation.")
		for key in ["uniformScale", "yawRadians"]:
			if fit.has(key) and (not _number(fit[key]) or not is_finite(float(fit[key]))):
				errors.append("fit.%s deve ser finito." % key)
		if fit.has("uniformScale") and _number(fit.uniformScale) and float(fit.uniformScale) <= 0:
			errors.append("fit.uniformScale deve ser positivo.")
		if fit.has("translation") and not _vector3_valid(fit.translation):
			errors.append("fit.translation requer três números finitos.")
		for key in ["scale", "scaleXYZ"]:
			if fit.has(key): errors.append("fit permite somente uniformScale, nunca ajuste por eixo.")
	for key in ["uniformScale", "yawRadians", "translation"]:
		if data.has(key): errors.append("%s pertence ao objeto fit do contrato v2." % key)
	var openings: Variant = data.get("openings")
	if not openings is Dictionary:
		errors.append("openings deve declarar a identidade e a compatibilidade do pipeline.")
	else:
		if not openings.get("pipelineId") is String or str(openings.get("pipelineId", "")).strip_edges().is_empty():
			errors.append("openings.pipelineId ausente.")
		if openings.get("compatibility") not in COMPATIBILITY:
			errors.append("openings.compatibility desconhecida.")
		if openings.has("sha256") and not _hash_valid(openings.sha256):
			errors.append("openings.sha256 inválido.")
	var source_data: Variant = data.get("sources")
	if not source_data is Array or source_data.is_empty() or source_data.size() > 32:
		errors.append("sources deve conter master e LODs explícitos.")
		return {"ok": false, "errors": errors}
	var sources: Array[Dictionary] = []
	var masters := 0
	var indices: Array[int] = []
	for index in source_data.size():
		var raw: Variant = source_data[index]
		var label := "sources[%d]" % index
		if not raw is Dictionary:
			errors.append(label + " deve ser um objeto.")
			continue
		var source: Dictionary = raw.duplicate(true)
		var role := str(source.get("role", ""))
		if role not in ["master-geometry", "drawable-lod"]:
			errors.append(label + ": role desconhecido.")
		if role == "master-geometry":
			masters += 1
			if source.has("runtimePath") or source.has("runtimeSha256"):
				errors.append(label + ": master de inspeção não pode declarar um pacote runtime.")
		if not source.get("path") is String or str(source.get("path", "")).is_empty() or str(source.get("path", "")).get_extension().to_lower() != "glb":
			errors.append(label + ": path deve apontar para um GLB explícito.")
		if not _hash_valid(source.get("sha256")):
			errors.append(label + ": sha256 inválido.")
		if source.has("runtimePath") != source.has("runtimeSha256"):
			errors.append(label + ": runtimePath e runtimeSha256 formam um par.")
		if source.has("runtimePath") and (not source.runtimePath is String or str(source.runtimePath).is_empty() or not _hash_valid(source.get("runtimeSha256"))):
			errors.append(label + ": par runtime inválido.")
		if role == "drawable-lod":
			var lod: Variant = source.get("lodIndex")
			if not _number(lod) or not is_finite(float(lod)) or float(lod) != floor(float(lod)) or float(lod) < 0:
				errors.append(label + ": lodIndex deve ser inteiro não negativo.")
			elif int(lod) in indices:
				errors.append(label + ": lodIndex duplicado.")
			else:
				indices.append(int(lod))
			if not _number(source.get("geometricError")) or not is_finite(float(source.get("geometricError", NAN))) or float(source.get("geometricError", -1)) < 0:
				errors.append(label + ": geometricError deve ser finito e não negativo.")
			if not source.get("errorVerified") is bool:
				errors.append(label + ": errorVerified explícito é obrigatório.")
			if _number(lod) and int(lod) == 0 and (source.get("geometricError") != 0 or source.get("errorVerified") != true):
				errors.append(label + ": LOD 0 requer erro 0 verificado, relativo à referência drawable.")
			if source.has("maxDiameterPixels") and (not _number(source.maxDiameterPixels) or not is_finite(float(source.maxDiameterPixels)) or float(source.maxDiameterPixels) < 0 or (source.get("lodIndex") != 0 and float(source.maxDiameterPixels) == 0)):
				errors.append(label + ": maxDiameterPixels deve ser finito e positivo para coarse; zero é ilimitado somente no LOD 0.")
			if source.has("openingEquivalenceVerified") and not source.openingEquivalenceVerified is bool:
				errors.append(label + ": openingEquivalenceVerified deve ser booleano.")
			if _number(lod) and is_finite(float(lod)) and float(lod) > 0:
				if not source.has("maxDiameterPixels"):
					errors.append(label + ": coarse requer maxDiameterPixels positivo explícito.")
				if not source.get("openingEquivalenceVerified") is bool:
					errors.append(label + ": coarse requer openingEquivalenceVerified explícito.")
		source["resolvedPath"] = resolve_path(str(source.get("path", "")), base_dir)
		sources.append(source)
	if masters < 1:
		errors.append("Uma fonte master-geometry é necessária; nunca é selecionada como drawable.")
	if 0 not in indices:
		errors.append("Uma referência drawable-lod de índice 0 é necessária.")
	indices.sort()
	for index in indices.size():
		if indices[index] != index:
			errors.append("Índices drawable devem ser contíguos desde 0.")
	var drawable: Array[Dictionary] = []
	for source in sources:
		if source.get("role") == "drawable-lod" and _number(source.get("lodIndex")) and is_finite(float(source.lodIndex)):
			drawable.append(source)
	drawable.sort_custom(func(first: Dictionary, second: Dictionary): return float(first.lodIndex) < float(second.lodIndex))
	for index in range(1, drawable.size()):
		if _number(drawable[index].get("geometricError")) and _number(drawable[index - 1].get("geometricError")) and float(drawable[index].geometricError) < float(drawable[index - 1].geometricError):
			errors.append("Erros dos LODs devem ser não decrescentes.")
		if index > 1 and _number(drawable[index].get("maxDiameterPixels")) and _number(drawable[index - 1].get("maxDiameterPixels")) and float(drawable[index].maxDiameterPixels) > float(drawable[index - 1].maxDiameterPixels):
			errors.append("Limites de diâmetro coarse devem ser não crescentes.")
	var geometric_reference: Variant = data.get("geometricReferenceSha256")
	if not _hash_valid(geometric_reference):
		errors.append("geometricReferenceSha256 deve identificar a referência drawable por hash.")
	else:
		for source in sources:
			if source.get("role") == "drawable-lod" and source.get("lodIndex") == 0 and source.get("sha256") != geometric_reference:
				errors.append("geometricReferenceSha256 diverge do drawable LOD 0.")
	return {"ok": errors.is_empty(), "errors": errors, "manifest": data.duplicate(true), "sources": sources}

static func load_glb(source: Dictionary) -> Dictionary:
	var path := str(source.get("resolvedPath", source.get("path", "")))
	if not FileAccess.file_exists(path):
		return _failure("GLB ausente: " + path.get_file())
	var bytes := FileAccess.get_file_as_bytes(path)
	var hash_context := HashingContext.new()
	hash_context.start(HashingContext.HASH_SHA256)
	hash_context.update(bytes)
	var actual_hash := hash_context.finish().hex_encode()
	if actual_hash != source.get("sha256"):
		return _failure("SHA-256 divergente. Esta cópia não corresponde à revisão declarada.")
	var descriptor_result := describe_glb(bytes)
	if not descriptor_result.ok:
		return descriptor_result
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	# The very same bytes hashed above are parsed; external resources are forbidden.
	var error := document.append_from_buffer(bytes, "", state)
	if error != OK:
		return _failure("Godot recusou o GLB (erro %d)." % error)
	var node := document.generate_scene(state)
	if not node is Node3D:
		if node != null: node.free()
		return _failure("Godot não criou uma cena 3D.")
	_remove_embedded_cameras_lights(node)
	var summary: Dictionary = descriptor_result.summary
	var inspect_only: bool = source.get("role") == "master-geometry"
	var gray: bool = inspect_only and not summary.uvPresent
	if gray:
		var neutral := StandardMaterial3D.new()
		neutral.albedo_color = Color(0.53, 0.55, 0.58)
		neutral.roughness = 0.85
		_apply_gray(node, neutral)
	return {"ok": true, "errors": [], "node": node, "sha256": actual_hash, "summary": summary, "inspectOnly": inspect_only, "gray": gray}

static func describe_glb(bytes: PackedByteArray) -> Dictionary:
	if bytes.size() < 20 or bytes.decode_u32(0) != 0x46546c67 or bytes.decode_u32(4) != 2 or bytes.decode_u32(8) != bytes.size():
		return _failure("GLB 2.0 inválido ou incompleto.")
	var json_size := bytes.decode_u32(12)
	if bytes.decode_u32(16) != 0x4e4f534a or json_size > bytes.size() - 20:
		return _failure("GLB sem bloco JSON válido.")
	var descriptor: Variant = JSON.parse_string(bytes.slice(20, 20 + json_size).get_string_from_utf8())
	if not descriptor is Dictionary:
		return _failure("JSON do GLB inválido.")
	for category in ["images", "buffers"]:
		if not descriptor.get(category, []) is Array:
			return _failure("Lista %s do GLB inválida." % category)
		for entry in descriptor.get(category, []):
			if not entry is Dictionary or entry.has("uri") or (category == "images" and not entry.has("bufferView")):
				return _failure("O GLB requer buffers e imagens incorporados por bufferView, sem URI.")
	var triangles := 0
	var primitives := 0
	var has_uv := true
	var has_normals := true
	var accessors: Array = descriptor.get("accessors", [])
	for mesh in descriptor.get("meshes", []):
		for primitive in mesh.get("primitives", []):
			primitives += 1
			var attributes: Dictionary = primitive.get("attributes", {})
			has_uv = has_uv and attributes.has("TEXCOORD_0")
			has_normals = has_normals and attributes.has("NORMAL")
			var accessor_index := int(primitive.get("indices", attributes.get("POSITION", -1)))
			if accessor_index >= 0 and accessor_index < accessors.size():
				var count := int(accessors[accessor_index].get("count", 0))
				var mode := int(primitive.get("mode", 4))
				if mode == 4: triangles += count / 3
				elif mode in [5, 6]: triangles += maxi(count - 2, 0)
	var channels: Array[String] = []
	for material in descriptor.get("materials", []):
		var pbr: Dictionary = material.get("pbrMetallicRoughness", {})
		for channel in ["baseColorTexture", "metallicRoughnessTexture"]:
			if pbr.has(channel) and channel not in channels: channels.append(channel)
		for channel in ["normalTexture", "occlusionTexture", "emissiveTexture"]:
			if material.has(channel) and channel not in channels: channels.append(channel)
	return {"ok": true, "errors": [], "summary": {"triangles": triangles, "primitives": primitives, "uvPresent": has_uv and primitives > 0, "normalsPresent": has_normals and primitives > 0, "materials": descriptor.get("materials", []).size(), "textureChannels": channels, "extensions": descriptor.get("extensionsUsed", []), "animations": descriptor.get("animations", []).size(), "skins": descriptor.get("skins", []).size()}}

static func bounds(node: Node3D, parent_transform: Transform3D = Transform3D.IDENTITY) -> AABB:
	var transform := parent_transform * node.transform
	var result := AABB()
	var initialized := false
	if node is MeshInstance3D and node.mesh != null:
		result = transform * node.get_aabb()
		initialized = true
	for child in node.get_children():
		if not child is Node3D: continue
		var child_box := bounds(child, transform)
		if child_box.size == Vector3.ZERO: continue
		result = result.merge(child_box) if initialized else child_box
		initialized = true
	return result

static func resolve_path(path: String, base_dir: String) -> String:
	if path.is_absolute_path(): return path.simplify_path()
	return base_dir.path_join(path).simplify_path()

static func _remove_embedded_cameras_lights(node: Node) -> void:
	for child in node.get_children():
		if child is Camera3D or child is Light3D:
			child.free()
		else:
			if child is AnimationPlayer: child.stop()
			_remove_embedded_cameras_lights(child)

static func _apply_gray(node: Node, material: Material) -> void:
	if node is MeshInstance3D: node.material_override = material
	for child in node.get_children(): _apply_gray(child, material)

static func _number(value: Variant) -> bool:
	return value is int or value is float

static func _vector3_valid(value: Variant) -> bool:
	if not value is Array or value.size() != 3: return false
	for coordinate in value:
		if not _number(coordinate) or not is_finite(float(coordinate)): return false
	return true

static func _hash_valid(value: Variant) -> bool:
	if not value is String or value.length() != 64: return false
	for character in value:
		if character not in "0123456789abcdef": return false
	return true

static func _failure(message: String) -> Dictionary:
	return {"ok": false, "errors": [message]}
