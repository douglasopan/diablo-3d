extends SceneTree
## Optional verification harness. Catalog and output are explicitly chosen locally.

const READER = preload("res://addons/model_review/d3d_reader.gd")
const SCENE = preload("res://review/model_inspector.tscn")
var failures: Array[String] = []
var catalog := ""
var output := ""
var count := 0

func _initialize() -> void:
	root.visible = false
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--catalog="): catalog = argument.trim_prefix("--catalog=")
		if argument.begins_with("--output="): output = argument.trim_prefix("--output=")
	call_deferred("_run")

func _run() -> void:
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(catalog)) if FileAccess.file_exists(catalog) else null
	if not data is Dictionary:
		push_error("Choose an existing --catalog= JSON.")
		quit(1)
		return
	for model in data.get("models", []):
		if "--render-only" in OS.get_cmdline_user_args(): break
		for entry in model.get("runtime", []):
			var path := str(entry.get("path", ""))
			if not path.is_absolute_path(): path = catalog.get_base_dir().path_join(path)
			if not FileAccess.file_exists(path): continue
			var result := READER.read_model(path, entry.get("origin", [0, 0, 0]))
			count += 1
			if not result.ok:
				failures.append(str(result.error))
				continue
			if result.sha256 != entry.get("sha256", result.sha256): failures.append("SHA mismatch: " + path.get_file())
			if result.triangles != entry.get("triangles", result.triangles): failures.append("Triangle count mismatch: " + path.get_file())
			if entry.get("bounds", {}).has("min") and entry.get("bounds", {}).has("max"):
				var low: Array = entry.bounds.min
				var high: Array = entry.bounds.max
				var expected := AABB(Vector3(float(low[0]), float(low[1]), float(low[2])) + result.origin, Vector3(float(high[0]) - float(low[0]), float(high[1]) - float(low[1]), float(high[2]) - float(low[2])))
				var actual: AABB = result.node.mesh.get_aabb()
				if not actual.position.is_equal_approx(expected.position) or not actual.size.is_equal_approx(expected.size): failures.append("Bounds/origin mismatch: " + path.get_file())
			result.node.free()
	if not output.is_empty():
		var viewport := SubViewport.new()
		viewport.size = Vector2i(1460, 940)
		viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		root.add_child(viewport)
		var inspector := SCENE.instantiate()
		viewport.add_child(inspector)
		inspector.set_deferred("size", Vector2(1460, 940))
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		var image := viewport.get_texture().get_image()
		if image == null or image.is_empty():
			failures.append("No rendered inspector pixels.")
		else:
			image.save_png(output)
		inspector.tabs.current_tab = 1
		await process_frame
		await RenderingServer.frame_post_draw
		var references := viewport.get_texture().get_image()
		if references != null and not references.is_empty(): references.save_png(output.get_basename() + "-references.png")
		var applied_count := 0
		var rejected_count := 0
		for model in data.get("models", []):
			if not model.get("runtime", []).is_empty(): applied_count += 1
			if inspector._rejected(model): rejected_count += 1
		inspector.status_filter.select(1)
		inspector._filter()
		if inspector.filtered.size() != applied_count: failures.append("Applied filter mismatch.")
		inspector.status_filter.select(3)
		inspector._filter()
		if inspector.filtered.size() != rejected_count: failures.append("Rejected filter mismatch.")
		for error in inspector.errors: failures.append(error)
		viewport.free()
	for failure in failures: push_error(failure)
	print("MODEL_REVIEW_VERIFY runtime_files=%d failures=%d output=%s" % [count, failures.size(), output])
	quit(0 if failures.is_empty() else 1)
