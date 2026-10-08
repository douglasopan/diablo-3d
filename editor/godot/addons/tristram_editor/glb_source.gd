@tool
extends RefCounted
## GLB é uma fonte explícita; a baseline nunca é reimportada a partir dele.

static func import_glb(path: String, instance: Node3D, replacement: bool) -> Dictionary:
	var errors: Array[String] = []
	if not FileAccess.file_exists(path) or path.get_extension().to_lower() != "glb":
		return {"ok": false, "errors": ["Selecione um arquivo GLB existente."]}
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.size() < 20 or bytes.decode_u32(0) != 0x46546c67 or bytes.decode_u32(4) != 2 or bytes.decode_u32(8) != bytes.size():
		return {"ok": false, "errors": ["GLB 2.0 inválido ou incompleto."]}
	var json_size := bytes.decode_u32(12)
	if bytes.decode_u32(16) != 0x4e4f534a or json_size > bytes.size() - 20:
		return {"ok": false, "errors": ["GLB sem bloco JSON válido."]}
	var descriptor: Variant = JSON.parse_string(bytes.slice(20, 20 + json_size).get_string_from_utf8())
	if not descriptor is Dictionary:
		return {"ok": false, "errors": ["JSON do GLB inválido."]}
	for category in ["images", "buffers"]:
		for entry in descriptor.get(category, []):
			var uri := str(entry.get("uri", ""))
			if not uri.is_empty() and not uri.begins_with("data:"):
				errors.append("GLB depende de arquivo externo (%s). Incorpore as dependências no GLB antes de importar." % category)
	if not errors.is_empty():
		return {"ok": false, "errors": errors}
	var sha := FileAccess.get_sha256(path)
	for child in instance.get_children():
		if child is Node3D and str(child.get_meta("d3d_source_sha256", "")) == sha and str(child.get_meta("d3d_source_mode", "")) == ("replacement" if replacement else "reference"):
			return {"ok": true, "errors": [], "branch": child, "sha256": sha, "path": child.get_meta("d3d_source_path", ""), "existing": true}
	var folder := ProjectSettings.globalize_path("res://local/sources")
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		return {"ok": false, "errors": ["Não foi possível criar local/sources."]}
	var ignore := FileAccess.open(folder.path_join(".gdignore"), FileAccess.WRITE)
	if ignore != null:
		ignore.close()
	var local_path := folder.path_join(sha + ".glb")
	if not FileAccess.file_exists(local_path):
		if DirAccess.copy_absolute(path, local_path) != OK:
			return {"ok": false, "errors": ["Falha ao copiar a fonte GLB para local/sources."]}
	if FileAccess.get_sha256(local_path) != sha:
		return {"ok": false, "errors": ["O hash da cópia local diverge da fonte GLB."]}
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_file(local_path, state)
	if error != OK:
		return {"ok": false, "errors": ["Godot recusou o GLB (erro %s)." % error]}
	var content := document.generate_scene(state)
	if content == null:
		return {"ok": false, "errors": ["Godot não criou a cena do GLB."]}
	# Fonte pesada fica em recurso binário local. Embutir ImageTexture no TSCN
	# transformaria um GLB de poucos MB em centenas de MB de texto decimal.
	for child in content.get_children():
		assign_owner(child, content)
	var imported_scene := PackedScene.new()
	var binary_path := ProjectSettings.localize_path(folder.path_join(sha + ".scn"))
	var packed_error := imported_scene.pack(content)
	if packed_error == OK:
		packed_error = ResourceSaver.save(imported_scene, binary_path, ResourceSaver.FLAG_COMPRESS)
	content.free()
	if packed_error != OK:
		return {"ok": false, "errors": ["Não foi possível guardar a fonte GLB importada (%s)." % packed_error]}
	imported_scene = ResourceLoader.load(binary_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	if imported_scene == null:
		return {"ok": false, "errors": ["Não foi possível reabrir a fonte GLB importada."]}
	content = imported_scene.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
	var branch := Node3D.new()
	branch.name = "ReplacementGLB" if replacement else "SourceGLB"
	branch.set_meta("d3d_export_ignore", not replacement)
	branch.set_meta("d3d_source_path", ProjectSettings.localize_path(local_path))
	branch.set_meta("d3d_source_sha256", sha)
	branch.set_meta("d3d_source_original_name", path.get_file())
	branch.set_meta("d3d_import_scene_path", binary_path)
	branch.set_meta("d3d_source_mode", "replacement" if replacement else "reference")
	branch.set_meta("d3d_glb_export_errors", descriptor_export_errors(descriptor))
	branch.set_meta("d3d_glb_extensions", descriptor.get("extensionsUsed", []))
	branch.set_meta("d3d_glb_animation_count", descriptor.get("animations", []).size())
	branch.set_meta("d3d_glb_skin_count", descriptor.get("skins", []).size())
	branch.visible = replacement
	branch.add_child(content)
	if replacement:
		for child in instance.get_children():
			if child is Node3D:
				child.visible = false
	instance.add_child(branch)
	var owner_root := instance.owner if instance.owner != null else instance
	branch.owner = owner_root
	content.owner = owner_root
	owner_root.set_editable_instance(content, true)
	if replacement:
		instance.set_meta("d3d_source_path", ProjectSettings.localize_path(local_path))
		instance.set_meta("d3d_source_sha256", sha)
	fit_to_native(branch, instance)
	return {"ok": true, "errors": [], "branch": branch, "sha256": sha, "path": ProjectSettings.localize_path(local_path)}

static func descriptor_export_errors(descriptor: Dictionary) -> Array[String]:
	# Avaliar também o GLB original: o importador pode não preservar uma extensão.
	var errors: Array[String] = []
	if not descriptor.get("animations", []).is_empty():
		errors.append("GLB contém animação; D3DMESH1 é estático.")
	if not descriptor.get("skins", []).is_empty():
		errors.append("GLB contém skin/esqueleto.")
	for extension in descriptor.get("extensionsUsed", []):
		if extension not in ["KHR_mesh_quantization"]:
			errors.append("Extensão GLB não suportada para exportação: %s." % extension)
	for mesh in descriptor.get("meshes", []):
		for primitive in mesh.get("primitives", []):
			if not primitive.get("targets", []).is_empty():
				errors.append("GLB contém morph targets.")
	for material in descriptor.get("materials", []):
		if material.get("alphaMode", "OPAQUE") != "OPAQUE":
			errors.append("GLB contém material transparente/recortado.")
		var pbr: Dictionary = material.get("pbrMetallicRoughness", {})
		if float(pbr.get("metallicFactor", 1.0)) != 0.0:
			errors.append("Material metálico do GLB exige conversão explícita antes da exportação.")
		if pbr.has("metallicRoughnessTexture") or material.has("normalTexture") or material.has("occlusionTexture") or material.has("emissiveTexture"):
			errors.append("GLB contém mapas PBR que o atlas RGB inicial não transporta.")
		var emission: Array = material.get("emissiveFactor", [0, 0, 0])
		if emission != [0, 0, 0]:
			errors.append("GLB contém emissão.")
		if int(pbr.get("baseColorTexture", {}).get("texCoord", 0)) != 0:
			errors.append("GLB usa UV secundária para albedo.")
	return errors

static func assign_owner(node: Node, root: Node) -> void:
	node.owner = root
	for child in node.get_children():
		assign_owner(child, root)

static func apply_recorded_fit(branch: Node3D, fit: Dictionary) -> bool:
	for key in ["rotatedSourceMin", "rotatedSourceMax", "targetMinRelative", "targetMaxRelative"]:
		if not fit.get(key) is Array or fit[key].size() != 3:
			return false
	var source_min := _vector(fit.rotatedSourceMin)
	var source_max := _vector(fit.rotatedSourceMax)
	var target_min := _vector(fit.targetMinRelative)
	var target_max := _vector(fit.targetMaxRelative)
	var extent := source_max - source_min
	if not source_min.is_finite() or not source_max.is_finite() or not target_min.is_finite() or not target_max.is_finite() or extent.x <= 0.0 or extent.y <= 0.0 or extent.z <= 0.0:
		return false
	var target_extent := target_max - target_min
	var yaw := float(fit.get("yawDegrees", 0))
	if target_extent.x <= 0.0 or target_extent.y <= 0.0 or target_extent.z <= 0.0 or not is_finite(yaw):
		return false
	# Recorded proxy bounds are an initial placement aid, never permission to
	# stretch three axes independently. Keep the author's proportions and ground.
	var factor := float(fit.get("uniformScale", minf(target_extent.x / extent.x, target_extent.z / extent.z)))
	if not is_finite(factor) or factor <= 0.0:
		return false
	var scale := Vector3.ONE * factor
	var target_center := (target_min + target_max) / 2.0
	var source_center := (source_min + source_max) / 2.0
	var translation := Vector3(target_center.x - source_center.x * factor, target_min.y - source_min.y * factor, target_center.z - source_center.z * factor)
	var basis := Basis.from_scale(scale) * Basis(Vector3.UP, deg_to_rad(yaw))
	branch.transform = Transform3D(basis, translation)
	var applied := fit.duplicate(true)
	applied["fitMode"] = "uniform-footprint-grounded; native review required"
	applied["appliedScale"] = [factor, factor, factor]
	applied["appliedTranslation"] = [translation.x, translation.y, translation.z]
	branch.set_meta("d3d_fit", applied)
	return true

static func _vector(values: Array) -> Vector3:
	return Vector3(float(values[0]), float(values[1]), float(values[2]))

static func fit_to_native(branch: Node3D, instance: Node3D) -> void:
	# Ajuste inicial explícito pelo footprint. Rotação/altura final pertencem ao autor.
	branch.transform = Transform3D.IDENTITY
	var bounds := _bounds(branch, Transform3D.IDENTITY)
	if bounds.size.x <= 0.00001 or bounds.size.z <= 0.00001:
		return
	var identity: Dictionary = instance.get_meta("d3d_instance", {})
	var low: Array = identity.get("nativeMin", [0, 0])
	var high: Array = identity.get("nativeMax", [1, 1])
	var width := float(high[0]) - float(low[0]) + 1.0
	var depth := float(high[1]) - float(low[1]) + 1.0
	var factor := minf(width / bounds.size.x, depth / bounds.size.z)
	branch.scale = Vector3.ONE * factor
	branch.position = Vector3(width / 2.0, 0.0, depth / 2.0) - Vector3(bounds.get_center().x, bounds.position.y, bounds.get_center().z) * factor
	branch.set_meta("d3d_fit", "uniform-footprint-grounded; manual review required")

static func _bounds(node: Node3D, parent_transform: Transform3D) -> AABB:
	var transform := parent_transform * node.transform
	var result := AABB()
	var initialized := false
	if node is MeshInstance3D and node.mesh != null:
		result = transform * node.get_aabb()
		initialized = true
	for child in node.get_children():
		if not child is Node3D:
			continue
		var child_box := _bounds(child, transform)
		if child_box.size == Vector3.ZERO:
			continue
		result = result.merge(child_box) if initialized else child_box
		initialized = true
	return result
