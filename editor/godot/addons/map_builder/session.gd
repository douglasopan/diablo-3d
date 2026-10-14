@tool
extends RefCounted
## Local assembly proposals. Registry, simulation and installed files are read only.

const Reader := preload("res://addons/model_review/d3d_reader.gd")
const Snapshot := preload("res://addons/tristram_editor/snapshot.gd")
const PROPOSALS := "res://local/map-builder/proposals"

var application: Dictionary = {}
var base_fingerprints: Dictionary = {}
var registry_names: Dictionary = {}
var loaded_scenes: Dictionary = {}

func read_registry_names(path: String) -> void:
	registry_names.clear()
	if not FileAccess.file_exists(path):
		return
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if data is Dictionary and data.get("format", "") == "d3d.asset-registry":
		for asset: Dictionary in data.get("entries", []):
			registry_names[str(asset.get("id", ""))] = str(asset.get("name", ""))

func label_for(node: Node3D) -> String:
	var identity: Dictionary = node.get_meta("d3d_instance", {})
	var id := str(identity.get("instanceId", node.name))
	var name := str(registry_names.get(str(identity.get("assetId", "")), identity.get("name", "")))
	return name + " • " + id if not name.is_empty() else id

func capture_base(root: Node3D) -> void:
	if root != null:
		loaded_scenes[root.get_instance_id()] = {"path": root.scene_file_path, "sha256": FileAccess.get_sha256(root.scene_file_path) if FileAccess.file_exists(root.scene_file_path) else ""}
	for node in instances(root):
		base_fingerprints[node.get_instance_id()] = assembly_sha(node)

static func instances(root: Node) -> Array[Node3D]:
	var found: Array[Node3D] = []
	if root == null:
		return found
	# Actors have their own authoring front. No generated IDs or catalogue copies.
	if root.name == "Actors":
		return found
	if root is Node3D and root.has_meta("d3d_instance"):
		found.append(root)
	for child in root.get_children():
		found.append_array(instances(child))
	return found

static func find_instance(root: Node, id: String) -> Node3D:
	for node in instances(root):
		if str(node.get_meta("d3d_instance", {}).get("instanceId", "")) == id:
			return node
	return null

static func baseline(node: Node3D) -> Transform3D:
	var value: Variant = node.get_meta("d3d_baseline_transform", null)
	if value is Transform3D:
		return value
	var low: Array = node.get_meta("d3d_instance", {}).get("nativeMin", [0, 0])
	return Transform3D(Basis.IDENTITY, Vector3(float(low[0]), 0, float(low[1])))

static func editable(node: Node3D) -> bool:
	return node != null and not bool(node.get_meta("d3d_read_only", false)) and str(node.get_meta("d3d_instance", {}).get("instanceId", "")) != "cabin-east"

static func transform_for(node: Node3D, position: Vector3, rotation_degrees: Vector3, uniform_scale: float) -> Dictionary:
	if not editable(node):
		return {"ok": false, "error": "Instância bloqueada/congelada; transformação preservada."}
	if not position.is_finite() or not rotation_degrees.is_finite() or not is_finite(uniform_scale) or uniform_scale < 0.01 or uniform_scale > 100.0:
		return {"ok": false, "error": "Posição/rotação precisam ser finitas; escala uniforme deve estar entre 0,01 e 100."}
	return {"ok": true, "transform": Transform3D(Basis.from_euler(rotation_degrees * PI / 180.0).scaled(Vector3.ONE * uniform_scale), position)}

static func transform_data(value: Transform3D) -> Dictionary:
	return {"position": _vector(value.origin), "basisColumns": [_vector(value.basis.x), _vector(value.basis.y), _vector(value.basis.z)], "rotationDegrees": _vector(value.basis.get_euler() * 180.0 / PI), "scale": _vector(value.basis.get_scale())}

static func bounds(node: Node3D) -> Dictionary:
	var result := {"initialized": false, "aabb": AABB(), "triangles": 0, "meshes": 0}
	_collect_bounds(node, _parent_transform(node), _parent_visible(node), result)
	var box: AABB = result.aabb
	return {"available": result.initialized, "min": _vector(box.position), "max": _vector(box.end), "size": _vector(box.size), "groundGap": box.position.y, "triangles": result.triangles, "meshes": result.meshes}

static func _parent_transform(node: Node3D) -> Transform3D:
	var value := Transform3D.IDENTITY
	var ancestor := node.get_parent()
	while ancestor != null:
		if ancestor is Node3D:
			value = ancestor.transform * value
		ancestor = ancestor.get_parent()
	return value

static func _parent_visible(node: Node3D) -> bool:
	var ancestor := node.get_parent()
	while ancestor != null:
		if ancestor is Node3D and not ancestor.visible:
			return false
		ancestor = ancestor.get_parent()
	return true

static func _collect_bounds(node: Node, parent_transform: Transform3D, visible: bool, result: Dictionary) -> void:
	var current := parent_transform
	if node is Node3D:
		visible = visible and node.visible
		current = parent_transform * node.transform
	if not visible or bool(node.get_meta("d3d_export_ignore", false)) and str(node.get_meta("d3d_source_mode", "")) != "reference":
		return
	if node is MeshInstance3D and node.mesh != null:
		var box: AABB = current * node.get_aabb()
		result.aabb = result.aabb.merge(box) if result.initialized else box
		result.initialized = true
		result.meshes += 1
		for index in node.mesh.get_surface_count():
			result.triangles += node.mesh.surface_get_array_index_len(index) / 3 if node.mesh.surface_get_array_index_len(index) > 0 else node.mesh.surface_get_array_len(index) / 3
	for child in node.get_children():
		_collect_bounds(child, current, visible, result)

static func assembly_sha(node: Node3D) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes([_parent_transform(node), _parent_visible(node)]))
	_hash_node(node, context, true)
	return context.finish().hex_encode()

static func _hash_node(node: Node, context: HashingContext, visible: bool) -> void:
	if node is Node3D:
		visible = visible and node.visible
	context.update(var_to_bytes([str(node.name), visible, node.transform if node is Node3D else null]))
	if visible and node is MeshInstance3D and node.mesh != null:
		for index in node.mesh.get_surface_count():
			context.update(var_to_bytes(node.mesh.surface_get_arrays(index)))
			var material: Material = node.get_active_material(index)
			if material is StandardMaterial3D:
				context.update(var_to_bytes([material.albedo_color, material.roughness, material.metallic, material.cull_mode, material.transparency, material.shading_mode]))
				if material.albedo_texture != null:
					var image: Image = material.albedo_texture.get_image()
					if image != null:
						context.update(var_to_bytes([image.get_width(), image.get_height(), image.get_format(), image.has_mipmaps()]))
						context.update(image.get_data())
	for child in node.get_children():
		_hash_node(child, context, visible)

func read_application(path: String, scene: Node3D) -> Dictionary:
	# Never infer application from a source GLB, scene visibility or modification time.
	application.clear()
	path = ProjectSettings.globalize_path(path).simplify_path()
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	if not parsed is Dictionary or parsed.get("format", "") != "d3d.godot-pack-receipt" or parsed.get("schemaVersion", 0) != 1 or not parsed.get("models") is Array:
		return {"ok": false, "error": "Recibo de aplicação incompatível ou ausente."}
	var directory := path.get_base_dir()
	var manifest := directory.path_join("d3d-maps/tristram.ini")
	if str(parsed.get("manifestSha256", "")).is_empty() or FileAccess.get_sha256(manifest) != parsed.get("manifestSha256", ""):
		return {"ok": false, "error": "Manifesto aplicado diverge do recibo; nenhum estado aplicado foi assumido."}
	var ini := _read_manifest(manifest)
	if ini.is_empty():
		return {"ok": false, "error": "Manifesto aplicado não pode ser lido."}
	var bindings: Dictionary = {}
	var declared: Array = ini.get("Scene", {}).get("instance", [])
	if declared.size() != parsed.models.size() or ini.size() != declared.size() + 1:
		return {"ok": false, "error": "Cobertura do manifesto diverge do recibo; nenhum vínculo parcial foi assumido."}
	for raw: Variant in parsed.models:
		if not raw is Dictionary:
			return {"ok": false, "error": "Entrada inválida no recibo."}
		var id := str(raw.get("instanceId", ""))
		var relative := str(raw.get("path", "")).replace("\\", "/")
		var known := find_instance(scene, id)
		if known == null or not declared.has(id) or bindings.has(id) or not relative.begins_with("d3d-models/editor/") or relative.contains("..") or relative.contains(":") or not relative.ends_with(".d3d"):
			return {"ok": false, "error": "Vínculo desconhecido/duplicado ou caminho inválido: " + id}
		var identity: Dictionary = known.get_meta("d3d_instance")
		var section: Dictionary = ini.get(id, {})
		if section.is_empty() or str(section.get("assetId", "")) != identity.get("assetId", "") or str(section.get("variantId", "")) != identity.get("variantId", ""):
			return {"ok": false, "error": "Asset/variante do manifesto não corresponde ao snapshot: " + id}
		for key in ["nativeMin", "nativeMax"]:
			var native: Array = identity.get(key, [])
			var expected := "%d,%d" % [int(native[0]), int(native[1])] if native.size() == 2 else ""
			if str(section.get(key, "")).replace(" ", "") != expected:
				return {"ok": false, "error": "Limites nativos divergentes: " + id}
		var model_path := directory.path_join(relative)
		var expected_hash := str(raw.get("sha256", ""))
		if expected_hash.length() != 64 or not FileAccess.file_exists(model_path) or FileAccess.get_sha256(model_path) != expected_hash or str(section.get("sha256", "")) != expected_hash or str(section.get("model", "")) != relative:
			return {"ok": false, "error": "Arquivo aplicado diverge do manifesto/recibo: " + id}
		bindings[id] = {"path": model_path, "sha256": expected_hash, "revision": str(section.get("revision", "")), "identity": identity.duplicate(true)}
	if bindings.is_empty():
		return {"ok": false, "error": "Recibo sem vínculos aplicados."}
	application = {"receiptPath": path, "receiptSha256": FileAccess.get_sha256(path), "manifestPath": manifest, "manifestSha256": parsed.manifestSha256, "gameExecutableSha256": parsed.get("gameExecutableSha256", ""), "bindings": bindings, "status": "verified-files-on-disk-runtime-not-proven"}
	return {"ok": true, "bindings": bindings.size()}

static func _read_manifest(path: String) -> Dictionary:
	# Native INI is raw key=value, not Godot ConfigFile Variant syntax.
	var sections: Dictionary = {}
	var section := ""
	for raw_line in FileAccess.get_file_as_string(path).split("\n"):
		var line := raw_line.strip_edges()
		if line.is_empty() or line.begins_with(";") or line.begins_with("#"):
			continue
		if line.begins_with("[") and line.ends_with("]"):
			section = line.substr(1, line.length() - 2)
			if sections.has(section):
				return {}
			sections[section] = {}
		elif not section.is_empty() and line.contains("="):
			var separator := line.find("=")
			var key := line.left(separator).strip_edges()
			if section == "Scene" and key == "instance":
				var ids: Array = sections[section].get(key, [])
				var id := line.substr(separator + 1).strip_edges()
				if ids.has(id) or id.is_empty():
					return {}
				ids.append(id)
				sections[section][key] = ids
				continue
			if sections[section].has(key):
				return {}
			sections[section][key] = line.substr(separator + 1).strip_edges()
		else:
			return {}
	var header: Dictionary = sections.get("Scene", {})
	return sections if header.get("format", "") == "d3d.town-map" and header.get("schemaVersion", "") == "1" and header.get("edition", "") == "retail" else {}

static func application_is_current(evidence: Dictionary) -> bool:
	if evidence.is_empty() or FileAccess.get_sha256(str(evidence.get("receiptPath", ""))) != evidence.get("receiptSha256", "") or FileAccess.get_sha256(str(evidence.get("manifestPath", ""))) != evidence.get("manifestSha256", ""):
		return false
	for binding: Dictionary in evidence.get("bindings", {}).values():
		if FileAccess.get_sha256(str(binding.get("path", ""))) != binding.get("sha256", ""):
			return false
	return not evidence.get("bindings", {}).is_empty()

func applied_preview(snapshot_path: String) -> Dictionary:
	if not application_is_current(application):
		return {"ok": false, "errors": ["Carregue e verifique um recibo de aplicação primeiro."]}
	var built: Dictionary = Snapshot.new().build(snapshot_path)
	if not built.get("ok", false):
		return built
	var root: Node3D = built.root
	for id: String in application.bindings:
		var binding: Dictionary = application.bindings[id]
		var node := find_instance(root, id)
		if node == null or FileAccess.get_sha256(binding.path) != binding.sha256:
			root.free()
			return {"ok": false, "errors": ["Arquivo aplicado mudou após verificação: " + id]}
		var decoded: Dictionary = Reader.read_model(binding.path)
		if not decoded.get("ok", false):
			root.free()
			return {"ok": false, "errors": [decoded.get("error", "D3D inválido")]}
		for child in node.get_children():
			child.free()
		var mesh: MeshInstance3D = decoded.node
		mesh.name = "AppliedD3D"
		# Same auxiliary lighting as the existing preview, no installed material edit.
		mesh.material_override.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
		node.add_child(mesh)
		mesh.owner = root
		var identity: Dictionary = node.get_meta("d3d_instance").duplicate(true)
		identity["sha256"] = binding.sha256
		identity["revision"] = binding.revision
		identity["external"] = true
		node.set_meta("d3d_instance", identity)
		node.set_meta("map_builder_applied_file", binding.path)
		node.set_meta("map_builder_applied_sha256", binding.sha256)
		base_fingerprints[node.get_instance_id()] = assembly_sha(node)
	root.set_meta("map_builder_application", application.duplicate(true))
	return {"ok": true, "root": root}

func save_proposal(root: Node3D, node: Node3D, revision: String, note: String, review_state: String, reviewer: String, mode: String, output_directory: String = PROPOSALS) -> Dictionary:
	if node == null or root == null or not root.transform.is_equal_approx(Transform3D.IDENTITY):
		return {"ok": false, "error": "Raiz Tristram deve permanecer sem transformação."}
	var valid_revision := RegEx.new()
	valid_revision.compile("^[a-z0-9][a-z0-9-]{0,63}$")
	if valid_revision.search(revision) == null or mode == "original":
		return {"ok": false, "error": "Informe revisão a-z/0-9/hífen (1–64); original é somente leitura."}
	if review_state not in ["pending", "reviewed", "changes-requested", "assembly-approved"] or review_state != "pending" and reviewer.strip_edges().is_empty():
		return {"ok": false, "error": "Revisão de montagem exige responsável identificado."}
	if review_state == "assembly-approved" and note.strip_edges().is_empty():
		return {"ok": false, "error": "Aprovação exige escopo/evidência na nota. Não promove o asset no catálogo."}
	var scale_value := node.transform.basis.get_scale()
	var world_transform := _parent_transform(node) * node.transform
	var world_scale := world_transform.basis.get_scale()
	if not node.transform.is_finite() or not world_transform.is_finite() or not is_equal_approx(scale_value.x, scale_value.y) or not is_equal_approx(scale_value.x, scale_value.z) or scale_value.x <= 0 or node.transform.basis.determinant() <= 0 or not is_equal_approx(world_scale.x, world_scale.y) or not is_equal_approx(world_scale.x, world_scale.z) or world_scale.x <= 0 or world_transform.basis.determinant() <= 0:
		return {"ok": false, "error": "Proposta exige transformação finita, escala uniforme positiva e sem espelhamento."}
	var application_evidence: Dictionary = root.get_meta("map_builder_application", {})
	if mode == "applied" and not application_is_current(application_evidence):
		return {"ok": false, "error": "Recibo/manifesto/modelos mudaram. Recarregue explicitamente a aplicação; proposta não gravada."}
	var identity: Dictionary = node.get_meta("d3d_instance", {}).duplicate(true)
	var id := str(identity.get("instanceId", ""))
	if valid_revision.search(id) == null:
		return {"ok": false, "error": "ID inválido; o catálogo não foi alterado."}
	var fingerprint := assembly_sha(node)
	var snapshot_path := str(root.get_meta("d3d_snapshot_path", ""))
	var scene_path := root.scene_file_path
	var document := {"format": "d3d.map-builder-proposal", "schemaVersion": 1, "instanceId": id, "identity": identity, "revision": revision, "state": "candidate", "sourceMode": mode, "savedAtUtc": Time.get_datetime_string_from_system(true), "assemblySha256": fingerprint, "base": {"scenePath": scene_path, "sceneSha256": FileAccess.get_sha256(scene_path) if FileAccess.file_exists(scene_path) else "", "snapshotPath": snapshot_path, "snapshotSha256Recorded": str(root.get_meta("d3d_snapshot_sha256", "")), "snapshotSha256Actual": FileAccess.get_sha256(snapshot_path) if FileAccess.file_exists(snapshot_path) else "", "transform": transform_data(baseline(node)), "assemblySha256": str(base_fingerprints.get(node.get_instance_id(), ""))}, "visual": {"transform": transform_data(node.transform), "worldTransform": transform_data(world_transform), "bounds": bounds(node), "reviewSelectedRevision": str(node.get_meta("review_selected_revision", "")), "reviewSelectedSourceSha256": str(node.get_meta("review_selected_sha256", ""))}, "collision": {"state": "native-fixed", "nativeMin": identity.get("nativeMin", []), "nativeMax": identity.get("nativeMax", []), "visualTransformDiverges": not world_transform.is_equal_approx(baseline(node)), "simulationModified": false}, "review": {"state": review_state, "reviewer": reviewer.strip_edges(), "scopeAndEvidence": note.strip_edges(), "assemblySha256": fingerprint, "catalogAcceptanceModified": false}, "application": application_evidence, "exported": false, "installed": false, "note": "Salvar proposta não aprova, exporta ou aplica. Revisão de montagem vale apenas para assemblySha256; não altera seleção/aceitação do catálogo."}
	document.base["snapshotPreparation"] = root.get_meta("d3d_snapshot", {}).get("mapBuilderPreparation", {})
	document.base["loadedScene"] = loaded_scenes.get(root.get_instance_id(), {})
	var directory := ProjectSettings.globalize_path(output_directory).simplify_path().path_join(id)
	if not directory.begins_with(ProjectSettings.globalize_path("res://local/").simplify_path() + "/"):
		return {"ok": false, "error": "Propostas devem ficar em res://local/ (privado)."}
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		return {"ok": false, "error": "Não foi possível criar a pasta privada da proposta."}
	var path := directory.path_join("%s-%d-%s.json" % [revision, Time.get_ticks_usec(), fingerprint.left(12)])
	if FileAccess.file_exists(path):
		return {"ok": false, "error": "Proposta imutável já existe; arquivo preservado."}
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return {"ok": false, "error": "Não foi possível gravar a proposta."}
	file.store_string(JSON.stringify(document, "\t", true, true) + "\n")
	file.close()
	return {"ok": true, "path": path, "sha256": FileAccess.get_sha256(path), "document": document}

static func _vector(value: Vector3) -> Array:
	return [value.x, value.y, value.z]
