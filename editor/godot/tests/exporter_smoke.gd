extends SceneTree
## Fixtures sintéticas. Resultados e pacotes ficam apenas em local/ (ignorado).

const Exporter = preload("res://addons/tristram_editor/exporter.gd")
var failures: Array[String] = []
var checks := 0
var destination := ""
var exporter := Exporter.new()


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	destination = "res://local/exporter-tests/%d-%d/package" % [OS.get_process_id(), Time.get_ticks_usec()]
	var scene := _fixture()
	var instance: Node3D = scene.get_child(0)
	instance.set_meta("d3d_override", false)
	var report := exporter.export_scene(scene, destination)
	_check(not report.ok and report.instances == 0, "Baseline herdada não é exportada automaticamente")
	_check(not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(destination)), "Sem override não cria pacote")
	instance.set_meta("d3d_override", true)
	# A escala de visualização pertence a um clone e nunca altera autoria/export.
	scene.scale = Vector3(1, 0.816496580927726, 1)
	_check(not exporter.export_scene(scene, destination).ok, "Root de autoria transformado é rejeitado para preservar a edição")
	scene.scale = Vector3.ONE
	report = exporter.export_scene(scene, destination)
	_check(report.ok, "Exportação opaca válida: %s" % [report.errors])
	if report.ok:
		_validate_package(report)
		var before := FileAccess.get_file_as_bytes(report.manifest)
		instance.set_meta("d3d_revision", "fixture\ninjection=bad")
		var rejected := exporter.export_scene(scene, destination)
		_check(not rejected.ok, "Revisão com injeção INI rejeitada")
		_check(before == FileAccess.get_file_as_bytes(report.manifest), "Falha preserva pacote anterior byte a byte")
		instance.set_meta("d3d_revision", "synthetic-revision-2")
		var second := exporter.export_scene(scene, destination)
		_check(second.ok and second.has("backup_dir"), "Atualização mantém pacote anterior em backup")
		if second.ok:
			_check(DirAccess.dir_exists_absolute(second.backup_dir), "Backup existe após substituição atômica")
	scene.free()
	_reject_cases()
	_atlas_case()
	_transaction_case()
	_limits_and_source_cases()
	_multiple_instances_case()
	_primitive_case()
	_orientation_cases()
	print("EXPORTER_SMOKE checks=%d failures=%d" % [checks, failures.size()])
	for failure in failures:
		push_error(failure)
	quit(0 if failures.is_empty() else 1)


func _fixture() -> Node3D:
	var scene := Node3D.new()
	scene.name = "SyntheticFixture"
	var instance := Node3D.new()
	instance.name = "cabin-west"
	instance.position = Vector3(26, 0, 48)
	instance.set_meta("d3d_instance", {"instanceId": "cabin-west", "assetId": "tristram.arch.cabin", "variantId": "west-short", "nativeMin": [26, 48], "nativeMax": [30, 52], "revision": "synthetic-baseline", "sha256": "baseline-fixture"})
	instance.set_meta("d3d_revision", "synthetic-revision-1")
	instance.set_meta("d3d_override", true)
	instance.add_to_group("tristram_instance")
	scene.add_child(instance)
	var geometry := MeshInstance3D.new()
	geometry.name = "SyntheticTriangle"
	geometry.mesh = _triangle_mesh()
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(1, 0, 0, 1)
	geometry.material_override = material
	instance.add_child(geometry)
	return scene


func _triangle_mesh(vertices := PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(0, 1, 0)]), uvs := PackedVector2Array([Vector2.ZERO, Vector2(1, 0), Vector2(0, 1)]), with_normals := true) -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	if with_normals:
		# This fixture models a native snapshot: outward normals with CCW order.
		var normal: Vector3 = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalized()
		arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([normal, normal, normal])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _validate_package(report: Dictionary) -> void:
	var folder: String = report.output_dir
	var bytes := FileAccess.get_file_as_bytes(folder.path_join("d3d-models/editor/cabin-west.d3d"))
	_check(bytes.slice(0, 8).get_string_from_ascii() == "D3DMESH1", "Magic e formato D3DMESH1")
	_check(bytes.decode_u32(8) == 1, "Contagem binária de triângulos")
	var width := bytes.decode_u32(12)
	var height := bytes.decode_u32(16)
	_check(bytes.size() == 20 + 60 + width * height * 3, "Tamanho exato sem padding de structs")
	_check(bytes.decode_float(20) == 0 and bytes.decode_float(28) == 0, "Vértice x/z relativo a nativeMin")
	_check(bytes.decode_float(64) == 1, "Altura bruta preservada sem escala de prévia")
	for vertex in 3:
		var offset := 20 + vertex * 20
		_check(bytes.decode_float(offset + 12) >= 0 and bytes.decode_float(offset + 12) <= 1 and bytes.decode_float(offset + 16) >= 0 and bytes.decode_float(offset + 16) <= 1, "UV atlas normalizada %d" % vertex)
	var receipt: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(report.receipt))
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(bytes)
	_check(receipt.instances[0].sha256 == hash.finish().hex_encode(), "SHA-256 corresponde aos bytes reais")
	_check(receipt.instances[0].baselineSha256 == "baseline-fixture", "Recibo mantém hash da baseline separado")
	_check(receipt.instances[0].validation == "technical-only" and not receipt.instances[0].visualApproval, "Recibo separa técnico de aprovação visual")
	var ini := FileAccess.get_file_as_string(report.manifest)
	_check(ini.contains("instance=cabin-west\n") and ini.contains("interior=none\n"), "INI contém vínculo nativo e interior completo")
	_check(not report.warnings.is_empty(), "Aviso de paleta/iluminação é visível")


func _reject_cases() -> void:
	var cases := ["fire-cabin", "fire-metadata", "bad-variant", "wrong-instance-variant", "bad-bounds", "unknown-id", "duplicate-id", "transparent-material", "transparent-instance", "transparent-texture", "shader", "unshaded", "normal-map", "metallic", "vertex-color", "uv-repeat", "height", "nonfinite", "degenerate", "animation", "skeleton", "skin", "blendshape", "overlay", "light", "glb-loss", "missing-source"]
	for test_case in cases:
		var scene := _fixture()
		var instance: Node3D = scene.get_child(0)
		var mesh: MeshInstance3D = instance.get_child(0)
		var material: StandardMaterial3D = mesh.material_override
		match test_case:
			"fire-cabin":
				var meta: Dictionary = instance.get_meta("d3d_instance").duplicate(true)
				meta.instanceId = "cabin-east"
				meta.variantId = "east-long"
				meta.nativeMin = [70, 66]
				meta.nativeMax = [74, 72]
				instance.set_meta("d3d_instance", meta)
				instance.position = Vector3(70, 0, 66)
			"fire-metadata":
				instance.get_meta("d3d_instance")["HasFireSources"] = true
			"bad-variant":
				instance.get_meta("d3d_instance")["variantId"] = "invented"
			"wrong-instance-variant":
				instance.get_meta("d3d_instance")["variantId"] = "east-long"
			"bad-bounds":
				instance.get_meta("d3d_instance")["nativeMin"] = [27, 48]
			"unknown-id":
				instance.get_meta("d3d_instance")["instanceId"] = "invented-house"
			"duplicate-id":
				var duplicate := instance.duplicate()
				scene.add_child(duplicate)
			"transparent-material":
				material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			"transparent-instance":
				mesh.transparency = 0.5
			"transparent-texture":
				var image := Image.create(2, 2, false, Image.FORMAT_RGBA8)
				image.fill(Color(1, 1, 1, 0.5))
				material.albedo_texture = ImageTexture.create_from_image(image)
			"shader":
				mesh.material_override = ShaderMaterial.new()
			"unshaded":
				material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			"normal-map":
				material.normal_enabled = true
			"metallic":
				material.metallic = 0.8
			"vertex-color":
				material.vertex_color_use_as_albedo = true
			"uv-repeat":
				var image := Image.create(1, 1, false, Image.FORMAT_RGB8)
				image.fill(Color.WHITE)
				material.albedo_texture = ImageTexture.create_from_image(image)
				mesh.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(0, 1, 0)]), PackedVector2Array([Vector2.ZERO, Vector2(2, 0), Vector2(0, 1)]))
			"height":
				instance.position.y = -1
			"nonfinite":
				instance.position.y = INF
			"degenerate":
				mesh.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]))
			"animation":
				var player := AnimationPlayer.new()
				var library := AnimationLibrary.new()
				library.add_animation("idle", Animation.new())
				player.add_animation_library("", library)
				instance.add_child(player)
			"skeleton":
				instance.add_child(Skeleton3D.new())
			"skin":
				mesh.skin = Skin.new()
			"blendshape":
				var shape_mesh := ArrayMesh.new()
				shape_mesh.add_blend_shape("morph")
				var base := mesh.mesh.surface_get_arrays(0)
				var shape := base.duplicate(true)
				shape[Mesh.ARRAY_TEX_UV] = null
				shape_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, base, [shape])
				mesh.mesh = shape_mesh
			"overlay":
				mesh.material_overlay = StandardMaterial3D.new()
			"light":
				instance.add_child(OmniLight3D.new())
			"glb-loss":
				mesh.set_meta("d3d_glb_export_errors", ["KHR_materials_transmission"])
			"missing-source":
				instance.set_meta("d3d_source_path", "res://local/nonexistent.glb")
		var report := exporter.export_scene(scene, destination + "-" + test_case)
		_check(not report.ok and not report.errors.is_empty(), "Rejeição explícita: %s" % test_case)
		_check(not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(destination + "-" + test_case)), "Falha não deixa pacote parcial: %s" % test_case)
		scene.free()
	var outside_scene := _fixture()
	var outside := exporter.export_scene(outside_scene, "res://addons/unsafe-output")
	_check(not outside.ok, "Destino fora de local/ rejeitado")
	outside_scene.set_meta("d3d_edition", "shareware")
	_check(not exporter.export_scene(outside_scene, destination + "-edition").ok, "Edição fora do contrato v1 é rejeitada")
	outside_scene.set_meta("d3d_edition", "retail")
	outside_scene.get_child(0).set_meta("d3d_revision", "á".repeat(65))
	_check(not exporter.export_scene(outside_scene, destination + "-long-revision").ok, "Limite revisão128bytes coincide com loader C++")
	outside_scene.get_child(0).set_meta("d3d_revision", "revision\tcontrol")
	_check(not exporter.export_scene(outside_scene, destination + "-control-revision").ok, "Controles em revisão coincidem com loader C++")
	outside_scene.free()


func _atlas_case() -> void:
	var scene := _fixture()
	var instance: Node3D = scene.get_child(0)
	var textured := MeshInstance3D.new()
	textured.name = "BlueTexturedTriangle"
	textured.position.x = 2
	textured.mesh = _triangle_mesh()
	var image := Image.create(4, 2, false, Image.FORMAT_RGB8)
	var texels := [Color.RED, Color.GREEN, Color.BLUE, Color.YELLOW, Color.CYAN, Color.MAGENTA, Color(1, 0.5, 0), Color.BLACK]
	for pixel in texels.size():
		image.set_pixel(pixel % 4, pixel / 4, texels[pixel])
	textured.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(0, 1, 0)]), PackedVector2Array([Vector2(0.2, 0.2), Vector2(0.4, 0.5), Vector2(0.9, 1.0)]))
	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	textured.material_override = material
	instance.add_child(textured)
	# Uma referência animada ignorada não contamina o pacote do substituto.
	var reference := Skeleton3D.new()
	reference.set_meta("d3d_export_ignore", true)
	instance.add_child(reference)
	var report := exporter.export_scene(scene, destination + "-atlas")
	_check(report.ok, "Atlas com dois materiais e referência ignorada: %s" % [report.errors])
	if report.ok:
		var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
		_check(bytes.decode_u32(8) == 2, "Duas malhas combinadas num modelo")
		var width := bytes.decode_u32(12)
		var height := bytes.decode_u32(16)
		var colors := Image.create_from_data(width, height, false, Image.FORMAT_RGB8, bytes.slice(20 + 120))
		var red_found := false
		var blue_found := false
		for y in height:
			for x in width:
				red_found = red_found or colors.get_pixel(x, y).r > 0.99
				blue_found = blue_found or colors.get_pixel(x, y).b > 0.99
		_check(red_found and blue_found, "Atlas mantém as duas cores sem reduzir texturas")
		var expected := [Color.RED, Color.MAGENTA, Color.BLACK]
		for vertex in 3:
			var offset := 80 + vertex * 20
			var x := clampi(int(bytes.decode_float(offset + 12) * width), 0, width - 1)
			var y := clampi(int(bytes.decode_float(offset + 16) * height), 0, height - 1)
			_check(colors.get_pixel(x, y).is_equal_approx(expected[vertex]), "Nearest original e linhas da textura preservados %d" % vertex)
	scene.free()


func _limits_and_source_cases() -> void:
	var scene := _fixture()
	var instance: Node3D = scene.get_child(0)
	var geometry: MeshInstance3D = instance.get_child(0)
	var indices := PackedInt32Array()
	indices.resize(20001 * 3)
	for index in indices.size():
		indices[index] = index % 3
	var arrays := geometry.mesh.surface_get_arrays(0)
	arrays[Mesh.ARRAY_INDEX] = indices
	var excessive := ArrayMesh.new()
	excessive.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	geometry.mesh = excessive
	_check(not exporter.export_scene(scene, destination + "-too-many-triangles").ok, "Limite de 20.000 triângulos imposto")
	geometry.mesh = _triangle_mesh()
	var image := Image.create(2049, 1, false, Image.FORMAT_RGB8)
	image.fill(Color.WHITE)
	geometry.material_override.albedo_texture = ImageTexture.create_from_image(image)
	_check(not exporter.export_scene(scene, destination + "-too-large-texture").ok, "Textura com lado maior que 2048 rejeitada")
	geometry.material_override.albedo_texture = null
	var atlas_errors := {"errors": [], "warnings": []}
	var large := Image.create(2048, 1025, false, Image.FORMAT_RGB8)
	var atlas: Dictionary = exporter._make_atlas([{"image": large}, {"image": large}], atlas_errors, "synthetic-overflow")
	_check(atlas.is_empty() and not atlas_errors.errors.is_empty(), "Atlas excessivo rejeitado sem redução silenciosa")
	instance.position.y = 64
	_check(not exporter.export_scene(scene, destination + "-too-high").ok, "Altura maior que64 rejeitada")
	instance.position.y = 0
	instance.position.x -= 65
	_check(not exporter.export_scene(scene, destination + "-too-far").ok, "x relativo menor que-64 rejeitado")
	instance.position.x += 65
	# UVs da malha nativa sem textura não mudam sua cor constante.
	geometry.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3(1, 0, 0), Vector3(0, 1, 0)]), PackedVector2Array([Vector2(22, -8), Vector2(23, -8), Vector2(22, -7)]))
	_check(exporter.export_scene(scene, destination + "-constant-uv").ok, "UV físico de material constante é convertido sem perda visual")
	geometry.mesh = _triangle_mesh()
	var source := ProjectSettings.globalize_path(destination).get_base_dir().path_join("synthetic-source.glb")
	var file := FileAccess.open(source, FileAccess.WRITE)
	file.store_buffer("synthetic fixture only".to_utf8_buffer())
	file.close()
	instance.set_meta("d3d_source_path", source)
	instance.set_meta("d3d_source_sha256", FileAccess.get_sha256(source))
	_check(exporter.export_scene(scene, destination + "-source-linked").ok, "Fonte vinculada tem hash real verificado")
	file = FileAccess.open(source, FileAccess.WRITE)
	file.store_buffer("synthetic changed bytes".to_utf8_buffer())
	file.close()
	_check(not exporter.export_scene(scene, destination + "-source-changed").ok, "Fonte alterada após importação é rejeitada")
	scene.free()


func _multiple_instances_case() -> void:
	var scene := _fixture()
	var second := _fixture()
	var instance: Node3D = second.get_child(0)
	second.remove_child(instance)
	second.free()
	instance.set_meta("d3d_instance", {"instanceId": "house-gillian", "assetId": "tristram.arch.house-common", "variantId": "gillian", "nativeMin": [36, 64], "nativeMax": [42, 68]})
	instance.position = Vector3(36, 0, 64)
	scene.add_child(instance)
	var report := exporter.export_scene(scene, destination + "-multiple")
	_check(report.ok and report.instances == 2, "Duas instâncias exportadas sem converter baseline alheia")
	if report.ok:
		var ini := FileAccess.get_file_as_string(report.manifest)
		_check(ini.count("instance=") == 2, "Chave INI instance repetida para cada override")
	scene.free()


func _primitive_case() -> void:
	var scene := _fixture()
	var instance: Node3D = scene.get_child(0)
	var geometry: MeshInstance3D = instance.get_child(0)
	geometry.mesh = BoxMesh.new()
	geometry.position = Vector3(0.5, 0.5, 0.5)
	var report := exporter.export_scene(scene, destination + "-box")
	_check(report.ok, "BoxMesh estática suportada: %s" % [report.errors])
	if report.ok:
		var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
		_check(bytes.decode_u32(8) == 12, "BoxMesh mantém12 triângulos")
	geometry.mesh = _triangle_mesh()
	geometry.top_level = true
	geometry.position = Vector3(26, 0, 48)
	report = exporter.export_scene(scene, destination + "-top-level")
	_check(report.ok, "Transformação top_level é incorporada: %s" % [report.errors])
	if report.ok:
		var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
		_check(bytes.decode_float(20) == 0 and bytes.decode_float(28) == 0, "top_level não recebe deslocamento do pai duas vezes")
	scene.free()


func _orientation_cases() -> void:
	# Built-in Godot boxes use clockwise vertex order and explicit outward normals.
	# C++ reconstructs each normal with CCW cross(v1-v0, v2-v0).
	var scales := [Vector3.ONE, Vector3(2, 3, 0.5), Vector3(-2, 3, 0.5), Vector3(2, -3, 0.5), Vector3(-2, -3, -0.5)]
	for index in scales.size():
		var scene := _fixture()
		var geometry: MeshInstance3D = scene.get_child(0).get_child(0)
		geometry.mesh = BoxMesh.new()
		geometry.position = Vector3(3, 4, 3)
		geometry.basis = Basis.from_euler(Vector3(0.2, -0.3, 0.4)).scaled(scales[index])
		var report := exporter.export_scene(scene, destination + "-orientation-box-%d" % index)
		_check(report.ok, "Box rotacionada/escala %s exportável: %s" % [scales[index], report.errors])
		if report.ok:
			var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
			for face in 12:
				var vertices := _read_triangle(bytes, face)
				var normal := (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0])
				var center := (vertices[0] + vertices[1] + vertices[2]) / 3.0
				_check(normal.dot(center - geometry.position) > 0.000001, "Face Box%d/%d aponta para fora no cross C++" % [index, face])
		scene.free()
	# Native snapshot-style CCW normals keep their original order; never flip all
	# triangles solely because Godot's default convention is clockwise.
	var snapshot := _fixture()
	var report := exporter.export_scene(snapshot, destination + "-orientation-snapshot")
	_check(report.ok, "Snapshot CCW com normais explícitas exportável")
	if report.ok:
		var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
		var vertices := _read_triangle(bytes, 0)
		_check(vertices[0] == Vector3.ZERO and vertices[1] == Vector3.RIGHT and vertices[2] == Vector3.UP, "Snapshot CCW mantém mesma ordem dos3vértices")
	snapshot.free()
	# An oblique face checks inverse-transpose normals under nonuniform scale and
	# reflection, rather than merely checking axis-aligned normals of a box.
	for scale in [Vector3(2, 3, 0.5), Vector3(-2, 3, 0.5)]:
		var scene := _fixture()
		var geometry: MeshInstance3D = scene.get_child(0).get_child(0)
		geometry.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3(1, 1, 0), Vector3(0, 1, 1)]))
		geometry.position = Vector3(3, 4, 3)
		geometry.basis = Basis.from_euler(Vector3(0.2, -0.3, 0.4)).scaled(scale)
		report = exporter.export_scene(scene, destination + "-orientation-oblique-%s" % ("negative" if scale.x < 0 else "positive"))
		_check(report.ok, "Face oblíqua/escala %s exportável: %s" % [scale, report.errors])
		if report.ok:
			var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
			var vertices := _read_triangle(bytes, 0)
			var decoded_normal := (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalized()
			var expected := (geometry.basis.inverse().transposed() * Vector3(1, -1, 1).normalized()).normalized()
			_check(decoded_normal.dot(expected) > 0.99999, "Normal oblíqua coincide com inverse transpose para %s" % scale)
		scene.free()
	# Absent normals assume Godot CW explicitly. Swapping vertices must swap UVs
	# with them, and a mirrored source must not be flipped a second time.
	for reflected in [false, true]:
		var scene := _fixture()
		var geometry: MeshInstance3D = scene.get_child(0).get_child(0)
		geometry.mesh = _triangle_mesh(PackedVector3Array([Vector3.ZERO, Vector3.UP, Vector3.RIGHT]), PackedVector2Array([Vector2(0.2, 0.1), Vector2(0.4, 0.5), Vector2(0.8, 0.9)]), false)
		geometry.position = Vector3(2, 2, 2)
		geometry.basis = Basis.from_euler(Vector3(0.15, 0.1, 0.2)).scaled(Vector3(-2 if reflected else 2, 1, 0.5))
		var image := Image.create(2, 2, false, Image.FORMAT_RGB8)
		image.set_pixel(0, 0, Color.RED)
		image.set_pixel(1, 0, Color.GREEN)
		image.set_pixel(0, 1, Color.BLUE)
		image.set_pixel(1, 1, Color.YELLOW)
		geometry.material_override.albedo_color = Color.WHITE
		geometry.material_override.albedo_texture = ImageTexture.create_from_image(image)
		report = exporter.export_scene(scene, destination + "-orientation-no-normal-%s" % str(reflected))
		_check(report.ok, "CW sem normais/reflexão%s exportável: %s" % [reflected, report.errors])
		_check(report.warnings.any(func(warning: String) -> bool: return warning.contains("malha sem normais explícitas")), "Convenção CW sem normais gera aviso explícito")
		if report.ok:
			var bytes := FileAccess.get_file_as_bytes(report.output_dir.path_join("d3d-models/editor/cabin-west.d3d"))
			var vertices := _read_triangle(bytes, 0)
			var decoded_normal := (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalized()
			var expected := (geometry.basis.inverse().transposed() * Vector3(0, 0, 1)).normalized()
			_check(decoded_normal.dot(expected) > 0.99999, "CW sem normais preserva normal física mesmo com reflexão%s" % reflected)
			var width := bytes.decode_u32(12)
			var height := bytes.decode_u32(16)
			var atlas := Image.create_from_data(width, height, false, Image.FORMAT_RGB8, bytes.slice(80))
			var expected_colors := [Color.RED, Color.BLUE, Color.YELLOW] if reflected else [Color.RED, Color.YELLOW, Color.BLUE]
			for vertex in 3:
				var offset := 20 + vertex * 20
				var x := clampi(int(bytes.decode_float(offset + 12) * width), 0, width - 1)
				var y := clampi(int(bytes.decode_float(offset + 16) * height), 0, height - 1)
				_check(atlas.get_pixel(x, y).is_equal_approx(expected_colors[vertex]), "UV acompanha vértice orientado %s/%d" % [reflected, vertex])
		scene.free()
	var singular := _fixture()
	singular.get_child(0).get_child(0).scale = Vector3(0, 1, 1)
	report = exporter.export_scene(singular, destination + "-orientation-singular")
	_check(not report.ok and report.errors.any(func(error: String) -> bool: return error.contains("singular")), "Escala singular rejeitada explicitamente")
	singular.free()


func _read_triangle(bytes: PackedByteArray, face: int) -> Array[Vector3]:
	var result: Array[Vector3] = []
	for vertex in 3:
		var offset := 20 + face * 60 + vertex * 20
		result.append(Vector3(bytes.decode_float(offset), bytes.decode_float(offset + 4), bytes.decode_float(offset + 8)))
	return result


func _transaction_case() -> void:
	var scene := _fixture()
	var invalid := _fixture()
	var moved: Node = invalid.get_child(0)
	invalid.remove_child(moved)
	invalid.free()
	moved.set_meta("d3d_instance", {"instanceId": "house-gillian", "assetId": "tristram.arch.house-common", "variantId": "gillian", "nativeMin": [36, 64], "nativeMax": [42, 68]})
	moved.position = Vector3(36, 0, 64)
	moved.get_child(0).material_override.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	scene.add_child(moved)
	var target := destination + "-transaction"
	var report := exporter.export_scene(scene, target)
	_check(not report.ok, "Uma instância inválida cancela o pacote inteiro")
	_check(not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(target)), "Instância válida também não é publicada parcialmente")
	scene.free()


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
