extends SceneTree
## Isolated generated fixtures; optional private renders via --manifest and --output.

const MANIFEST = preload("res://addons/asset_lod_review/lod_manifest.gd")
const REVIEW = preload("res://review/lod_review.tscn")
var failures: Array[String] = []
var checks := 0
var folder := ""
var chosen_manifest := ""
var output := ""
var render_only := false
var fixtures_finished := false
var render_finished := false

func _initialize() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--manifest="): chosen_manifest = argument.trim_prefix("--manifest=")
		if argument.begins_with("--output="): output = argument.trim_prefix("--output=")
		if argument == "--render-only": render_only = true
	folder = OS.get_user_data_dir().path_join("lod-review-fixture-%d" % Time.get_ticks_usec())
	call_deferred("_run")

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition: failures.append(message)

func _run() -> void:
	if not render_only:
		await _fixtures()
		_check(fixtures_finished, "Fixture suite interrupted by a script error.")
	if not output.is_empty():
		await _render_private()
		_check(render_finished, "Private render interrupted before evidence was written.")
	for failure in failures: push_error(failure)
	print("ASSET_LOD_REVIEW checks=%d failures=%d" % [checks, failures.size()])
	quit(0 if failures.is_empty() else 1)

func _fixtures() -> void:
	DirAccess.make_dir_recursive_absolute(folder)
	var master_path := folder.path_join("master.glb")
	var drawable_path := folder.path_join("drawable.glb")
	_write_bytes(master_path, _glb_fixture(false))
	_write_bytes(drawable_path, _glb_fixture(true))
	var manifest := {"schemaVersion": 2, "assetId": "fixture.arch.box", "variant": "synthetic", "revision": "isolated-v2", "geometricReferenceSha256": FileAccess.get_sha256(drawable_path), "anchor": {"position": [0, 0, 0], "upAxis": "Y"}, "openings": {"pipelineId": "fixture-no-runtime-cuts", "compatibility": "native-unchanged"}, "fit": {"uniformScale": 2.0, "yawRadians": 0.2, "translation": [3, 0, -1]}, "sources": [{"role": "master-geometry", "path": "master.glb", "sha256": FileAccess.get_sha256(master_path)}, {"role": "drawable-lod", "path": "drawable.glb", "sha256": FileAccess.get_sha256(drawable_path), "lodIndex": 0, "geometricError": 0.0, "errorVerified": true}, {"role": "drawable-lod", "path": "drawable.glb", "sha256": FileAccess.get_sha256(drawable_path), "lodIndex": 1, "geometricError": 0.03, "errorVerified": false, "openingEquivalenceVerified": false, "maxDiameterPixels": 80.0}]}
	var manifest_path := folder.path_join("manifest.json")
	_write_text(manifest_path, JSON.stringify(manifest))
	var source_hash_before := FileAccess.get_sha256(drawable_path)
	var master_hash_before := FileAccess.get_sha256(master_path)
	var manifest_hash_before := FileAccess.get_sha256(manifest_path)
	var good := MANIFEST.read(manifest_path)
	_check(good.ok, "Valid v2 manifest rejected.")
	_check(good.sources[0].resolvedPath == master_path.replace("\\", "/"), "Relative source path did not resolve beside manifest.")
	_check(MANIFEST.resolve_path(drawable_path, folder) == drawable_path.replace("\\", "/"), "Absolute path changed identity.")
	_check(not MANIFEST.validate([]).ok, "Non-object manifest accepted.")
	for mutation in ["schemaVersion", "assetId", "variant", "revision", "geometricReferenceSha256", "anchor", "openings", "sources"]:
		var bad := manifest.duplicate(true)
		bad.erase(mutation)
		_check(not MANIFEST.validate(bad).ok, "Missing %s accepted." % mutation)
	for value in [[0, 0], [0, 0, NAN], [0, 0, "1"]]:
		var bad := manifest.duplicate(true)
		bad.anchor.position = value
		_check(not MANIFEST.validate(bad).ok, "Invalid anchor accepted.")
	for mutation in ["role", "path", "sha256", "lodIndex", "geometricError", "errorVerified"]:
		var bad := manifest.duplicate(true)
		bad.sources[1].erase(mutation)
		_check(not MANIFEST.validate(bad).ok, "Missing source %s accepted." % mutation)
	var invalid_cases := [{"uniformScale": 0}, {"uniformScale": -1}, {"uniformScale": [1, 2, 3]}, {"yawRadians": INF}, {"translation": [0, NAN, 0]}]
	for values in invalid_cases:
		var bad := manifest.duplicate(true)
		bad.fit.merge(values, true)
		_check(not MANIFEST.validate(bad).ok, "Invalid shared transform accepted.")
	for values in [{"lodIndex": 1}, {"lodIndex": 0.5}, {"geometricError": -1}, {"errorVerified": false}, {"runtimePath": "a.mesh"}, {"maxDiameterPixels": -1}, {"sha256": "abc"}, {"role": "drawable-master"}]:
		var bad := manifest.duplicate(true)
		bad.sources[1].merge(values, true)
		_check(not MANIFEST.validate(bad).ok, "Invalid source metadata accepted.")
	var with_runtime := manifest.duplicate(true)
	with_runtime.sources[1]["runtimePath"] = "optional-private.mesh"
	with_runtime.sources[1]["runtimeSha256"] = "a".repeat(64)
	_check(MANIFEST.validate(with_runtime).ok, "Valid optional runtime pair rejected.")
	var finest_unlimited := manifest.duplicate(true)
	finest_unlimited.sources[1]["maxDiameterPixels"] = 0
	_check(MANIFEST.validate(finest_unlimited).ok, "Finest unlimited diameter rejected.")
	var coarse_unlimited := manifest.duplicate(true)
	coarse_unlimited.sources[2]["maxDiameterPixels"] = 0
	_check(not MANIFEST.validate(coarse_unlimited).ok, "Coarse unlimited diameter accepted.")
	var wrong_reference := manifest.duplicate(true)
	wrong_reference.geometricReferenceSha256 = "b".repeat(64)
	_check(not MANIFEST.validate(wrong_reference).ok, "Wrong geometric reference accepted.")
	var multi_master := manifest.duplicate(true)
	multi_master.sources.append(manifest.sources[0].duplicate(true))
	_check(MANIFEST.validate(multi_master).ok, "Multiple inspect-only masters rejected.")
	var master_runtime := manifest.duplicate(true)
	master_runtime.sources[0]["runtimePath"] = "master-must-not-be-drawable.mesh"
	master_runtime.sources[0]["runtimeSha256"] = "b".repeat(64)
	_check(not MANIFEST.validate(master_runtime).ok, "Inspect-only master declared runtime drawable.")
	for field in ["maxDiameterPixels", "openingEquivalenceVerified"]:
		var incomplete_coarse := manifest.duplicate(true)
		incomplete_coarse.sources[2].erase(field)
		_check(not MANIFEST.validate(incomplete_coarse).ok, "Coarse missing %s accepted." % field)
	var gap := manifest.duplicate(true)
	gap.sources[2].lodIndex = 2
	_check(not MANIFEST.validate(gap).ok, "Non-contiguous LOD indices accepted.")
	var monotonic := manifest.duplicate(true)
	var coarse2: Dictionary = manifest.sources[2].duplicate(true)
	coarse2.lodIndex = 2
	coarse2.geometricError = 0.06
	coarse2.maxDiameterPixels = 40
	monotonic.sources.append(coarse2)
	_check(MANIFEST.validate(monotonic).ok, "Valid monotonic LOD sequence rejected.")
	monotonic.sources[3].maxDiameterPixels = 90
	_check(not MANIFEST.validate(monotonic).ok, "Non-monotonic diameter thresholds accepted.")
	monotonic.sources[3].maxDiameterPixels = 40
	monotonic.sources[3].geometricError = 0.01
	_check(not MANIFEST.validate(monotonic).ok, "Decreasing geometric error accepted.")
	var scale_xyz := manifest.duplicate(true)
	scale_xyz.fit["scaleXYZ"] = [1, 2, 3]
	_check(not MANIFEST.validate(scale_xyz).ok, "Nonuniform fit accepted.")
	var hash_bad: Dictionary = good.sources[1].duplicate(true)
	hash_bad.sha256 = "0".repeat(64)
	_check(not MANIFEST.load_glb(hash_bad).ok, "Hash mismatch loaded.")
	var absent: Dictionary = good.sources[1].duplicate(true)
	absent.resolvedPath = folder.path_join("absent.glb")
	_check(not MANIFEST.load_glb(absent).ok, "Absent source loaded.")
	_check(not MANIFEST.describe_glb(PackedByteArray([1, 2, 3])).ok, "Truncated GLB accepted.")
	_check(not MANIFEST.describe_glb(_glb_fixture(true, true)).ok, "External resource accepted.")
	var master := MANIFEST.load_glb(good.sources[0])
	_check(master.ok, "Master GLB failed import.")
	if master.ok:
		_check(master.inspectOnly and master.gray, "UV-less master was not gray inspect-only.")
		_check(not master.summary.uvPresent and master.summary.normalsPresent, "Master descriptor lost attribute identity.")
		var mesh := _first_mesh(master.node)
		_check(mesh != null and mesh.material_override is StandardMaterial3D, "Gray master material missing.")
		master.node.free()
	var drawable := MANIFEST.load_glb(good.sources[1])
	_check(drawable.ok, "Drawable GLB failed import.")
	if drawable.ok:
		_check(not drawable.inspectOnly and not drawable.gray, "Drawable forced to inspect-only/gray.")
		_check(drawable.summary.uvPresent and drawable.summary.normalsPresent, "Drawable UV/normals not recognized.")
		_check(drawable.summary.triangles == 12, "Triangle count wrong.")
		_check(drawable.summary.textureChannels.size() == 3, "PBR texture channels not recognized.")
		var mesh := _first_mesh(drawable.node)
		var material: StandardMaterial3D = mesh.get_active_material(0) if mesh != null else null
		_check(material != null and material.albedo_texture != null, "Base color texture lost on import.")
		_check(material != null and material.normal_enabled and material.normal_texture != null, "Normal map lost on import.")
		_check(material != null and material.roughness_texture != null, "Metallic/roughness map lost on import.")
		_check(mesh != null and mesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_NORMAL].size() == 24, "Imported smooth/vertex normals absent.")
		var first_normal: Vector3 = mesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_NORMAL][0] if mesh != null else Vector3.ZERO
		_check(first_normal.dot(Vector3.BACK) > 0.99999, "Authored vertex normal changed on import: " + str(first_normal))
		drawable.node.free()
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1460, 940)
	root.add_child(viewport)
	var review := REVIEW.instantiate()
	viewport.add_child(review)
	review.set_deferred("size", Vector2(1460, 940))
	await process_frame
	_check(review.load_manifest(manifest_path), "Viewer refused valid pair.")
	await process_frame
	_check(review.sides[0].camera.transform.is_equal_approx(review.sides[1].camera.transform), "Cameras diverged.")
	_check(review.sides[0].frame.transform.is_equal_approx(review.sides[1].frame.transform), "Source fitting was not uniform/shared.")
	_check(review.sides[0].frame.basis.get_scale().is_equal_approx(Vector3.ONE * 2), "Uniform object scale lost.")
	review.set_review_camera(1.6, 0.4, 0.6)
	_check(review.sides[0].camera.transform.is_equal_approx(review.sides[1].camera.transform), "Orbit/close cameras diverged.")
	_check(review.select_source(1, 2, false), "Unverified source cannot be inspected.")
	await process_frame
	_check("NÃO VERIFICADO" in review.sides[1].hud.text, "HUD hid unverified error.")
	_check("aberturas NÃO VERIFICADAS" in review.sides[1].hud.text, "HUD hid unverified opening equivalence.")
	review.set_review_camera(0, 0.15, 0.1)
	await process_frame
	_check("Limite cruza near: exigir drawable mais fino" in review.sides[1].hud.text, "Near-plane crossing reported a finite trustworthy diameter.")
	_check(review.review_note().approvalStatus == "pending", "Viewer falsely approved an asset.")
	_check(not JSON.stringify(review.review_note()).contains(folder), "Review note leaked source path.")
	_check(not review.save_note(manifest_path), "Review note overwrote manifest.")
	_check(not review.save_note(master_path), "Review note overwrote master source.")
	_check(not review.save_note(manifest_path.to_upper()), "Review note bypassed Windows path case protection.")
	var note_path := folder.path_join("review-note.json")
	_check(review.save_note(note_path), "Local note export failed.")
	_check(FileAccess.get_sha256(master_path) == master_hash_before, "Master bytes changed.")
	_check(FileAccess.get_sha256(drawable_path) == source_hash_before, "Drawable bytes changed.")
	_check(FileAccess.get_sha256(manifest_path) == manifest_hash_before, "Manifest bytes changed.")
	viewport.free()
	for path in [master_path, drawable_path, manifest_path, note_path]: DirAccess.remove_absolute(path)
	DirAccess.remove_absolute(folder)
	fixtures_finished = true

func _render_private() -> void:
	if chosen_manifest.is_empty():
		_check(false, "Private render requires --manifest=PATH.")
		return
	if DisplayServer.get_name() == "headless":
		_check(false, "Render requires graphics; run fixtures headless without --output.")
		return
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1600, 1000)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var review := REVIEW.instantiate()
	viewport.add_child(review)
	review.set_deferred("size", Vector2(1600, 1000))
	await process_frame
	await process_frame
	_check(review.errors.is_empty(), "Private manifest failed: " + " ".join(review.errors))
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--left-source-index="): _check(review.select_source(0, int(argument.trim_prefix("--left-source-index="))), "Left private source failed.")
		if argument.begins_with("--right-source-index="): _check(review.select_source(1, int(argument.trim_prefix("--right-source-index="))), "Right private source failed.")
	var views := [[0.0, 0.2, 1.0], [PI / 2, 0.2, 1.0], [PI, 0.2, 1.0], [PI * 1.5, 0.2, 1.0], [0.4, 0.15, 0.52], [PI / 2, 0.12, 0.52], [PI / 2, 0.03, 0.30]]
	var rendered_views: Array[Dictionary] = []
	for index in views.size():
		var camera: Array = views[index]
		review.set_review_camera(camera[0], camera[1], camera[2])
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		var image := viewport.get_texture().get_image()
		_check(image != null and not image.is_empty(), "No rendered pixels.")
		if image != null and not image.is_empty():
			var path := output if index == 0 else output.get_basename() + "-%d.png" % index
			_check(image.save_png(path) == OK, "Failed to save private render.")
			var record: Dictionary = review.review_note()
			record["image"] = path.get_file()
			record["imageSha256"] = FileAccess.get_sha256(path)
			record["size"] = [image.get_width(), image.get_height()]
			rendered_views.append(record)
	for source in review.manifest_result.sources:
		_check(FileAccess.get_sha256(source.resolvedPath) == source.sha256, "Private GLB changed during render.")
	_check(FileAccess.get_sha256(chosen_manifest) == review.manifest_result.sha256, "Private manifest changed during render.")
	_write_text(output.get_basename() + "-evidence.json", JSON.stringify({"format": "town-asset-lod-render-evidence-v1", "approvalStatus": "pending", "views": rendered_views}, "\t") + "\n")
	viewport.free()
	render_finished = true

func _first_mesh(node: Node) -> MeshInstance3D:
	if node is MeshInstance3D: return node
	for child in node.get_children():
		var found := _first_mesh(child)
		if found != null: return found
	return null

func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()

func _write_text(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(text)
	file.close()

func _glb_fixture(with_uv: bool, external: bool = false) -> PackedByteArray:
	var positions := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var indices := PackedInt32Array()
	var faces := [[Vector3(-1, 0, 1), Vector3(1, 0, 1), Vector3(1, 2, 1), Vector3(-1, 2, 1), Vector3.BACK], [Vector3(1, 0, -1), Vector3(-1, 0, -1), Vector3(-1, 2, -1), Vector3(1, 2, -1), Vector3.FORWARD], [Vector3(1, 0, 1), Vector3(1, 0, -1), Vector3(1, 2, -1), Vector3(1, 2, 1), Vector3.RIGHT], [Vector3(-1, 0, -1), Vector3(-1, 0, 1), Vector3(-1, 2, 1), Vector3(-1, 2, -1), Vector3.LEFT], [Vector3(-1, 2, 1), Vector3(1, 2, 1), Vector3(1, 2, -1), Vector3(-1, 2, -1), Vector3.UP], [Vector3(-1, 0, -1), Vector3(1, 0, -1), Vector3(1, 0, 1), Vector3(-1, 0, 1), Vector3.DOWN]]
	for face in faces:
		var start := positions.size()
		for index in 4:
			positions.append(face[index])
			normals.append(face[4])
		uvs.append_array(PackedVector2Array([Vector2.ZERO, Vector2.RIGHT, Vector2.ONE, Vector2.DOWN]))
		indices.append_array(PackedInt32Array([start, start + 1, start + 2, start, start + 2, start + 3]))
	var binary := positions.to_byte_array()
	var normal_offset := binary.size()
	binary.append_array(normals.to_byte_array())
	var uv_offset := binary.size()
	binary.append_array(uvs.to_byte_array())
	var index_offset := binary.size()
	binary.append_array(indices.to_byte_array())
	var texture := Image.create(2, 2, false, Image.FORMAT_RGB8)
	texture.fill(Color(0.55, 0.55, 1.0))
	var png := texture.save_png_to_buffer()
	var image_offset := binary.size()
	binary.append_array(png)
	while binary.size() % 4 != 0: binary.append(0)
	var attributes := {"POSITION": 0, "NORMAL": 1}
	if with_uv: attributes["TEXCOORD_0"] = 2
	var material := {"pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.8}}
	if with_uv:
		material.pbrMetallicRoughness["baseColorTexture"] = {"index": 0}
		material.pbrMetallicRoughness["metallicRoughnessTexture"] = {"index": 0}
		material["normalTexture"] = {"index": 0}
	var image := {"bufferView": 4, "mimeType": "image/png"}
	if external: image = {"uri": "unhashed-external.png"}
	var descriptor := {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}], "buffers": [{"byteLength": binary.size()}], "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": normal_offset}, {"buffer": 0, "byteOffset": normal_offset, "byteLength": uv_offset - normal_offset}, {"buffer": 0, "byteOffset": uv_offset, "byteLength": index_offset - uv_offset}, {"buffer": 0, "byteOffset": index_offset, "byteLength": image_offset - index_offset}, {"buffer": 0, "byteOffset": image_offset, "byteLength": png.size()}], "accessors": [{"bufferView": 0, "componentType": 5126, "count": 24, "type": "VEC3", "min": [-1, 0, -1], "max": [1, 2, 1]}, {"bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3"}, {"bufferView": 2, "componentType": 5126, "count": 24, "type": "VEC2"}, {"bufferView": 3, "componentType": 5125, "count": 36, "type": "SCALAR"}], "meshes": [{"primitives": [{"attributes": attributes, "indices": 3, "material": 0}]}], "materials": [material]}
	if with_uv or external:
		descriptor["images"] = [image]
		descriptor["textures"] = [{"source": 0}]
	var json := JSON.stringify(descriptor).to_utf8_buffer()
	while json.size() % 4 != 0: json.append(32)
	var result := PackedByteArray()
	result.resize(20)
	result.encode_u32(0, 0x46546c67)
	result.encode_u32(4, 2)
	result.encode_u32(8, 28 + json.size() + binary.size())
	result.encode_u32(12, json.size())
	result.encode_u32(16, 0x4e4f534a)
	result.append_array(json)
	var chunk_header := PackedByteArray()
	chunk_header.resize(8)
	chunk_header.encode_u32(0, binary.size())
	chunk_header.encode_u32(4, 0x004e4942)
	result.append_array(chunk_header)
	result.append_array(binary)
	return result
