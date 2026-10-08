@tool
extends RefCounted
## Reads the private runtime snapshot; never writes native simulation data.

const MATERIAL_NAMES := ["Parede", "Telhado", "Pedra", "Madeira", "Água"]
const ROLE_NAMES := ["Exterior", "Interior", "Face_inferior"]
const DETAIL_NAMES := ["Base", "Alvenaria", "Palha", "Porta", "Vidro", "Estrutura", "Barril_madeira", "Barril_aro", "Fundação", "Vela_cera", "Vela_suporte", "Vela_pavio", "Fogo_núcleo", "Fogo_ponta"]


func build(path: String) -> Dictionary:
	# Image.load recebe o arquivo local real, sem passar pela importação
	# automática do editor. PNGs privados não são recursos de um jogo Godot.
	path = ProjectSettings.globalize_path(path).simplify_path()
	var errors: Array[String] = []
	var warnings: Array[String] = []
	var parsed: Variant = _read_json(path, errors)
	if not parsed is Dictionary:
		if errors.is_empty():
			errors.append("A raiz do snapshot precisa ser um objeto JSON.")
		return _result(null, errors, warnings)
	var snapshot: Dictionary = parsed
	if snapshot.get("format", "") != "d3d.town-snapshot" or snapshot.get("schemaVersion", 0) != 1:
		errors.append("Snapshot incompatível: esperado d3d.town-snapshot, versão 1.")
	if not snapshot.get("models") is Array or not snapshot.get("collision") is Array:
		errors.append("Snapshot precisa de models e collision em listas.")
	var grid: Variant = snapshot.get("gridSize", [])
	if not _pair(grid, true) or int(grid[0]) <= 0 or int(grid[1]) <= 0:
		errors.append("gridSize inválido no snapshot.")
	if not errors.is_empty():
		return _result(null, errors, warnings)
	var registry: Dictionary = _registry(warnings)
	var root := Node3D.new()
	root.name = "Tristram"
	root.set_meta("d3d_snapshot_path", path)
	root.set_meta("d3d_snapshot_sha256", _file_hash(path))
	root.set_meta("d3d_grid_size", grid)
	root.set_meta("d3d_edition", str(snapshot.get("edition", "retail")))
	root.set_meta("d3d_collision_locked", true)
	root.set_meta("d3d_snapshot_fixture", bool(snapshot.get("syntheticFixture", false)))
	root.set_meta("d3d_snapshot", _without_geometry(snapshot, ["models", "collision", "ground"]))
	var architecture := Node3D.new()
	architecture.name = "Architecture"
	root.add_child(architecture)
	architecture.owner = root
	var seen: Dictionary = {}
	var models: Array = snapshot["models"]
	for index in models.size():
		var raw: Variant = models[index]
		if not raw is Dictionary:
			errors.append("models[%d] precisa ser um objeto." % index)
			continue
		var model: Dictionary = raw
		var identity := str(model.get("instanceId", ""))
		var prefix := "models[%d] (%s)" % [index, identity]
		if not _valid_id(identity) or seen.has(identity):
			errors.append("%s: instanceId inválido ou duplicado." % prefix)
			continue
		seen[identity] = true
		if not _validate_model(model, prefix, errors):
			continue
		_build_model(model, architecture, root, path, registry, errors, warnings)
	if snapshot.has("ground"):
		_build_ground(snapshot["ground"], grid, root, path, errors, warnings)
	_build_collision(snapshot["collision"], grid, root, errors)
	if architecture.get_child_count() == 0:
		errors.append("Snapshot não contém arquitetura utilizável.")
	if not errors.is_empty():
		root.free()
		return _result(null, errors, warnings)
	warnings.append("Prévia auxiliar: os materiais técnicos e a iluminação do Godot não comprovam a aparência na paleta do jogo.")
	root.set_meta("d3d_snapshot_warnings", warnings)
	return _result(root, errors, warnings)


func _validate_model(model: Dictionary, prefix: String, errors: Array[String]) -> bool:
	var before := errors.size()
	for field in ["assetId", "variantId", "revision", "sha256", "texture"]:
		if not model.get(field) is String:
			errors.append("%s: %s precisa ser texto." % [prefix, field])
	if not _pair(model.get("nativeMin", []), true) or not _pair(model.get("nativeMax", []), true):
		errors.append("%s: nativeMin/nativeMax inválidos." % prefix)
	else:
		var minimum: Array = model["nativeMin"]
		var maximum: Array = model["nativeMax"]
		if minimum[0] > maximum[0] or minimum[1] > maximum[1]:
			errors.append("%s: limites nativos invertidos." % prefix)
	if not _integer(model.get("kind")) or int(model["kind"]) < 0 or int(model["kind"]) > 6:
		errors.append("%s: kind desconhecido (esperado 0..6)." % prefix)
	if not model.get("external") is bool:
		errors.append("%s: external precisa ser booleano." % prefix)
	var hash_value := str(model.get("sha256", ""))
	if not hash_value.is_empty() and not _valid_hash(hash_value):
		errors.append("%s: sha256 inválido." % prefix)
	if not model.get("triangles") is Array or model["triangles"].is_empty():
		errors.append("%s: nenhuma lista de triângulos utilizável." % prefix)
		return false
	var triangles: Array = model["triangles"]
	for index in triangles.size():
		var triangle: Variant = triangles[index]
		if not triangle is Array or triangle.size() != 18:
			errors.append("%s: triângulo %d deve ter 18 valores (3 × x,height,z,u,v + material,role,detail)." % [prefix, index])
			return false
		for value in triangle:
			if not _number(value):
				errors.append("%s: triângulo %d contém número inválido." % [prefix, index])
				return false
		if not _integer(triangle[15]) or not _integer(triangle[16]) or not _integer(triangle[17]) or int(triangle[15]) not in range(5) or int(triangle[16]) not in range(3) or int(triangle[17]) not in range(DETAIL_NAMES.size()):
			errors.append("%s: triângulo %d tem material/role/detail desconhecido; nenhum atributo foi descartado." % [prefix, index])
			return false
	return errors.size() == before


func _build_model(model: Dictionary, architecture: Node3D, root: Node3D, path: String, registry: Dictionary, errors: Array[String], warnings: Array[String]) -> void:
	var identity := str(model["instanceId"])
	var instance := Node3D.new()
	instance.name = identity
	architecture.add_child(instance)
	instance.owner = root
	instance.add_to_group("tristram_instance", true)
	var minimum: Array = model["nativeMin"]
	var origin := Vector3(float(minimum[0]), 0.0, float(minimum[1]))
	instance.position = origin
	instance.set_meta("d3d_instance", _without_geometry(model, ["triangles"]))
	instance.set_meta("d3d_override", false)
	instance.set_meta("d3d_revision", str(model["revision"]))
	instance.set_meta("d3d_read_only", bool(model.get("readOnly", false)))
	instance.set_meta("d3d_read_only_reason", str(model.get("readOnlyReason", "")))
	instance.set_meta("d3d_baseline_transform", instance.transform)
	instance.set_meta("d3d_native_origin", origin)
	instance.set_meta("d3d_source_triangle_count", model["triangles"].size())
	var catalog: Dictionary = registry.get(str(model["assetId"]), {})
	var variants: Array[String] = []
	for variant: Dictionary in catalog.get("variants", []):
		variants.append(str(variant.get("id", "")))
	var variant_valid := variants.has(str(model["variantId"]))
	instance.set_meta("d3d_registry_status", str(catalog.get("status", "desconhecido")))
	instance.set_meta("d3d_approved_revision", str(catalog.get("approvedRevision", "")) if catalog.get("approvedRevision") != null else "")
	instance.set_meta("d3d_variant_valid", variant_valid)
	if not variant_valid:
		warnings.append("%s: vínculo %s / %s não consta no catálogo existente. Não foi criado um ID alternativo." % [identity, model["assetId"], model["variantId"]])
	var texture: Texture2D = _model_texture(model, path, errors)
	if not bool(model["external"]) or texture == null:
		warnings.append("%s: materiais técnicos opacos; o snapshot não inclui o atlas de materiais nativos." % identity)
	var buckets: Dictionary = {}
	var fire := bool(model.get("hasFireSources", model.get("HasFireSources", false)))
	var fire_sources: Variant = model.get("fireSources", model.get("FireSources", []))
	if fire_sources is Array and not fire_sources.is_empty():
		fire = true
	var degenerate := 0
	for triangle: Array in model["triangles"]:
		var material_id := int(triangle[15])
		var role_id := int(triangle[16])
		var detail_id := int(triangle[17])
		if detail_id == 12 or detail_id == 13:
			fire = true
		var key := "%d_%d_%d" % [role_id, material_id, detail_id]
		if not buckets.has(key):
			buckets[key] = {"vertices": PackedVector3Array(), "normals": PackedVector3Array(), "uv": PackedVector2Array(), "material": material_id, "role": role_id, "detail": detail_id}
		var bucket: Dictionary = buckets[key]
		var points: Array[Vector3] = []
		for vertex in 3:
			var offset := vertex * 5
			points.append(Vector3(float(triangle[offset]), float(triangle[offset + 1]), float(triangle[offset + 2])) - origin)
		var normal := (points[1] - points[0]).cross(points[2] - points[0])
		if normal.length_squared() < 0.0000000001:
			degenerate += 1
			normal = Vector3.UP
		else:
			normal = normal.normalized()
		for vertex in 3:
			bucket["vertices"].append(points[vertex])
			bucket["normals"].append(normal)
			bucket["uv"].append(Vector2(float(triangle[vertex * 5 + 3]), float(triangle[vertex * 5 + 4])))
	instance.set_meta("d3d_has_fire", fire)
	if degenerate > 0:
		warnings.append("%s: %d triângulos degenerados preservados do snapshot." % [identity, degenerate])
	for key: String in buckets:
		var bucket: Dictionary = buckets[key]
		var material_id: int = bucket["material"]
		var role_id: int = bucket["role"]
		var detail_id: int = bucket["detail"]
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = bucket["vertices"]
		arrays[Mesh.ARRAY_NORMAL] = bucket["normals"]
		arrays[Mesh.ARRAY_TEX_UV] = bucket["uv"]
		var mesh := ArrayMesh.new()
		mesh.resource_name = "%s_%s" % [identity, key]
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(0, _material(material_id, role_id, detail_id, texture if bool(model["external"]) and role_id == 0 and detail_id < 9 else null))
		var geometry := MeshInstance3D.new()
		geometry.name = "%s_%s_%s" % [ROLE_NAMES[role_id], MATERIAL_NAMES[material_id], DETAIL_NAMES[detail_id]]
		geometry.mesh = mesh
		geometry.set_meta("d3d_surface", {"material": material_id, "role": role_id, "detail": detail_id})
		geometry.set_meta("d3d_baseline", true)
		instance.add_child(geometry)
		geometry.owner = root
	var light_count := _add_explicit_lights(fire_sources, instance, root, origin, warnings)
	instance.set_meta("d3d_snapshot_light_count", light_count)
	if fire:
		warnings.append("%s: contém fogo/luz adjunta nativa; a exportação v1 deve recusar seu override. %d fontes explícitas de luz na prévia." % [identity, light_count])


func _model_texture(model: Dictionary, path: String, errors: Array[String]) -> Texture2D:
	var texture_path := str(model.get("texture", ""))
	if texture_path.is_empty():
		return null
	if texture_path.is_absolute_path() or texture_path.begins_with("res://") or texture_path.begins_with("user://"):
		errors.append("%s: textura deve ter caminho relativo ao snapshot." % model["instanceId"])
		return null
	var resolved := path.get_base_dir().path_join(texture_path).simplify_path()
	var image := Image.new()
	var code := image.load(resolved)
	if code != OK:
		errors.append("%s: não foi possível ler a textura %s (erro %d)." % [model["instanceId"], texture_path, code])
		return null
	# Snapshot texture is the opaque imported RGB atlas. Do not silently flatten alpha.
	if image.detect_alpha() != Image.ALPHA_NONE:
		errors.append("%s: textura do snapshot contém transparência não suportada; preparação deve preservar o atlas RGB real." % model["instanceId"])
		return null
	return ImageTexture.create_from_image(image)


func _material(material_id: int, role_id: int, detail_id: int, texture: Texture2D) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.resource_name = "%s_%s_%s" % [ROLE_NAMES[role_id], MATERIAL_NAMES[material_id], DETAIL_NAMES[detail_id]]
	material.roughness = 1.0
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	material.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	if texture != null:
		material.albedo_texture = texture
		material.albedo_color = Color.WHITE
	else:
		var colors := [Color("827766"), Color("635a49"), Color("7d7a73"), Color("68513a"), Color("315d68")]
		material.albedo_color = colors[material_id]
		match detail_id:
			2: material.albedo_color = Color("8b7c56")
			3: material.albedo_color = Color("394a61")
			4: material.albedo_color = Color("7c6746")
			6: material.albedo_color = Color("6e4c2f")
			7: material.albedo_color = Color("494744")
			9: material.albedo_color = Color("b2a67f")
			10: material.albedo_color = Color("79746b")
			11: material.albedo_color = Color("231d17")
			12, 13:
				material.albedo_color = Color("ddc47e") if detail_id == 12 else Color("6b4a18")
				material.emission_enabled = true
				material.emission = material.albedo_color
	return material


func _build_ground(raw: Variant, grid: Array, root: Node3D, path: String, errors: Array[String], warnings: Array[String]) -> void:
	if not raw is Dictionary or not raw.get("textures") is Array or not raw.get("tiles") is Array:
		errors.append("ground inválido: esperado objeto com listas textures e tiles.")
		return
	var ground: Dictionary = raw
	var layer := Node3D.new()
	layer.name = "GroundReference"
	root.add_child(layer)
	layer.owner = root
	layer.add_to_group("tristram_reference", true)
	layer.set_meta("d3d_export_ignore", true)
	layer.set_meta("d3d_locked_reference", true)
	layer.set_meta("d3d_source", str(ground.get("source", "")))
	layer.set_meta("_edit_lock_", true)
	layer.set_meta("_edit_group_", true)
	var buckets: Dictionary = {}
	for index in ground["textures"].size():
		var entry: Variant = ground["textures"][index]
		if not entry is Dictionary or not entry.get("key") is String or not entry.get("path") is String:
			errors.append("ground.textures[%d]: key/path inválidos." % index)
			continue
		var key: String = entry["key"]
		var relative: String = entry["path"]
		if not _valid_id(key) or buckets.has(key):
			errors.append("ground.textures[%d]: chave inválida ou duplicada." % index)
			continue
		if relative.is_empty() or relative.is_absolute_path() or relative.begins_with("res://") or relative.begins_with("user://"):
			errors.append("ground.textures[%d]: caminho deve ser relativo ao snapshot." % index)
			continue
		if not _integer(entry.get("width")) or not _integer(entry.get("height")) or int(entry["width"]) <= 0 or int(entry["height"]) <= 0:
			errors.append("ground.textures[%d]: width/height inválidos." % index)
			continue
		var image := Image.new()
		var resolved := path.get_base_dir().path_join(relative).simplify_path()
		var code := image.load(resolved)
		if code != OK:
			errors.append("ground.textures[%d]: não foi possível ler %s (erro %d)." % [index, relative, code])
			continue
		if image.get_width() != int(entry["width"]) or image.get_height() != int(entry["height"]):
			errors.append("ground.textures[%d]: dimensões PNG divergem do snapshot." % index)
			continue
		var material := StandardMaterial3D.new()
		material.resource_name = "Chão_%s" % key
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		material.texture_repeat = false
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.albedo_texture = ImageTexture.create_from_image(image)
		match image.detect_alpha():
			Image.ALPHA_BIT:
				material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
				material.alpha_scissor_threshold = 0.5
			Image.ALPHA_BLEND:
				material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		buckets[key] = {"vertices": PackedVector3Array(), "uv": PackedVector2Array(), "normals": PackedVector3Array(), "indices": PackedInt32Array(), "material": material, "sha256": _file_hash(resolved), "path": relative, "tiles": 0}
	var tile_count := 0
	for index in ground["tiles"].size():
		var tile: Variant = ground["tiles"][index]
		if not tile is Array or tile.size() != 3 or not _integer(tile[0]) or not _integer(tile[1]) or not tile[2] is String:
			errors.append("ground.tiles[%d]: esperado [x,z,key]." % index)
			continue
		if tile[0] < 0 or tile[1] < 0 or tile[0] >= grid[0] or tile[1] >= grid[1] or not buckets.has(tile[2]):
			errors.append("ground.tiles[%d]: célula ou textura desconhecida." % index)
			continue
		var bucket: Dictionary = buckets[tile[2]]
		var base: int = bucket["vertices"].size()
		var x := float(tile[0])
		var z := float(tile[1])
		var corners := [Vector3(x - 1.46875, 0.0, z - 0.46875), Vector3(x - 0.46875, 0.0, z - 1.46875), Vector3(x + 0.53125, 0.0, z - 0.46875), Vector3(x - 0.46875, 0.0, z + 0.53125)]
		var uvs := [Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]
		for vertex in 4:
			bucket["vertices"].append(corners[vertex])
			bucket["uv"].append(uvs[vertex])
			bucket["normals"].append(Vector3.UP)
		for offset in [0, 1, 2, 0, 2, 3]:
			bucket["indices"].append(base + offset)
		bucket["tiles"] += 1
		tile_count += 1
	for key: String in buckets:
		var bucket: Dictionary = buckets[key]
		if bucket["vertices"].is_empty():
			continue
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = bucket["vertices"]
		arrays[Mesh.ARRAY_NORMAL] = bucket["normals"]
		arrays[Mesh.ARRAY_TEX_UV] = bucket["uv"]
		arrays[Mesh.ARRAY_INDEX] = bucket["indices"]
		var mesh := ArrayMesh.new()
		mesh.resource_name = "Chão_%s" % key
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(0, bucket["material"])
		var geometry := MeshInstance3D.new()
		geometry.name = "Chão_%s" % key
		geometry.mesh = mesh
		geometry.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		geometry.set_meta("d3d_export_ignore", true)
		geometry.set_meta("d3d_locked_reference", true)
		geometry.set_meta("_edit_lock_", true)
		geometry.set_meta("d3d_ground_texture", key)
		geometry.set_meta("d3d_ground_texture_path", bucket["path"])
		geometry.set_meta("d3d_ground_texture_sha256", bucket["sha256"])
		geometry.set_meta("d3d_ground_tile_count", bucket["tiles"])
		layer.add_child(geometry)
		geometry.owner = root
	root.set_meta("d3d_ground_tile_count", tile_count)
	root.set_meta("d3d_ground_texture_count", layer.get_child_count())
	warnings.append("Chão: referência plana das texturas reais de piso, com transparência preservada; não edita terreno/colisão nativos e não integra o pacote de arquitetura.")


func _build_collision(cells: Array, grid: Array, root: Node3D, errors: Array[String]) -> void:
	var layer := Node3D.new()
	layer.name = "CollisionReference"
	root.add_child(layer)
	layer.owner = root
	layer.add_to_group("tristram_reference", true)
	layer.set_meta("d3d_export_ignore", true)
	layer.set_meta("d3d_native_cells", cells.duplicate(true))
	layer.set_meta("d3d_locked_reference", true)
	layer.set_meta("_edit_lock_", true)
	layer.set_meta("_edit_group_", true)
	var vertices := PackedVector3Array()
	for index in cells.size():
		var cell: Variant = cells[index]
		if not _pair(cell, true) or cell[0] < 0 or cell[1] < 0 or cell[0] >= grid[0] or cell[1] >= grid[1]:
			errors.append("collision[%d]: célula nativa inválida." % index)
			continue
		var x := float(cell[0])
		var z := float(cell[1])
		var corners := [Vector3(x - 0.5, 0.025, z - 0.5), Vector3(x + 0.5, 0.025, z - 0.5), Vector3(x + 0.5, 0.025, z + 0.5), Vector3(x - 0.5, 0.025, z + 0.5)]
		for edge in 4:
			vertices.append(corners[edge])
			vertices.append(corners[(edge + 1) % 4])
	if vertices.is_empty():
		return
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_LINES, arrays)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color("e6a251")
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	mesh.surface_set_material(0, material)
	var outlines := MeshInstance3D.new()
	outlines.name = "Células_bloqueadas"
	outlines.mesh = mesh
	outlines.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	outlines.set_meta("d3d_export_ignore", true)
	outlines.set_meta("_edit_lock_", true)
	layer.add_child(outlines)
	outlines.owner = root


func _add_explicit_lights(sources: Variant, instance: Node3D, root: Node3D, origin: Vector3, warnings: Array[String]) -> int:
	if not sources is Array:
		return 0
	var count := 0
	for source: Variant in sources:
		if not source is Dictionary:
			continue
		var light_data: Variant = source.get("light", source)
		if not light_data is Dictionary:
			continue
		var position: Variant = light_data.get("position", [])
		var color: Variant = light_data.get("color", [])
		var radius: Variant = light_data.get("radius", light_data.get("range", null))
		var intensity: Variant = light_data.get("intensity", null)
		if not _tuple(position, 3) or not _tuple(color, 3) or not _number(radius) or not _number(intensity) or float(radius) <= 0.0 or float(intensity) < 0.0:
			warnings.append("%s: fonte de fogo sem posição/cor/raio/intensidade explícitos; nenhuma luz foi inventada." % instance.name)
			continue
		var light := OmniLight3D.new()
		light.name = "Luz_snapshot_%d" % count
		light.position = Vector3(float(position[0]), float(position[1]), float(position[2])) - origin
		light.light_color = Color(float(color[0]), float(color[1]), float(color[2]))
		light.light_energy = float(intensity)
		light.omni_range = float(radius)
		light.set_meta("d3d_export_ignore", true)
		light.set_meta("d3d_snapshot_light", true)
		light.set_meta("_edit_lock_", true)
		instance.add_child(light)
		light.owner = root
		count += 1
	return count


func _registry(warnings: Array[String]) -> Dictionary:
	var project_dir := ProjectSettings.globalize_path("res://")
	var path := project_dir.path_join("../../assets/registry.json").simplify_path()
	if not FileAccess.file_exists(path):
		# The preparer may copy this same catalog for a portable local workspace.
		path = project_dir.path_join("local/catalog.json").simplify_path()
	var failures: Array[String] = []
	var raw: Variant = _read_json(path, failures)
	var result: Dictionary = {}
	if not raw is Dictionary or not raw.get("entries") is Array:
		warnings.append("Catálogo assets/registry.json indisponível; os vínculos não puderam ser conferidos.")
		return result
	for entry: Variant in raw["entries"]:
		if entry is Dictionary and entry.get("id") is String:
			result[entry["id"]] = entry
	return result


func _read_json(path: String, errors: Array[String]) -> Variant:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		errors.append("Não foi possível abrir %s (erro %d). Prepare o snapshot local pelo launcher do projeto." % [path, FileAccess.get_open_error()])
		return null
	var parser := JSON.new()
	var code := parser.parse(file.get_as_text())
	if code != OK:
		errors.append("JSON inválido em %s:%d: %s" % [path, parser.get_error_line(), parser.get_error_message()])
		return null
	return parser.data


func _without_geometry(source: Dictionary, excluded: Array) -> Dictionary:
	var result: Dictionary = {}
	for key: Variant in source:
		if key not in excluded:
			result[key] = source[key]
	return result.duplicate(true)


func _file_hash(path: String) -> String:
	return FileAccess.get_sha256(path)


func _valid_id(value: String) -> bool:
	if value.is_empty() or value.length() > 96:
		return false
	for character in value:
		if character not in "abcdefghijklmnopqrstuvwxyz0123456789-":
			return false
	return true


func _valid_hash(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value.to_lower():
		if character not in "0123456789abcdef":
			return false
	return true


func _number(value: Variant) -> bool:
	return (value is int or value is float) and is_finite(float(value))


func _integer(value: Variant) -> bool:
	return _number(value) and float(value) == floor(float(value))


func _tuple(value: Variant, count: int) -> bool:
	if not value is Array or value.size() != count:
		return false
	for item: Variant in value:
		if not _number(item):
			return false
	return true


func _pair(value: Variant, integer: bool) -> bool:
	if not _tuple(value, 2):
		return false
	return not integer or (_integer(value[0]) and _integer(value[1]))


func _result(root: Node3D, errors: Array[String], warnings: Array[String]) -> Dictionary:
	return {"ok": root != null and errors.is_empty(), "root": root, "errors": errors, "warnings": warnings}
