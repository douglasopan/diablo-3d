extends SceneTree

const Snapshot = preload("res://addons/tristram_editor/snapshot.gd")

var failures: Array[String] = []


func _initialize() -> void:
	var folder := "res://local/snapshot-test"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(folder))
	var fixture := {
		"format": "d3d.town-snapshot", "schemaVersion": 1, "edition": "retail", "gridSize": [112, 112], "syntheticFixture": true,
		"collision": [[26, 48], [70, 66]],
		"models": [
			{"instanceId": "cabin-west", "assetId": "tristram.arch.cabin", "variantId": "west-short", "kind": 5, "nativeMin": [26, 48], "nativeMax": [30, 52], "sha256": "", "revision": "fixture-synthetic", "external": false, "texture": "", "triangles": [[26, 0, 48, 0, 0, 27, 0, 48, 1, 0, 26, 1, 48, 0, 1, 0, 0, 0], [26, 0, 48, 0, 0, 26, 1, 48, 0, 1, 27, 0, 48, 1, 0, 2, 1, 1]]},
			{"instanceId": "cabin-east", "assetId": "tristram.arch.cabin", "variantId": "east-long", "kind": 5, "nativeMin": [70, 66], "nativeMax": [74, 72], "sha256": "", "revision": "fixture-synthetic-fire", "external": false, "texture": "", "HasFireSources": true, "triangles": [[70, 1, 66, 0, 0, 71, 1, 66, 1, 0, 70, 2, 66, 0, 1, 3, 1, 12]]}
		]
	}
	var path := folder.path_join("synthetic.json")
	_write(path, fixture)
	var result: Dictionary = Snapshot.new().build(path)
	_check(result["ok"], "valid snapshot builds: %s" % [result["errors"]])
	if result["ok"]:
		var scene: Node3D = result["root"]
		_check(not scene.has_node("GroundReference"), "ground remains optional")
		var west: Node3D = scene.get_node("Architecture/cabin-west")
		_check(west.position == Vector3(26, 0, 48), "native origin is instance transform")
		_check(not west.get_meta("d3d_override"), "baseline is inherited")
		_check(not west.get_meta("d3d_has_fire"), "ordinary instance has no inferred fire")
		_check(west.get_meta("d3d_variant_valid"), "existing registry variant resolves")
		_check(not west.get_meta("d3d_instance").has("triangles"), "metadata omits bulk triangles")
		_check(west.get_child_count() == 2, "interior is kept separately from exterior")
		var mesh_node: MeshInstance3D = west.get_child(0)
		var arrays: Array = mesh_node.mesh.surface_get_arrays(0)
		_check(arrays[Mesh.ARRAY_VERTEX][0] + west.position == Vector3(26, 0, 48), "absolute geometry is not translated twice")
		_check(mesh_node.owner == scene, "geometry belongs to saved scene")
		var east: Node3D = scene.get_node("Architecture/cabin-east")
		_check(east.get_meta("d3d_has_fire"), "native fire is recognized")
		_check(east.get_meta("d3d_snapshot_light_count") == 0, "missing light coordinates do not invent lights")
		var collision: Node3D = scene.get_node("CollisionReference")
		_check(collision.get_meta("d3d_locked_reference") and collision.get_meta("d3d_export_ignore"), "collision is reference-only")
		_check(collision.get_meta("d3d_native_cells").size() == 2, "native blocked cells are retained")
		_check(scene.get_meta("d3d_snapshot_sha256") == FileAccess.get_sha256(path), "snapshot hash describes real bytes")
		var packed := PackedScene.new()
		_check(packed.pack(scene) == OK, "scene packs")
		var saved_path := folder.path_join("roundtrip.tscn")
		_check(ResourceSaver.save(packed, saved_path) == OK, "scene saves")
		var loaded: PackedScene = ResourceLoader.load(saved_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
		var reopened: Node3D = loaded.instantiate()
		_check(reopened.get_node("Architecture/cabin-west").get_child_count() == 2, "saved scene preserves mesh hierarchy")
		_check(reopened.get_node("Architecture/cabin-east").get_meta("d3d_has_fire"), "saved scene preserves fire rejection metadata")
		reopened.free()
		scene.free()
	var invalid: Dictionary = fixture.duplicate(true)
	invalid["models"][0]["triangles"][0].pop_back()
	_write(path, invalid)
	_check(not Snapshot.new().build(path)["ok"], "malformed triangle fails atomically")
	invalid = fixture.duplicate(true)
	invalid["models"][1]["instanceId"] = "cabin-west"
	_write(path, invalid)
	_check(not Snapshot.new().build(path)["ok"], "duplicate identity fails")
	_write(path, fixture)
	_check_ground(folder, fixture)
	if "--real" in OS.get_cmdline_user_args():
		_check_real()
	if failures.is_empty():
		print("SNAPSHOT_SMOKE_OK: absolute coordinates, interiors, registry, fire, locked collision, optional batched ground/alpha, hashes, save/reopen and invalid input.")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)


func _check_real() -> void:
	var result: Dictionary = Snapshot.new().build("res://local/town-snapshot.json")
	_check(result["ok"], "real local snapshot builds: %s" % [result["errors"]])
	if not result["ok"]:
		return
	var scene: Node3D = result["root"]
	_check(scene.get_node("Architecture").get_child_count() > 0, "real snapshot includes architecture")
	if scene.has_node("GroundReference"):
		_check(scene.get_meta("d3d_ground_tile_count") > 0, "real ground contains native floor cells")
		_check(scene.get_node("GroundReference").get_child_count() == scene.get_meta("d3d_ground_texture_count"), "real ground texture batches match metadata")
		print("REAL_SNAPSHOT_OK: models=%d ground_tiles=%d ground_meshes=%d warnings=%d" % [scene.get_node("Architecture").get_child_count(), scene.get_meta("d3d_ground_tile_count"), scene.get_meta("d3d_ground_texture_count"), result["warnings"].size()])
	scene.free()


func _check_ground(folder: String, fixture: Dictionary) -> void:
	var ground_dir := folder.path_join("ground")
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(ground_dir))
	var image := Image.create(64, 32, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.3, 0.4, 0.2, 1.0))
	image.set_pixel(0, 0, Color(0, 0, 0, 0))
	_check(image.save_png(ground_dir.path_join("fallback.png")) == OK, "ground cutout fixture saves")
	image.fill(Color(0.2, 0.3, 0.4, 0.5))
	_check(image.save_png(ground_dir.path_join("p7.png")) == OK, "ground blend fixture saves")
	var ground_fixture: Dictionary = fixture.duplicate(true)
	ground_fixture["ground"] = {
		"source": "synthetic fixture, not actual game floor",
		"textures": [{"key": "fallback", "path": "ground/fallback.png", "width": 64, "height": 32}, {"key": "p7", "path": "ground/p7.png", "width": 64, "height": 32}],
		"tiles": [[26, 48, "fallback"], [27, 48, "fallback"], [28, 48, "p7"]]
	}
	var path := folder.path_join("synthetic-ground.json")
	_write(path, ground_fixture)
	var result: Dictionary = Snapshot.new().build(path)
	_check(result["ok"], "RGBA ground builds without architecture transparency rejection: %s" % [result["errors"]])
	if result["ok"]:
		var scene: Node3D = result["root"]
		var layer: Node3D = scene.get_node("GroundReference")
		_check(layer.get_child_count() == 2, "ground is batched per texture rather than per tile")
		_check(layer.get_meta("d3d_locked_reference") and layer.get_meta("d3d_export_ignore"), "ground is a locked nonexported reference")
		_check(scene.get_meta("d3d_ground_tile_count") == 3 and scene.get_meta("d3d_ground_texture_count") == 2, "ground counts describe actual meshes")
		_check(not scene.get_meta("d3d_snapshot").has("ground"), "bulk ground data is not duplicated in root metadata")
		var cutout: MeshInstance3D = layer.get_node("Chão_fallback")
		var blend: MeshInstance3D = layer.get_node("Chão_p7")
		var arrays: Array = cutout.mesh.surface_get_arrays(0)
		_check(arrays[Mesh.ARRAY_VERTEX].size() == 8 and arrays[Mesh.ARRAY_INDEX].size() == 12, "two ground quads share one mesh")
		_check(arrays[Mesh.ARRAY_VERTEX][0] == Vector3(24.53125, 0, 47.53125) and arrays[Mesh.ARRAY_VERTEX][1] == Vector3(25.53125, 0, 46.53125), "ground uses exact native inverse-projection corners")
		_check(arrays[Mesh.ARRAY_TEX_UV][0] == Vector2(0, 0) and arrays[Mesh.ARRAY_TEX_UV][2] == Vector2(1, 1), "ground preserves source rectangle UVs")
		var material: StandardMaterial3D = cutout.mesh.surface_get_material(0)
		_check(material.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR, "binary floor alpha uses scissor")
		_check(blend.mesh.surface_get_material(0).transparency == BaseMaterial3D.TRANSPARENCY_ALPHA, "graded floor alpha uses blending")
		_check(material.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED and material.texture_filter == BaseMaterial3D.TEXTURE_FILTER_NEAREST, "floor reference remains unshaded nearest")
		_check(cutout.owner == scene and cutout.get_meta("d3d_export_ignore"), "ground ownership and export exclusion survive packing")
		var packed := PackedScene.new()
		_check(packed.pack(scene) == OK, "ground scene packs")
		var saved_path := folder.path_join("ground-roundtrip.tscn")
		_check(ResourceSaver.save(packed, saved_path) == OK, "ground scene saves")
		var loaded: PackedScene = ResourceLoader.load(saved_path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
		var reopened := loaded.instantiate()
		_check(reopened.get_node("GroundReference/Chão_fallback").mesh.surface_get_material(0).transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR, "ground alpha survives save/reopen")
		reopened.free()
		scene.free()
	ground_fixture["ground"]["tiles"][0][2] = "missing"
	_write(path, ground_fixture)
	_check(not Snapshot.new().build(path)["ok"], "unknown ground texture is rejected without silently losing tiles")
	ground_fixture["ground"]["tiles"][0][2] = "fallback"
	_write(path, ground_fixture)


func _write(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(value))
	file.close()


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
