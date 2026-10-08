extends SceneTree
## Integração local real. O único override é uma caixa sintética identificada no poço.
## Não altera local/tristram.tscn, local/export nem o perfil habitual do jogo.

const Snapshot := preload("res://addons/tristram_editor/snapshot.gd")
const Exporter := preload("res://addons/tristram_editor/exporter.gd")
const GLBSource := preload("res://addons/tristram_editor/glb_source.gd")
const Preview := preload("res://addons/tristram_editor/preview.gd")

var checks := 0
var failures: Array[String] = []
var output_dir := "res://local/roundtrip"

func _initialize() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			var requested := argument.trim_prefix("--output=").simplify_path()
			if requested.begins_with("res://local/") and requested != "res://local/":
				output_dir = requested
	call_deferred("_run")

func _check(value: bool, description: String) -> void:
	checks += 1
	if not value:
		failures.append(description)
		push_error(description)

func _run() -> void:
	var snapshot_path := ProjectSettings.globalize_path("res://local/town-snapshot.json")
	if not FileAccess.file_exists(snapshot_path):
		print("EDITOR_ROUNDTRIP: snapshot real ausente. Execute Abrir-Editor-Godot.cmd para preparar; nenhum fixture foi usado em seu lugar.")
		quit(2)
		return
	var report: Dictionary = Snapshot.new().build(snapshot_path)
	_check(report.ok, "Snapshot real deve ser carregado: %s" % [report.errors])
	if not report.ok:
		_finish()
		return
	var scene: Node3D = report.root
	var snapshot_sha := str(scene.get_meta("d3d_snapshot_sha256", ""))
	root.add_child(scene)
	_check(scene.get_node("Architecture").get_child_count() == 14, "Snapshot real tem 14 vínculos de arquitetura")
	var baseline_count := _baseline_triangles(scene)
	var cabin: Node3D = scene.get_node("Architecture/cabin-east")
	_check(cabin.get_meta("d3d_has_fire", false), "Cabana real tem adjunto de fogo")
	_check(not cabin.get_meta("d3d_override", true), "Cabana permanece herdada")
	var inherited_export: Dictionary = Exporter.new().export_scene(scene, ProjectSettings.globalize_path(output_dir.path_join("inherited-must-not-exist")))
	_check(not inherited_export.ok, "Baseline inteira não exporta automaticamente")
	var identity: Dictionary = cabin.get_meta("d3d_instance")
	var glb := str(identity.get("sourceGlb", identity.get("sourceGLB", "")))
	if not glb.is_empty():
		var path := snapshot_path.get_base_dir().path_join(glb) if not glb.is_absolute_path() else glb
		_check(FileAccess.get_sha256(path) == identity.get("sourceSha256", ""), "Hash do GLB fonte corresponde à revisão explícita")
		var imported: Dictionary = GLBSource.import_glb(path, cabin, false)
		_check(imported.ok, "GLB fonte importado somente como referência: %s" % [imported.errors])
		if imported.ok:
			_check(imported.branch.get_meta("d3d_export_ignore", false), "GLB fonte não entra no pacote")
			_check(GLBSource.apply_recorded_fit(imported.branch, identity.get("sourceFit", {})), "Fit registrado do GLB selecionado é aplicado")
			var count_before := cabin.get_child_count()
			var repeated: Dictionary = GLBSource.import_glb(path, cabin, false)
			_check(repeated.ok and cabin.get_child_count() == count_before, "Reinspeção da mesma fonte não cria duplicatas")
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))
	var ignore := FileAccess.open(output_dir.path_join(".gdignore"), FileAccess.WRITE)
	if ignore != null:
		ignore.close()
	var scene_path := output_dir.path_join("tristram-snapshot.tscn")
	var packed := PackedScene.new()
	_check(packed.pack(scene) == OK and ResourceSaver.save(packed, scene_path) == OK, "Snapshot + GLB referência salvos em TSCN")
	var loaded: PackedScene = ResourceLoader.load(scene_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	_check(loaded != null, "TSCN salva pode ser reaberta")
	if loaded == null:
		scene.free()
		_finish()
		return
	var reopened: Node3D = loaded.instantiate()
	root.add_child(reopened)
	_check(_baseline_triangles(reopened) == baseline_count, "Todas as faces do snapshot sobrevivem ao save/reopen")
	_check(reopened.get_meta("d3d_snapshot_sha256", "") == snapshot_sha, "Hash do snapshot carregado persiste em TSCN mesmo se a entrada local for atualizada")
	var saved_cabin: Node3D = reopened.get_node("Architecture/cabin-east")
	_check(saved_cabin.get_meta("d3d_has_fire", false) and not saved_cabin.get_meta("d3d_override", true), "Cabana fogo/herdada persiste em TSCN")
	var well: Node3D = reopened.get_node("Architecture/well")
	for child in well.get_children():
		if child is Node3D:
			child.visible = false
	var box := MeshInstance3D.new()
	box.name = "SYNTHETIC_TEST_ONLY_static_box"
	box.mesh = BoxMesh.new()
	box.mesh.size = Vector3(1, 0.5, 1)
	box.position = Vector3(0.5, 0.25, 0.5)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.35, 0.55, 0.8)
	material.roughness = 1.0
	box.material_override = material
	box.set_meta("d3d_source_mode", "replacement")
	well.add_child(box)
	box.owner = reopened
	well.set_meta("d3d_override", true)
	well.set_meta("d3d_revision", "synthetic-static-box-roundtrip-v1")
	var exported: Dictionary = Exporter.new().export_scene(reopened, ProjectSettings.globalize_path(output_dir.path_join("export")))
	_check(exported.ok, "Export da caixa sintética vinculada ao poço: %s" % [exported.errors])
	if exported.ok:
		_check(exported.instances == 1, "Só uma instância explicitamente marcada foi exportada")
		var manifest := FileAccess.get_file_as_string(ProjectSettings.globalize_path(output_dir.path_join("export/d3d-maps/tristram.ini")))
		_check(manifest.contains("instance=well") and not manifest.contains("instance=cabin-east"), "INI substitui poço; cabana segue baseline")
		var model_path := ProjectSettings.globalize_path(output_dir.path_join("export/d3d-models/editor/well.d3d"))
		var bytes := FileAccess.get_file_as_bytes(model_path)
		_check(bytes.slice(0, 8).get_string_from_ascii() == "D3DMESH1" and bytes.decode_u32(8) == 12, "D3DMESH1 contém 12 triângulos reais da caixa")
		var outward := true
		for face in 12:
			var vertices: Array[Vector3] = []
			for corner in 3:
				var offset := 20 + face * 60 + corner * 20
				vertices.append(Vector3(bytes.decode_float(offset), bytes.decode_float(offset + 4), bytes.decode_float(offset + 8)))
			var normal := (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0])
			var center := (vertices[0] + vertices[1] + vertices[2]) / 3.0
			outward = outward and normal.dot(center - Vector3(0.5, 0.25, 0.5)) > 0.0
		_check(outward, "Ordem CCW das 12 faces aponta para fora no D3DMESH1, preservando a iluminação do jogo")
	packed = PackedScene.new()
	_check(packed.pack(reopened) == OK and ResourceSaver.save(packed, output_dir.path_join("tristram-candidate.tscn")) == OK, "Cena candidata separada salva sem tocar a cena do usuário")
	if OS.get_cmdline_user_args().has("--render-preview"):
		await _render_preview(scene)
	var evidence := {"format": "d3d.godot-editor-roundtrip-test", "syntheticOverride": true, "testOnlyInstance": "well", "snapshotSha256": snapshot_sha, "baselineTriangles": baseline_count, "checks": checks, "failures": failures, "pack": "local/roundtrip/export", "visualApproval": false}
	var file := FileAccess.open(output_dir.path_join("evidence.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(evidence, "\t") + "\n")
	file.close()
	scene.free()
	reopened.free()
	_finish()

func _baseline_triangles(node: Node) -> int:
	var count := 0
	if node is MeshInstance3D and node.has_meta("d3d_surface"):
		count = node.mesh.get_faces().size() / 3
	for child in node.get_children():
		count += _baseline_triangles(child)
	return count

func _render_preview(scene: Node3D) -> void:
	if DisplayServer.get_name() == "headless":
		_check(false, "--render-preview requer backend gráfico; rode sem --headless")
		return
	root.size = Vector2i(1280, 820)
	var preview := Preview.new()
	root.add_child(preview)
	preview.set_source(scene)
	preview.select_instance("cabin-east", true)
	await process_frame
	await RenderingServer.frame_post_draw
	var image: Image = preview.viewport.get_texture().get_image()
	_check(not image.is_empty() and image.save_png(output_dir.path_join("preview-cabin-snapshot.png")) == OK, "Prévia geométrica real capturada")
	preview.display_mode = 2
	preview.refresh()
	preview.focus_selected()
	await process_frame
	await RenderingServer.frame_post_draw
	image = preview.viewport.get_texture().get_image()
	_check(not image.is_empty() and image.save_png(output_dir.path_join("preview-cabin-source-glb.png")) == OK, "Prévia GLB fonte separada capturada")
	preview.display_mode = 1
	preview.interior_only = true
	preview.refresh()
	preview.focus_selected()
	await process_frame
	await RenderingServer.frame_post_draw
	image = preview.viewport.get_texture().get_image()
	_check(not image.is_empty() and image.save_png(output_dir.path_join("preview-cabin-interior.png")) == OK, "Interior real do snapshot capturado sem modificar a cena")
	preview.free()

func _finish() -> void:
	print("EDITOR_ROUNDTRIP checks=%d failures=%d; pacote sintético de integração: local/roundtrip/export" % [checks, failures.size()])
	quit(0 if failures.is_empty() else 1)
