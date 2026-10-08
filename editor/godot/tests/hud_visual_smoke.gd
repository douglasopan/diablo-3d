extends SceneTree
## Render only this synthetic HUD into isolated SubViewports. No game window.
## Run with the GL Compatibility renderer (headless uses a dummy renderer).

const SCENE := preload("res://ui/hud.tscn")
const OUTPUT := "res://local/hud-visual-tests"

var failures: Array[String] = []
var captures: Array[Dictionary] = []

func _initialize() -> void:
	root.visible = false
	call_deferred("_run")

func _run() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUTPUT))
	await _capture("hud-16x9", Vector2i(1280, 720), 70, 55.4)
	await _capture("hud-ultrawide", Vector2i(2560, 1080), 70, 55.4)
	await _capture("hud-compact", Vector2i(640, 480), 70, 55.4)
	await _capture("hud-empty-life-full-mana", Vector2i(1280, 720), 0, 100)
	if captures.size() == 4:
		if captures[0].red_pixels <= captures[3].red_pixels * 2:
			failures.append("Render de vida não respondeu à mudança 70% → 0%.")
		if captures[0].blue_pixels >= captures[3].blue_pixels:
			failures.append("Render de mana não respondeu à mudança 55.4% → 100%.")
	var file := FileAccess.open(OUTPUT.path_join("results.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"captures": captures, "failures": failures, "scope": "Synthetic Godot HUD preview, not a game capture or artistic approval"}, "\t"))
	file.close()
	for failure: String in failures:
		push_error(failure)
	print("HUD_VISUAL_SMOKE: %d renders; %d failures" % [captures.size(), failures.size()])
	quit(0 if failures.is_empty() else 1)

func _capture(label: String, resolution: Vector2i, life: float, mana: float) -> void:
	var viewport := SubViewport.new()
	viewport.size = resolution
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var hud := SCENE.instantiate()
	viewport.add_child(hud)
	if hud.collect_layout().config.get_sections().size() != 13:
		failures.append("Prévia contém controles além dos 13 originais: " + label)
	hud.demo_life_percent = life
	hud.demo_mana_percent = mana
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	if image == null or image.is_empty():
		failures.append("Renderer não retornou pixels: " + label)
		viewport.free()
		return
	var path := OUTPUT.path_join(label + ".png")
	if image.save_png(path) != OK:
		failures.append("Não foi possível salvar: " + label)
	var red := 0
	var blue := 0
	for y: int in range(0, image.get_height(), 2):
		for x: int in range(0, image.get_width(), 2):
			var color := image.get_pixel(x, y)
			if color.r > 0.25 and color.r > color.g * 2 and color.r > color.b * 2:
				red += 1
			if color.b > 0.25 and color.b > color.r * 2 and color.b > color.g * 1.3:
				blue += 1
	captures.append({"path": path, "width": resolution.x, "height": resolution.y, "life": life, "mana": mana, "red_pixels": red, "blue_pixels": blue})
	viewport.free()
