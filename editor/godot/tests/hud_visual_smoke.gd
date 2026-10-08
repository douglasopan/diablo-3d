extends SceneTree
## Render shared authored PNGs into isolated SubViewports. No game window.
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
	if OS.get_cmdline_user_args().has("--glass-only"):
		await _run_glass_only()
		return
	await _capture("hud-16x9", Vector2i(1280, 720), 70, 55.4)
	await _capture("hud-ultrawide", Vector2i(2560, 1080), 70, 55.4)
	await _capture("hud-compact", Vector2i(640, 480), 70, 55.4)
	await _capture("hud-empty-life-full-mana", Vector2i(1280, 720), 0, 100)
	await _capture("hud-native-panel-fullhd", Vector2i(1920, 1080), 70, 55.4)
	await _capture("hud-native-panel-multiplayer", Vector2i(1280, 720), 70, 55.4, true)
	await _capture("hud-native-panel-five-lines", Vector2i(640, 480), 70, 55.4, false, "Espada curta\nDano: 2–6\nDurabilidade: 17 / 24\nForça necessária: 10\nValor demonstrativo")
	await _capture("hud-hd-960-five-lines", Vector2i(960, 540), 50, 50, false, "Espada curta\nDano: 2–6\nDurabilidade: 17 / 24\nForça necessária: 10\nValor demonstrativo")
	await _capture("hud-hd-fullhd-0", Vector2i(1920, 1080), 0, 0)
	await _capture("hud-hd-fullhd-50", Vector2i(1920, 1080), 50, 50)
	await _capture("hud-hd-fullhd-100", Vector2i(1920, 1080), 100, 100)
	await _capture("hud-hd-13-hitboxes", Vector2i(1920, 1080), 50, 50, true, "Informações demonstrativas", true)
	await _capture("hud-hd-ultrawide-3440", Vector2i(3440, 1440), 50, 50)
	if captures.size() == 13:
		if captures[0].red_pixels <= captures[3].red_pixels * 2:
			failures.append("Render de vida não respondeu à mudança 70% → 0%.")
		if captures[0].blue_pixels >= captures[3].blue_pixels:
			failures.append("Render de mana não respondeu à mudança 55.4% → 100%.")
		for color: String in ["red_pixels", "blue_pixels"]:
			if not (captures[8][color] < captures[9][color] and captures[9][color] < captures[10][color]):
				failures.append("PNG/máscara não respondeu a 0/50/100: " + color)
		if captures[11].hitboxes.size() != 13 or captures[11].cyan_pixels < 100:
			failures.append("Fixture não mostra as 13 áreas originais em MP.")
	var file := FileAccess.open(OUTPUT.path_join("results.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"captures": captures, "failures": failures, "scope": "Shared real-PNG Godot candidate; Georgia preview font differs from native Font12; not a game capture or final artistic approval"}, "\t"))
	file.close()
	for failure: String in failures:
		push_error(failure)
	print("HUD_VISUAL_SMOKE: %d renders; %d failures" % [captures.size(), failures.size()])
	quit(0 if failures.is_empty() else 1)

func _run_glass_only() -> void:
	# Focused follow-up: retain the preceding 13-render receipt and repeat only
	# the 0/50 images affected by the neutral empty-glass presentation change.
	await _capture("hud-hd-fullhd-0", Vector2i(1920, 1080), 0, 0)
	await _capture("hud-hd-fullhd-50", Vector2i(1920, 1080), 50, 50)
	if captures.size() == 2:
		if captures[0].red_pixels != 0 or captures[0].blue_pixels != 0:
			failures.append("Vidro vazio contém cor de recurso.")
		if captures[1].red_pixels <= 0 or captures[1].blue_pixels <= 0:
			failures.append("Preenchimento 50% não ficou visível sobre o vidro.")
	var file := FileAccess.open(OUTPUT.path_join("results-glass-focused.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"captures": captures, "failures": failures, "formula": "shade=floor(min(R,G,B)*0.30); emptyRGB=(4+shade,5+shade,7+shade); alpha=circular edge", "scope": "Focused neutral glass 0/50 Godot preview; no runtime/artistic approval"}, "\t"))
	file.close()
	print("HUD_GLASS_FOCUSED: %d renders; %d failures" % [captures.size(), failures.size()])
	quit(0 if failures.is_empty() else 1)

func _capture(label: String, resolution: Vector2i, life: float, mana: float, multiplayer := false, info := "Informações nativas", hitboxes := false) -> void:
	var viewport := SubViewport.new()
	viewport.size = resolution
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var hud := SCENE.instantiate()
	viewport.add_child(hud)
	if hud.collect_layout().config.get_sections().size() != 13:
		failures.append("Prévia contém controles além dos 13 originais: " + label)
	if not hud.missing_assets.is_empty() or hud.textures.size() != 6:
		failures.append("Arte compartilhada incompleta: " + str(hud.missing_assets))
	hud.show_preview_tools = false
	hud.show_safe_frame = false
	hud.show_hitboxes = hitboxes
	hud.demo_life_percent = life
	hud.demo_mana_percent = mana
	hud.demo_multiplayer = multiplayer
	hud.demo_info_text = info
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
	var cyan := 0
	for y: int in range(0, image.get_height(), 2):
		for x: int in range(0, image.get_width(), 2):
			var color := image.get_pixel(x, y)
			if color.r > 0.25 and color.r > color.g * 2 and color.r > color.b * 2:
				red += 1
			if color.b > 0.25 and color.b > color.r * 2 and color.b > color.g * 1.3:
				blue += 1
			if color.g > 0.7 and color.b > 0.4 and color.r < 0.4:
				cyan += 1
	var visible_hitboxes := {}
	if hitboxes:
		for element_name: String in hud.ELEMENT_NAMES:
			var element: Control = hud.get_element(element_name)
			if not element.visible:
				failures.append("Área original escondida na fixture13: " + element_name)
			var actual := element.get_global_rect()
			visible_hitboxes[element_name] = [actual.position.x, actual.position.y, actual.size.x, actual.size.y]
	captures.append({"path": path, "width": resolution.x, "height": resolution.y, "life": life, "mana": mana, "multiplayer": multiplayer, "info_lines": info.split("\n").size(), "red_pixels": red, "blue_pixels": blue, "cyan_pixels": cyan, "hitboxes": visible_hitboxes, "skin_revision": hud.skin_metadata.revision, "font_runtime_equivalent": false})
	viewport.free()
