@tool
extends RefCounted
## Exportador v1: somente arquitetura explicitamente escolhida para substituição.
## Coordenadas são medidas no scene_root de autoria, antes de qualquer clone de prévia.

const MAX_TRIANGLES := 20000
const MAX_TEXTURE_SIDE := 2048
const REGISTRY_PATH := "res://../../assets/registry.json"
# Vínculos de cena do contrato GODOT-BRIDGE; variantes vêm exclusivamente do registry.
const NATIVE_INSTANCES := {
	"tavern-main": ["tristram.arch.tavern", [46, 54], [53, 63]],
	"tavern-wing": ["tristram.arch.tavern", [53, 56], [56, 61]],
	"smithy-house": ["tristram.arch.smithy", [60, 56], [71, 60]],
	"smithy-forge": ["tristram.arch.smithy", [60, 60], [63, 63]],
	"house-gillian": ["tristram.arch.house-common", [36, 64], [42, 68]],
	"house-pepin": ["tristram.arch.house-pepin", [46, 74], [55, 80]],
	"house-adria": ["tristram.arch.house-adria", [74, 16], [79, 21]],
	"house-north": ["tristram.arch.house-common", [46, 40], [54, 46]],
	"house-southeast": ["tristram.arch.house-common", [67, 78], [72, 82]],
	"cabin-west": ["tristram.arch.cabin", [26, 48], [30, 52]],
	"cabin-east": ["tristram.arch.cabin", [70, 66], [74, 72]],
	"well": ["tristram.arch.well", [60, 70], [61, 71]],
	"cathedral": ["tristram.arch.cathedral-exterior", [15, 14], [29, 28]],
	"catacombs-entrance": ["tristram.arch.catacombs-entrance", [48, 18], [50, 21]],
}
# Associação da instância à variante existente no contrato, sem estado de aprovação
# ou reservas duplicadas. A presença destes IDs é conferida no registry a cada export.
const INSTANCE_VARIANTS := {
	"tavern-main": ["main-with-wing"], "tavern-wing": ["main-with-wing"],
	"smithy-house": ["main-with-open-bay"], "smithy-forge": ["main-with-open-bay"],
	"house-gillian": ["gillian"], "house-pepin": ["pepin-hip-roof"],
	"house-adria": ["adria-open-shed"], "house-north": ["north"],
	"house-southeast": ["farnham"], "cabin-west": ["west-short"], "cabin-east": ["east-long"],
	"well": ["clean-water", "poisoned-water"], "cathedral": ["exterior-with-bell-tower"],
	"catacombs-entrance": ["closed-warp", "open-warp"],
}


func export_scene(scene_root: Node3D, output_dir: String) -> Dictionary:
	var result := {"ok": false, "errors": [], "warnings": [], "instances": 0, "inherited": 0}
	if not is_instance_valid(scene_root):
		_fail(result, "Cena de autoria ausente.")
		return result
	if scene_root.transform != Transform3D.IDENTITY:
		_fail(result, "O nó raiz de autoria deve manter transformação identidade. Edite as instâncias/meshes; a escala de prévia pertence somente à câmera auxiliar.")
		return result
	if str(scene_root.get_meta("d3d_edition", "retail")) != "retail":
		_fail(result, "O contrato de exportação v1 aceita apenas snapshot retail. Não converta outra edição silenciosamente.")
		return result
	var local_root := ProjectSettings.globalize_path("res://local").simplify_path()
	var target := ProjectSettings.globalize_path(output_dir).simplify_path().trim_suffix("/")
	if not _inside(target, local_root) or target == local_root:
		_fail(result, "Escolha uma subpasta de local/ para o pacote; o snapshot e a cena não podem ser sobrescritos.")
		return result
	var registry := _read_registry(result)
	if not result.errors.is_empty():
		return result
	var instances: Array[Node3D] = []
	_find_instances(scene_root, instances)
	var seen := {}
	var prepared: Array[Dictionary] = []
	for instance in instances:
		var metadata: Dictionary = instance.get_meta("d3d_instance", {})
		var id := str(metadata.get("instanceId", ""))
		if id.is_empty() or seen.has(id):
			_fail(result, "Identidade de instância vazia ou duplicada: %s." % id)
			continue
		seen[id] = true
		if instance.get_meta("d3d_override", false) != true:
			result.inherited += 1
			continue
		var errors_before: int = result.errors.size()
		_validate_identity(metadata, registry, result)
		var revision := str(instance.get_meta("d3d_revision", metadata.get("revision", "")))
		if not _safe_ini_value(revision) or revision.strip_edges().is_empty():
			_fail(result, "%s: informe uma revisão explícita, sem quebras de linha ou delimitadores INI." % id)
		if id == "cabin-east" or _has_fire(metadata) or instance.get_meta("d3d_has_fire", false) == true:
			_fail(result, "%s: exportação bloqueada na v1. Luzes/fogo dependem do adjunto nativo; mantenha esta instância herdada para conservar o comportamento real." % id)
		if result.errors.size() != errors_before:
			continue
		var mesh_data := {"triangles": [], "materials": [], "material_ids": {}, "label": id}
		_collect_geometry(instance, scene_root, mesh_data, result)
		if mesh_data.triangles.is_empty():
			_fail(result, "%s: não há triângulos visíveis para exportar." % id)
		if mesh_data.triangles.size() > MAX_TRIANGLES:
			_fail(result, "%s: %d triângulos excedem o limite de %d." % [id, mesh_data.triangles.size(), MAX_TRIANGLES])
		if result.errors.size() != errors_before:
			continue
		var atlas := _make_atlas(mesh_data.materials, result, id)
		if result.errors.size() != errors_before:
			continue
		var bytes := _encode(mesh_data.triangles, atlas, metadata.nativeMin, result, id)
		if result.errors.size() != errors_before:
			continue
		var item := metadata.duplicate(true)
		item["baselineSha256"] = str(metadata.get("sha256", ""))
		item["revision"] = revision
		item["model"] = "d3d-models/editor/%s.d3d" % id
		item["sha256"] = _sha256(bytes)
		item["bytes"] = bytes
		item["triangles"] = mesh_data.triangles.size()
		item["atlasSize"] = [atlas.width, atlas.height]
		item["sourcePath"] = str(instance.get_meta("d3d_source_path", ""))
		item["sourceSha256"] = str(instance.get_meta("d3d_source_sha256", instance.get_meta("d3d_source_hash", "")))
		if not item.sourcePath.is_empty():
			var source := FileAccess.get_file_as_bytes(ProjectSettings.globalize_path(item.sourcePath))
			if source.is_empty():
				_fail(result, "%s: arquivo fonte vinculado ausente ou vazio; reimporte o GLB antes de exportar." % id)
				continue
			var source_hash := _sha256(source)
			if not item.sourceSha256.is_empty() and item.sourceSha256 != source_hash:
				_fail(result, "%s: o arquivo fonte mudou após a importação. Reimporte-o para manter o vínculo da revisão." % id)
				continue
			item.sourceSha256 = source_hash
		prepared.append(item)
	if not result.errors.is_empty():
		return result
	if prepared.is_empty():
		_fail(result, "Nenhuma instância foi marcada explicitamente para substituição; a baseline permanece herdada.")
		return result
	_warn(result, "A validação técnica não é aprovação visual. Paleta indexada, iluminação, faces e amostragem no DevilutionX podem diferir da prévia Godot; compare no jogo na câmera original e em 360°.")
	# Nenhum arquivo de pacote é escrito antes de validar todas as instâncias.
	_commit_package(prepared, target, local_root, result)
	return result


func _find_instances(node: Node, output: Array[Node3D]) -> void:
	if node.get_meta("d3d_export_ignore", false) == true:
		return
	if node is Node3D and (node.has_meta("d3d_instance") or node.is_in_group("tristram_instance")):
		output.append(node)
	for child in node.get_children():
		_find_instances(child, output)


func _read_registry(result: Dictionary) -> Dictionary:
	var path := ProjectSettings.globalize_path(REGISTRY_PATH).simplify_path()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_fail(result, "Catálogo assets/registry.json indisponível; exportação cancelada para não inventar IDs.")
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if not parsed is Dictionary or parsed.get("format") != "d3d.asset-registry" or parsed.get("schemaVersion") != 1:
		_fail(result, "Catálogo de assets inválido ou de versão não suportada.")
		return {}
	var entries := {}
	for entry in parsed.get("entries", []):
		if entry is Dictionary:
			entries[str(entry.get("id", ""))] = entry
	return entries


func _validate_identity(metadata: Dictionary, registry: Dictionary, result: Dictionary) -> void:
	var id := str(metadata.get("instanceId", ""))
	if not NATIVE_INSTANCES.has(id):
		_fail(result, "Instância nativa desconhecida: %s." % id)
		return
	var expected: Array = NATIVE_INSTANCES[id]
	var asset := str(metadata.get("assetId", ""))
	var variant := str(metadata.get("variantId", ""))
	if asset != expected[0] or not _same_bounds(metadata.get("nativeMin"), expected[1]) or not _same_bounds(metadata.get("nativeMax"), expected[2]):
		_fail(result, "%s: asset ou limites nativos divergentes do contrato. A colisão não pode ser editada." % id)
	if not registry.has(asset):
		_fail(result, "%s: assetId não encontrado no catálogo: %s." % [id, asset])
		return
	var found := false
	for registered in registry[asset].get("variants", []):
		if registered is Dictionary and str(registered.get("id", "")) == variant:
			found = true
	if not found:
		_fail(result, "%s: variante '%s' não existe no registry; corrija o vínculo, sem criar catálogo paralelo." % [id, variant])
	elif not variant in INSTANCE_VARIANTS[id]:
		_fail(result, "%s: variante '%s' pertence a outro vínculo nativo; mantenha a variante original desta instância." % [id, variant])


func _same_bounds(actual: Variant, expected: Array) -> bool:
	if not actual is Array or actual.size() != 2:
		return false
	for axis in 2:
		if not (actual[axis] is int or actual[axis] is float) or not is_finite(float(actual[axis])) or float(actual[axis]) != float(expected[axis]):
			return false
	return true


func _has_fire(metadata: Dictionary) -> bool:
	for key in ["fireSources", "FireSources", "hasFireSources", "HasFireSources"]:
		var value: Variant = metadata.get(key, false)
		if value is Array and not value.is_empty():
			return true
		if value is bool and value:
			return true
		if (value is int or value is float) and value != 0:
			return true
	return false


func _collect_geometry(node: Node, scene_root: Node3D, data: Dictionary, result: Dictionary) -> void:
	if node.get_meta("d3d_export_ignore", false) == true:
		return
	if node is Node3D and not node.visible:
		return
	for reason in node.get_meta("d3d_glb_export_errors", []):
		_fail(result, "%s/%s: fonte GLB não exportável: %s" % [data.label, node.name, reason])
	# Animações são recusadas inclusive paradas: o pacote não tem como preservá-las.
	if node is AnimationPlayer and not node.get_animation_list().is_empty():
		_fail(result, "%s/%s: animação não suportada em D3DMESH1." % [data.label, node.name])
	if node is AnimationTree or node is Skeleton3D:
		_fail(result, "%s/%s: rig/animação não suportados em D3DMESH1." % [data.label, node.name])
	if node is Light3D or node is GPUParticles3D or node is CPUParticles3D:
		_fail(result, "%s/%s: luzes, fogo e partículas precisam de um contrato versionado; não podem ser descartados." % [data.label, node.name])
	if node is GeometryInstance3D and not node is MeshInstance3D:
		_fail(result, "%s/%s: geometria %s não suportada; converta explicitamente em MeshInstance3D estático." % [data.label, node.name, node.get_class()])
	if node is MeshInstance3D:
		_collect_mesh(node, scene_root, data, result)
	for child in node.get_children():
		_collect_geometry(child, scene_root, data, result)


func _collect_mesh(node: MeshInstance3D, scene_root: Node3D, data: Dictionary, result: Dictionary) -> void:
	var label := "%s/%s" % [data.label, node.name]
	var mesh := node.mesh
	if mesh == null:
		return
	var has_blend_shapes: bool = mesh is ArrayMesh and mesh.get_blend_shape_count() > 0
	if node.skin != null or not node.skeleton.is_empty() or has_blend_shapes:
		# Godot defaults skeleton to ..; only an actual resolved Skeleton is a rig.
		if node.skin != null or has_blend_shapes or node.get_node_or_null(node.skeleton) is Skeleton3D:
			_fail(result, "%s: skin, esqueleto e blend shapes não suportados." % label)
			return
	if node.material_overlay != null:
		_fail(result, "%s: material overlay não suportado." % label)
		return
	if node.transparency != 0.0:
		_fail(result, "%s: transparência de instância não suportada." % label)
		return
	var transform := _author_transform(node, scene_root)
	if not transform.origin.is_finite() or not transform.basis.x.is_finite() or not transform.basis.y.is_finite() or not transform.basis.z.is_finite():
		_fail(result, "%s: transformação não finita." % label)
		return
	var determinant := transform.basis.determinant()
	if not is_finite(determinant) or absf(determinant) <= 0.000000000001:
		_fail(result, "%s: escala singular ou extrema não permite transformar as normais com segurança." % label)
		return
	var normal_basis := transform.basis.inverse().transposed()
	for surface in mesh.get_surface_count():
		# Mesh's public primitive query exists on ArrayMesh only. Built-in solid
		# PrimitiveMesh resources produce triangles; PointMesh must be rejected.
		var is_triangles: bool = (mesh is ArrayMesh and mesh.surface_get_primitive_type(surface) == Mesh.PRIMITIVE_TRIANGLES) or (mesh is PrimitiveMesh and not mesh is PointMesh)
		if not is_triangles:
			_fail(result, "%s: superfície %d não usa triângulos." % [label, surface])
			continue
		if not mesh.surface_get_blend_shape_arrays(surface).is_empty():
			_fail(result, "%s: blend shapes não suportados." % label)
			continue
		var arrays := mesh.surface_get_arrays(surface)
		if arrays.is_empty() or not arrays[Mesh.ARRAY_VERTEX] is PackedVector3Array:
			_fail(result, "%s: superfície sem vértices válidos." % label)
			continue
		for index in [Mesh.ARRAY_BONES, Mesh.ARRAY_WEIGHTS]:
			if arrays[index] != null and arrays[index].size() > 0:
				_fail(result, "%s: pesos/ossos não suportados." % label)
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals := PackedVector3Array()
		if arrays[Mesh.ARRAY_NORMAL] is PackedVector3Array:
			normals = arrays[Mesh.ARRAY_NORMAL]
		if not normals.is_empty() and normals.size() != vertices.size():
			_fail(result, "%s: normais incompletas. Recalcule uma normal por vértice antes de exportar." % label)
			continue
		var has_normals := normals.size() == vertices.size()
		if not has_normals:
			_warn(result, "%s: malha sem normais explícitas; assumida a convenção Godot de faces horárias (CW), convertida para o cross CCW do jogo. Confira a iluminação no jogo." % label)
		var uvs := PackedVector2Array()
		if arrays[Mesh.ARRAY_TEX_UV] is PackedVector2Array:
			uvs = arrays[Mesh.ARRAY_TEX_UV]
		var material: Material = node.material_override
		if material == null:
			material = node.get_surface_override_material(surface)
		if material == null:
			material = mesh.surface_get_material(surface)
		var material_index := _material_index(material, data, result, label)
		if material_index < 0:
			continue
		if data.materials[material_index].textured and uvs.size() != vertices.size():
			_fail(result, "%s: textura albedo requer UV1 por vértice." % label)
			continue
		var indices := PackedInt32Array()
		if arrays[Mesh.ARRAY_INDEX] is PackedInt32Array:
			indices = arrays[Mesh.ARRAY_INDEX]
		else:
			for vertex in vertices.size():
				indices.append(vertex)
		if indices.size() % 3 != 0:
			_fail(result, "%s: lista de índices incompleta." % label)
			continue
		for face in range(0, indices.size(), 3):
			var triangle: Array = []
			var authored_normal := Vector3.ZERO
			for corner in 3:
				var index: int = indices[face + corner]
				if index < 0 or index >= vertices.size():
					_fail(result, "%s: índice de vértice fora da malha." % label)
					break
				var position: Vector3 = transform * vertices[index]
				var uv := Vector2.ZERO
				# UVs físicos da baseline não têm efeito num material de cor constante.
				if data.materials[material_index].textured and uvs.size() == vertices.size():
					uv = uvs[index]
				if not position.is_finite() or not uv.is_finite() or uv.x < 0 or uv.x > 1 or uv.y < 0 or uv.y > 1:
					_fail(result, "%s: coordenada inválida ou UV fora de 0..1. Repetição/triplanar precisa ser convertida explicitamente." % label)
					break
				if has_normals:
					# Inverse transpose preserves authored normals through rotation,
					# nonuniform scale and reflection; transformed cross alone does not.
					var normal: Vector3 = normal_basis * normals[index]
					if not normal.is_finite() or normal.length_squared() <= 0.000000000001:
						_fail(result, "%s: normal inválida ou nula. Recalcule as normais antes de exportar." % label)
						break
					authored_normal += normal.normalized()
				triangle.append([position, uv])
			if triangle.size() == 3:
				var edge_a: Vector3 = triangle[1][0] - triangle[0][0]
				var edge_b: Vector3 = triangle[2][0] - triangle[0][0]
				var cross_world := edge_a.cross(edge_b)
				if not cross_world.is_finite() or cross_world.length() <= 0.000001:
					_fail(result, "%s: triângulo degenerado após transformação." % label)
					continue
				# Godot/GLTF normally use CW; native snapshot meshes already carry
				# outward CCW normals. Let their actual normals choose the winding.
				var swap_winding := determinant > 0.0
				if has_normals:
					var alignment := cross_world.normalized().dot(authored_normal.normalized())
					if authored_normal.length_squared() <= 0.000000000001 or absf(alignment) <= 0.000001:
						_fail(result, "%s: orientação ambígua entre face e normais. Recalcule as normais antes de exportar." % label)
						continue
					swap_winding = alignment < 0.0
				if swap_winding:
					var second_vertex: Array = triangle[1]
					triangle[1] = triangle[2]
					triangle[2] = second_vertex
				data.triangles.append({"vertices": triangle, "material": material_index})
			if data.triangles.size() > MAX_TRIANGLES:
				return


func _author_transform(node: Node3D, root: Node3D) -> Transform3D:
	var transform := Transform3D.IDENTITY
	var current: Node = node
	while current != root and current != null:
		if current is Node3D:
			transform = current.transform * transform
			if current.top_level:
				break
		current = current.get_parent()
	return transform


func _material_index(material: Material, data: Dictionary, result: Dictionary, label: String) -> int:
	var key := material.get_instance_id() if material != null else 0
	if data.material_ids.has(key):
		return data.material_ids[key]
	var image := Image.create(1, 1, false, Image.FORMAT_RGB8)
	var textured := false
	var color := Color.WHITE
	if material != null:
		if not material is StandardMaterial3D:
			_fail(result, "%s: use StandardMaterial3D opaco, com cor base e textura albedo; ShaderMaterial e materiais personalizados não são exportáveis." % label)
			return -1
		if material.next_pass != null or material.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED or material.albedo_color.a != 1.0:
			_fail(result, "%s: transparência ou múltiplos passes não suportados." % label)
			return -1
		for feature in BaseMaterial3D.FEATURE_MAX:
			if material.get_feature(feature):
				_fail(result, "%s: recurso avançado de material (%d) não suportado." % [label, feature])
				return -1
		for slot in BaseMaterial3D.TEXTURE_MAX:
			if slot != BaseMaterial3D.TEXTURE_ALBEDO and material.get_texture(slot) != null:
				_fail(result, "%s: somente textura albedo é suportada (slot %d)." % [label, slot])
				return -1
		var unsupported := {
			"shading_mode": BaseMaterial3D.SHADING_MODE_PER_PIXEL,
			"vertex_color_use_as_albedo": false, "uv1_triplanar": false, "uv2_triplanar": false,
			"uv1_world_triplanar": false, "uv2_world_triplanar": false,
			"uv1_scale": Vector3.ONE, "uv1_offset": Vector3.ZERO,
			"grow": false, "proximity_fade_enabled": false, "distance_fade_mode": 0,
			"billboard_mode": 0, "fixed_size": false, "no_depth_test": false,
			"use_point_size": false, "albedo_texture_force_srgb": false,
			"metallic": 0.0, "roughness": 1.0, "metallic_specular": 0.5,
		}
		var properties := {}
		for property in material.get_property_list():
			properties[property.name] = true
		for property in unsupported:
			if properties.has(property) and material.get(property) != unsupported[property]:
				_fail(result, "%s: propriedade '%s' não suportada; exportação cancelada para preservar o material." % [label, property])
				return -1
		color = material.albedo_color
		if not is_finite(color.r) or not is_finite(color.g) or not is_finite(color.b) or color.r < 0 or color.r > 1 or color.g < 0 or color.g > 1 or color.b < 0 or color.b > 1:
			_fail(result, "%s: cor base fora do intervalo RGB 0..1." % label)
			return -1
		if material.albedo_texture != null:
			textured = true
			image = material.albedo_texture.get_image()
			if image == null or image.is_empty():
				_fail(result, "%s: pixels da textura albedo indisponíveis." % label)
				return -1
			image = image.duplicate()
			if image.is_compressed() and image.decompress() != OK:
				_fail(result, "%s: textura comprimida não pôde ser decodificada." % label)
				return -1
			if image.detect_alpha() != Image.ALPHA_NONE:
				_fail(result, "%s: textura possui transparência; D3DMESH1 aceita apenas RGB opaco." % label)
				return -1
			if image.get_width() > MAX_TEXTURE_SIDE or image.get_height() > MAX_TEXTURE_SIDE:
				_fail(result, "%s: textura excede 2048 pixels; reduza-a explicitamente antes de exportar." % label)
				return -1
			# Albedo é combinado em espaço linear e armazenado em RGB sRGB.
			var linear_color := color.srgb_to_linear()
			for y in image.get_height():
				for x in image.get_width():
					var pixel := image.get_pixel(x, y).srgb_to_linear()
					image.set_pixel(x, y, (pixel * linear_color).linear_to_srgb())
			image.convert(Image.FORMAT_RGB8)
		else:
			image.fill(color)
	else:
		image.fill(color)
	var index: int = data.materials.size()
	data.materials.append({"image": image, "textured": textured})
	data.material_ids[key] = index
	return index


func _make_atlas(materials: Array, result: Dictionary, label: String) -> Dictionary:
	var order: Array[int] = []
	var widths: Array[int] = []
	var heights: Array[int] = []
	for index in materials.size():
		order.append(index)
		var image: Image = materials[index].image
		var padding := 1 if maxi(image.get_width(), image.get_height()) <= MAX_TEXTURE_SIDE - 2 else 0
		materials[index]["padding"] = padding
		widths.append(image.get_width() + padding * 2)
		heights.append(image.get_height() + padding * 2)
	order.sort_custom(func(a: int, b: int) -> bool: return heights[a] > heights[b])
	var best: Dictionary = {}
	var atlas_width := 1
	while atlas_width <= MAX_TEXTURE_SIDE:
		var placements := {}
		var x := 0
		var y := 0
		var row_height := 0
		var fits := true
		for index in order:
			if widths[index] > atlas_width:
				fits = false
				break
			if x + widths[index] > atlas_width:
				y += row_height
				x = 0
				row_height = 0
			placements[index] = Vector2i(x, y)
			x += widths[index]
			row_height = maxi(row_height, heights[index])
		var atlas_height := y + row_height
		if fits and atlas_height <= MAX_TEXTURE_SIDE and (best.is_empty() or atlas_width * atlas_height < best.width * best.height):
			best = {"width": atlas_width, "height": atlas_height, "placements": placements}
		atlas_width *= 2
	if best.is_empty():
		_fail(result, "%s: materiais não cabem num atlas RGB 2048×2048 sem reduzir texturas. Faça essa redução explicitamente." % label)
		return {}
	var atlas := Image.create(best.width, best.height, false, Image.FORMAT_RGB8)
	atlas.fill(Color.BLACK)
	var uv_rects: Array[Rect2] = []
	for index in materials.size():
		var source: Image = materials[index].image
		var padding: int = materials[index].padding
		var position: Vector2i = best.placements[index] + Vector2i(padding, padding)
		atlas.blit_rect(source, Rect2i(Vector2i.ZERO, source.get_size()), position)
		# Borda repetida evita vazamento entre materiais e conserva floor(u * width).
		for y in range(-padding, source.get_height() + padding):
			for x in range(-padding, source.get_width() + padding):
				if x < 0 or y < 0 or x >= source.get_width() or y >= source.get_height():
					atlas.set_pixel(position.x + x, position.y + y, source.get_pixel(clampi(x, 0, source.get_width() - 1), clampi(y, 0, source.get_height() - 1)))
		uv_rects.append(Rect2(Vector2(position), Vector2(source.get_width(), source.get_height())))
	best["image"] = atlas
	best["uv_rects"] = uv_rects
	return best


func _encode(triangles: Array, atlas: Dictionary, native_min: Array, result: Dictionary, label: String) -> PackedByteArray:
	var buffer := StreamPeerBuffer.new()
	buffer.big_endian = false
	buffer.put_data("D3DMESH1".to_ascii_buffer())
	buffer.put_u32(triangles.size())
	buffer.put_u32(atlas.width)
	buffer.put_u32(atlas.height)
	for triangle in triangles:
		var points: Array[Vector3] = []
		for vertex in triangle.vertices:
			var position: Vector3 = vertex[0]
			position.x -= float(native_min[0])
			position.z -= float(native_min[1])
			# Quantize before validation: these exact float32 values are decoded by C++.
			var quantized := StreamPeerBuffer.new()
			for scalar in [position.x, position.y, position.z]:
				quantized.put_float(scalar)
			quantized.seek(0)
			position = Vector3(quantized.get_float(), quantized.get_float(), quantized.get_float())
			if not position.is_finite() or position.x < -64 or position.x > 128 or position.z < -64 or position.z > 128 or position.y < 0 or position.y > 64:
				_fail(result, "%s: vértice fora dos limites D3DMESH1 (x/z -64..128; altura 0..64)." % label)
				return PackedByteArray()
			points.append(position)
		if (points[1] - points[0]).cross(points[2] - points[0]).length() <= 0.000001:
			_fail(result, "%s: triângulo degenerado após transformação/float32." % label)
			return PackedByteArray()
		for index in 3:
			var position := points[index]
			var rect: Rect2 = atlas.uv_rects[triangle.material]
			var source_uv: Vector2 = triangle.vertices[index][1]
			var pixel := rect.position + source_uv * rect.size
			if source_uv.x == 1.0:
				pixel.x = rect.end.x - 0.5
			if source_uv.y == 1.0:
				pixel.y = rect.end.y - 0.5
			var uv := pixel / Vector2(atlas.width, atlas.height)
			for scalar in [position.x, position.y, position.z, uv.x, uv.y]:
				buffer.put_float(scalar)
	buffer.put_data(atlas.image.get_data())
	return buffer.data_array


func _commit_package(items: Array[Dictionary], target: String, local_root: String, result: Dictionary) -> void:
	var token := "%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]
	var staging := target + ".building-" + token
	var backup := target + ".previous-" + token
	if DirAccess.dir_exists_absolute(target):
		var existing := FileAccess.open(target.path_join("receipt.json"), FileAccess.READ)
		var receipt: Variant = JSON.parse_string(existing.get_as_text()) if existing != null else null
		if not receipt is Dictionary or receipt.get("format") != "d3d.godot-export-receipt":
			_fail(result, "A pasta de destino já existe e não é um pacote reconhecido do editor; escolha outra subpasta para preservar seus arquivos.")
			return
	if DirAccess.make_dir_recursive_absolute(staging.path_join("d3d-models/editor")) != OK or DirAccess.make_dir_recursive_absolute(staging.path_join("d3d-maps")) != OK:
		_fail(result, "Não foi possível preparar a pasta temporária de exportação.")
		_remove_staging(staging, local_root)
		return
	var ini := "[Scene]\nformat=d3d.town-map\nschemaVersion=1\nedition=retail\n"
	for item in items:
		ini += "instance=%s\n" % item.instanceId
	var receipt_items: Array = []
	for item in items:
		if not _write_bytes(staging.path_join(item.model), item.bytes):
			_fail(result, "%s: falha ao gravar o modelo temporário." % item.instanceId)
			break
		ini += "\n[%s]\nassetId=%s\nvariantId=%s\nnativeMin=%d,%d\nnativeMax=%d,%d\nmodel=%s\nsha256=%s\nrevision=%s\ninterior=none\n" % [item.instanceId, item.assetId, item.variantId, item.nativeMin[0], item.nativeMin[1], item.nativeMax[0], item.nativeMax[1], item.model, item.sha256, item.revision]
		var receipt_item := {
			"instanceId": item.instanceId, "assetId": item.assetId, "variantId": item.variantId,
			"nativeMin": item.nativeMin, "nativeMax": item.nativeMax,
			"model": item.model, "sha256": item.sha256, "revision": item.revision,
			"triangles": item.triangles, "atlasSize": item.atlasSize,
			"sourceSha256": item.sourceSha256,
			"baselineSha256": item.baselineSha256,
			"validation": "technical-only", "visualApproval": false,
		}
		receipt_items.append(receipt_item)
	var ini_bytes := ini.to_utf8_buffer()
	var receipt := {"format": "d3d.godot-export-receipt", "schemaVersion": 1, "createdAtUtc": Time.get_datetime_string_from_system(true) + "Z", "edition": "retail", "manifestSha256": _sha256(ini_bytes), "instances": receipt_items, "warnings": result.warnings}
	if result.errors.is_empty() and (not _write_bytes(staging.path_join("d3d-maps/tristram.ini"), ini_bytes) or not _write_bytes(staging.path_join("receipt.json"), (JSON.stringify(receipt, "\t") + "\n").to_utf8_buffer())):
		_fail(result, "Falha ao gravar manifesto ou recibo temporário.")
	if not result.errors.is_empty():
		_remove_staging(staging, local_root)
		return
	# Leitura independente dos bytes gravados antes de publicar o diretório inteiro.
	for item in items:
		if _sha256(FileAccess.get_file_as_bytes(staging.path_join(item.model))) != item.sha256:
			_fail(result, "%s: hash dos bytes gravados divergiu; pacote anterior preservado." % item.instanceId)
	if not result.errors.is_empty():
		_remove_staging(staging, local_root)
		return
	var had_previous := DirAccess.dir_exists_absolute(target)
	if had_previous and DirAccess.rename_absolute(target, backup) != OK:
		_fail(result, "O pacote anterior está em uso ou não pôde ser preservado; feche o consumidor e tente novamente.")
		_remove_staging(staging, local_root)
		return
	if DirAccess.rename_absolute(staging, target) != OK:
		if had_previous:
			DirAccess.rename_absolute(backup, target)
		_fail(result, "Não foi possível finalizar a exportação; o pacote anterior foi preservado.")
		_remove_staging(staging, local_root)
		return
	result.ok = true
	result.instances = items.size()
	result.output_dir = target
	result.manifest = target.path_join("d3d-maps/tristram.ini")
	result.receipt = target.path_join("receipt.json")
	if had_previous:
		result.backup_dir = backup


func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	return file.get_error() == OK


func _remove_staging(path: String, local_root: String) -> void:
	if not _inside(path, local_root) or not path.get_file().contains(".building-"):
		return
	_remove_tree(path)


func _remove_tree(path: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	for name in directory.get_files():
		DirAccess.remove_absolute(path.path_join(name))
	for name in directory.get_directories():
		_remove_tree(path.path_join(name))
	DirAccess.remove_absolute(path)


func _inside(path: String, root: String) -> bool:
	return path.to_lower().begins_with(root.to_lower().trim_suffix("/") + "/")


func _safe_ini_value(value: String) -> bool:
	var bytes := value.to_utf8_buffer()
	if bytes.size() > 128 or value != value.strip_edges():
		return false
	for byte in bytes:
		if byte < 32 or byte == 127:
			return false
	for forbidden in ["\n", "\r", "[", "]", "=", ";", "#"]:
		if value.contains(forbidden):
			return false
	return true


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()


func _fail(result: Dictionary, message: String) -> void:
	if not result.errors.has(message):
		result.errors.append(message)


func _warn(result: Dictionary, message: String) -> void:
	if not result.warnings.has(message):
		result.warnings.append(message)
