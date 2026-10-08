@tool
extends Control
## Editable layout for the native DevilutionX HUD. This scene never runs a game.
## Layout offsets use current logical pixels; the reference canvas is 640 x 480.

signal preview_area_clicked(element_name: String)

const EXPORT_PATH := "res://local/ui/layout.ini"
const FILL_SHADER := """
shader_type canvas_item;
uniform float fill_fraction : hint_range(0.0, 1.0) = 1.0;
void fragment() {
	vec4 tex = texture(TEXTURE, UV);
	float d = length(UV - vec2(0.5));
	float aa = max(fwidth(d), 0.0001);
	float circle = 1.0 - smoothstep(0.5 - aa, 0.5 + aa, d);
	float liquid = step(1.0 - fill_fraction, UV.y) * step(0.00001, fill_fraction);
	COLOR = tex * vec4(1.0, 1.0, 1.0, circle * liquid);
}
"""
const EMPTY_GLASS_SHADER := """
shader_type canvas_item;
void fragment() {
	vec3 tex = texture(TEXTURE, UV).rgb;
	float shade = floor(min(tex.r, min(tex.g, tex.b)) * 255.0 * 0.30);
	float d = length(UV - vec2(0.5));
	float aa = max(fwidth(d), 0.0001);
	float edge = 1.0 - smoothstep(0.5 - aa, 0.5 + aa, d);
	COLOR = vec4((vec3(4.0, 5.0, 7.0) + vec3(shade)) / 255.0, edge);
}
"""

const SKIN_ASSET_NAMES := ["chassis", "button", "orb-frame", "orb-red", "orb-blue", "inset"]
const ELEMENT_NAMES := [
	"HudHealthOrb", "HudManaOrb", "HudBelt", "HudSpell",
	"HudCharacter", "HudInventory", "HudSpellbook", "HudQuests", "HudMap", "HudMenu",
	"HudChat", "HudFriendly", "HudInfo",
]
# Removed at the user's request: preserve the original HUD's controls only.
# These old sections are discarded during export instead of resurfacing later.
const RETIRED_ELEMENTS := ["HudQuickSpell0", "HudQuickSpell1", "HudQuickSpell2", "HudQuickSpell3"]
const UTILITY_NAMES := [
	"HudCharacter", "HudInventory", "HudSpellbook", "HudQuests", "HudMap", "HudMenu",
	"HudChat", "HudFriendly",
]
const ANCHORS := {
	"top-left": Vector2(0, 0), "top-right": Vector2(1, 0),
	"center": Vector2(0.5, 0.5), "bottom-center": Vector2(0.5, 1),
	"bottom-right": Vector2(1, 1),
}
const BUTTON_TEXT := {
	"HudCharacter": "PERSONAGEM", "HudInventory": "INVENTÁRIO", "HudSpellbook": "MAGIAS",
	"HudQuests": "MISSÕES", "HudMap": "MAPA", "HudMenu": "MENU",
	"HudChat": "Chat", "HudFriendly": "Paz",
}

@export_group("Prévia — sem conexão com a partida")
@export var preview_logical_size := Vector2i(854, 480):
	set(value):
		preview_logical_size = Vector2i(maxi(640, value.x), 480)
		if is_inside_tree() and Engine.is_editor_hint():
			set_logical_size(preview_logical_size)
@export_range(0.0, 100.0, 0.1) var demo_life_percent := 70.0:
	set(value):
		demo_life_percent = clampf(value, 0, 100)
		_refresh_visuals()
@export_range(0.0, 100.0, 0.1) var demo_mana_percent := 55.4:
	set(value):
		demo_mana_percent = clampf(value, 0, 100)
		_refresh_visuals()
@export var demo_multiplayer := false:
	set(value):
		demo_multiplayer = value
		_refresh_visuals()
@export_multiline var demo_info_text := "Informações nativas":
	set(value):
		demo_info_text = value
		_refresh_visuals()
@export var show_safe_frame := true:
	set(value):
		show_safe_frame = value
		queue_redraw()
@export var show_hitboxes := false:
	set(value):
		show_hitboxes = value
		_refresh_visuals()
@export var show_preview_tools := true:
	set(value):
		show_preview_tools = value
		_refresh_visuals()
@export_group("Exportação explícita — somente layout")
@export_tool_button("Exportar layout HUD", "Save") var export_action: Callable = _export_button_pressed
@export_multiline var status := "Edite os Control em SafeFrame. Salve e exporte o layout. Cinto vazio e magia sem seleção são demonstrativos; fonte aproximada."

var _last_size := Vector2.ZERO
var skin_metadata: Dictionary = {}
var textures: Dictionary = {}
var missing_assets: Array[String] = []
var font: SystemFont
var _skin_regions: Dictionary = {}
var _frame_circle_center := Vector2.ZERO
var _frame_circle_radius := 0.0
var _liquid_overlap := 0.0

func _load_preview_skin() -> void:
	# Shared authored PNGs remain outside the Godot project. Never extract game
	# archives, install a profile, or alter artwork while loading this preview.
	var repo := ProjectSettings.globalize_path("res://").replace("\\", "/").trim_suffix("/").get_base_dir().get_base_dir()
	var folder := repo.path_join("assets/d3d-ui/hud/r1")
	var metadata_path := folder.path_join("skin.json")
	if not FileAccess.file_exists(metadata_path):
		missing_assets.append("skin.json")
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(metadata_path))
	if not parsed is Dictionary or parsed.get("format", "") != "d3d.hud-skin" or parsed.get("schemaVersion", 0) != 1:
		missing_assets.append("skin.json incompatível")
		return
	skin_metadata = parsed
	for asset_name: String in SKIN_ASSET_NAMES:
		var asset: Dictionary = skin_metadata.get("assets", {}).get(asset_name, {})
		var path := folder.path_join(str(asset.get("file", "")))
		if not FileAccess.file_exists(path) or FileAccess.get_sha256(path) != asset.get("sha256", ""):
			missing_assets.append(asset_name + " ausente/divergente")
			continue
		var image := Image.load_from_file(path)
		var pixels: Array = asset.get("pixels", [])
		var source: Array = asset.get("sourceRegion", [])
		if image == null or image.is_empty() or pixels.size() != 2 or source.size() != 4 or image.get_size() != Vector2i(pixels[0], pixels[1]):
			missing_assets.append(asset_name + " inválido")
			continue
		textures[asset_name] = ImageTexture.create_from_image(image)
		_skin_regions[asset_name] = Rect2(source[0], source[1], source[2], source[3])
	if skin_metadata.get("assets", {}).has("orb-frame"):
		var circle: Dictionary = skin_metadata.assets["orb-frame"].get("circle", {})
		var center: Array = circle.get("centerSource", [0, 0])
		_frame_circle_center = Vector2(center[0], center[1])
		_frame_circle_radius = float(circle.get("radiusSource", 0))
		_liquid_overlap = float(circle.get("liquidOverlapSource", 0))

func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	font = SystemFont.new()
	font.font_names = PackedStringArray(["Georgia", "Times New Roman"])
	font.multichannel_signed_distance_field = true
	font.msdf_pixel_range = 8
	font.msdf_size = 64
	_load_preview_skin()
	for element_name: String in ELEMENT_NAMES:
		var element := get_element(element_name)
		if element == null:
			continue
		element.draw.connect(_draw_element.bind(element))
		element.resized.connect(_refresh_visuals)
		element.gui_input.connect(_element_input.bind(element_name))
		if element_name in ["HudHealthOrb", "HudManaOrb"] and _skin_regions.has("orb-frame"):
			_make_orb_layers(element, element_name == "HudManaOrb")
	if Engine.is_editor_hint():
		set_logical_size(preview_logical_size)
	else:
		get_viewport().size_changed.connect(_fit_runtime)
		_fit_runtime()
		get_node("PreviewTools/Content/Life").value = demo_life_percent
		get_node("PreviewTools/Content/Mana").value = demo_mana_percent
		get_node("PreviewTools/Content/Life").value_changed.connect(func(value: float): demo_life_percent = value)
		get_node("PreviewTools/Content/Mana").value_changed.connect(func(value: float): demo_mana_percent = value)
		get_node("PreviewTools/Content/Export").pressed.connect(_export_button_pressed)
	_refresh_visuals()

func _process(_delta: float) -> void:
	if size != _last_size:
		_sync_safe_frame()
	# Native Control offsets remain the editable source, including changes made
	# with the 2D handles/inspector while this @tool script is running.
	if Engine.is_editor_hint():
		_refresh_visuals()

func _fit_runtime() -> void:
	var viewport_size := get_viewport_rect().size
	if viewport_size.x <= 0 or viewport_size.y <= 0:
		return
	var logical := Vector2i(maxi(640, floori(viewport_size.x * 480.0 / viewport_size.y)), 480)
	set_logical_size(logical)
	var factor := minf(viewport_size.x / size.x, viewport_size.y / size.y)
	scale = Vector2.ONE * factor
	position = ((viewport_size - size * factor) / 2.0).floor()

func set_logical_size(logical: Vector2i) -> void:
	size = Vector2(maxi(640, logical.x), 480)
	_sync_safe_frame()

func _sync_safe_frame() -> void:
	var frame := get_node_or_null("SafeFrame") as Control
	if frame == null:
		return
	_last_size = size
	var screen := Vector2i(floori(size.x), floori(size.y))
	var safe_width := mini(screen.x, floori(screen.y * 16.0 / 9.0))
	frame.position = Vector2((screen.x - safe_width) / 2, 0)
	frame.size = Vector2(safe_width, screen.y)
	# The connected native panel uses the same grouping at 4:3 and 16:9.
	var utilities := get_node("SafeFrame/Utilities") as Control
	utilities.offset_top = 0
	utilities.offset_bottom = 0
	var output := get_node_or_null("PreviewStatus") as Control
	if output:
		output.offset_right = size.x - 16
	_refresh_visuals()

func get_element(element_name: String) -> Control:
	if not element_name in ELEMENT_NAMES:
		return null
	var prefix := "SafeFrame/Utilities/" if element_name in UTILITY_NAMES else "SafeFrame/"
	return get_node_or_null(prefix + element_name) as Control

func get_panel_preview_rect() -> Rect2i:
	var frame := get_node("SafeFrame") as Control
	var bounds := Rect2i()
	var found := false
	for element_name: String in ELEMENT_NAMES:
		if element_name in ["HudHealthOrb", "HudManaOrb", "HudChat", "HudFriendly"]:
			continue
		var element := get_element(element_name)
		if element == null:
			continue
		var transform := frame.get_global_transform().affine_inverse() * element.get_global_transform()
		var rect := Rect2i(Vector2i((transform.origin + Vector2.ONE * 0.0001).floor()), Vector2i(element.size.round()))
		bounds = bounds.merge(rect) if found else rect
		found = true
	return Rect2i(Vector2i(frame.position) + bounds.position - Vector2i(10, 6), bounds.size + Vector2i(20, 6))

func _refresh_visuals() -> void:
	if not is_inside_tree():
		return
	queue_redraw()
	for element_name: String in ELEMENT_NAMES:
		var element := get_element(element_name)
		if element:
			if element_name in ["HudChat", "HudFriendly"]:
				element.visible = demo_multiplayer
			if element_name in ["HudHealthOrb", "HudManaOrb"] and _skin_regions.has("orb-frame"):
				_place_orb_layers(element, element_name == "HudManaOrb")
				var overlay := element.get_node_or_null("HitboxOverlay") as Control
				if overlay:
					overlay.queue_redraw()
			element.queue_redraw()
	var tools := get_node_or_null("PreviewTools") as Control
	if tools:
		tools.visible = show_preview_tools
	var life := get_node_or_null("PreviewTools/Content/LifeLabel") as Label
	var mana := get_node_or_null("PreviewTools/Content/ManaLabel") as Label
	if life:
		life.text = "Vida demonstrativa: %.1f%%" % demo_life_percent
	if mana:
		mana.text = "Mana demonstrativa: %.1f%%" % demo_mana_percent
	var output := get_node_or_null("PreviewStatus") as Label
	if output:
		output.visible = show_preview_tools
		output.text = status

func _element_input(event: InputEvent, element_name: String) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		status = "Área: %s. A ação nativa é executada no jogo; esta cena só demonstra o layout." % element_name
		preview_area_clicked.emit(element_name)
		_refresh_visuals()

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("161a1d"))
	if font == null:
		return
	var title := "PRÉVIA DO EDITOR • HUD CANDIDATO"
	_draw_text(self, title, Rect2(0, 28, size.x, 20), 12, Color("d2c7ab"))
	_draw_text(self, "PNGs gerados • valores demonstrativos • não é captura do jogo", Rect2(0, 52, size.x, 15), 9, Color("8e9598"))
	_draw_text(self, "Cinto vazio • magia sem seleção • fonte de prévia aproximada", Rect2(0, 70, size.x, 15), 9, Color("8e9598"))
	var frame := get_node_or_null("SafeFrame") as Control
	if show_safe_frame and frame:
		draw_rect(frame.get_rect().grow(-1), Color(0.66, 0.56, 0.36, 0.3), false, 1)
	if not missing_assets.is_empty():
		_draw_text(self, "ASSETS AUSENTES: " + ", ".join(missing_assets), Rect2(0, 95, size.x, 20), 10, Color("f0a36b"))
	_nine_slice(self, Rect2(get_panel_preview_rect()), "chassis")

func _draw_element(element: Control) -> void:
	var rect := Rect2(Vector2.ZERO, element.size)
	var element_name := String(element.name)
	match element_name:
		"HudHealthOrb", "HudManaOrb":
			pass # Neutral glass, liquid and frame are separate decorative layers.
		"HudBelt":
			for index: int in range(8):
				var left := element.size.x * index / 8.0
				var right := element.size.x * (index + 1) / 8.0
				var cell := Rect2(left, 0, right - left, element.size.y)
				_inset(element, cell)
				_draw_hotkey(element, str(index + 1), cell.grow(-2))
		"HudInfo":
			_inset(element, rect)
			var lines := demo_info_text.split("\n")
			var pitch := 11.2
			var first := (element.size.y - pitch * lines.size()) / 2
			for index: int in range(lines.size()):
				_draw_text(element, lines[index], Rect2(5, first + pitch * index, element.size.x - 10, pitch), 10)
		"HudSpell":
			_inset(element, rect)
		_:
			_nine_slice(element, rect, "button")
			var text_size := 9 if element_name in ["HudCharacter", "HudInventory"] else 10
			_draw_text(element, BUTTON_TEXT.get(element_name, ""), rect.grow(-4), text_size)
	if show_hitboxes:
		element.draw_rect(rect, Color(0.2, 0.95, 0.75, 0.9), false, 0.5)

func _inset(target: Control, rect: Rect2) -> void:
	_nine_slice(target, rect, "inset")

func _draw_hotkey(target: Control, text: String, rect: Rect2) -> void:
	if font == null:
		return
	# Match native hotkeys: bottom right inside the existing slot, with outline.
	var point_size := 12
	var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size).x
	var point := Vector2(rect.end.x - width, rect.end.y - font.get_descent(point_size))
	target.draw_string_outline(font, point, text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size, 1, Color("080a0b"))
	target.draw_string(font, point, text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size, Color("eee9dd"))

func _draw_text(target: Control, text: String, rect: Rect2, point_size: int, color := Color("e5d8b6")) -> void:
	if font == null:
		return
	var text_width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size).x
	var point := Vector2(rect.position.x + (rect.size.x - text_width) / 2, rect.position.y + (rect.size.y - font.get_height(point_size)) / 2 + font.get_ascent(point_size))
	target.draw_string_outline(font, point, text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size, 1, Color("11100d"))
	target.draw_string(font, point, text, HORIZONTAL_ALIGNMENT_LEFT, -1, point_size, color)

func _nine_slice(target: Control, destination: Rect2, asset_name: String) -> void:
	if not textures.has(asset_name):
		return
	var asset: Dictionary = skin_metadata.assets[asset_name]
	var src: Array = asset.nineSliceSource
	var dst: Array = asset.nineSliceDestination
	var source_margins := Vector4(src[0], src[1], src[2], src[3])
	var margins := Vector4(dst[0], dst[1], dst[2], dst[3])
	var source: Rect2 = _skin_regions[asset_name]
	var sx := [source.position.x, source.position.x + source_margins.x, source.end.x - source_margins.z, source.end.x]
	var sy := [source.position.y, source.position.y + source_margins.y, source.end.y - source_margins.w, source.end.y]
	var dx := [destination.position.x, destination.position.x + margins.x, destination.end.x - margins.z, destination.end.x]
	var dy := [destination.position.y, destination.position.y + margins.y, destination.end.y - margins.w, destination.end.y]
	for y: int in range(3):
		for x: int in range(3):
			target.draw_texture_rect_region(textures[asset_name], Rect2(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]), Rect2(sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]))

func _make_orb_layers(element: Control, mana: bool) -> void:
	if element.has_node("LiquidPNG"):
		return
	for child_name: String in ["EmptyGlassPNG", "LiquidPNG", "FramePNG"]:
		var child := TextureRect.new()
		child.name = child_name
		child.mouse_filter = Control.MOUSE_FILTER_IGNORE
		child.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		child.stretch_mode = TextureRect.STRETCH_SCALE
		child.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
		element.add_child(child)
		var asset_name := "orb-frame" if child_name == "FramePNG" else ("orb-blue" if mana and child_name == "LiquidPNG" else "orb-red")
		if textures.has(asset_name):
			var atlas := AtlasTexture.new()
			atlas.atlas = textures[asset_name]
			atlas.region = _skin_regions[asset_name]
			if child_name == "EmptyGlassPNG":
				# Match C++'s uniform centered crop (red 1137×1136 → 1136²).
				var source: Rect2 = atlas.region
				var side := minf(source.size.x, source.size.y)
				atlas.region = Rect2(source.position + ((source.size - Vector2.ONE * side) / 2).floor(), Vector2.ONE * side)
			atlas.filter_clip = true
			child.texture = atlas
		if child_name in ["EmptyGlassPNG", "LiquidPNG"]:
			var material := ShaderMaterial.new()
			var shader := Shader.new()
			shader.code = EMPTY_GLASS_SHADER if child_name == "EmptyGlassPNG" else FILL_SHADER
			material.shader = shader
			child.material = material
		else:
			child.flip_h = mana
	var overlay := Control.new()
	overlay.name = "HitboxOverlay"
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	element.add_child(overlay)
	overlay.draw.connect(_draw_orb_hitbox.bind(element, overlay))
	_place_orb_layers(element, mana)

func _draw_orb_hitbox(element: Control, overlay: Control) -> void:
	if show_hitboxes:
		overlay.draw_rect(Rect2(Vector2.ZERO, element.size).grow(-0.5), Color(0.2, 0.9, 0.75, 0.85), false, 1)

func orb_frame_rect(element: Control) -> Rect2:
	if not _skin_regions.has("orb-frame"):
		return Rect2()
	var region: Rect2 = _skin_regions["orb-frame"]
	var factor := minf(element.size.x / region.size.x, element.size.y / region.size.y)
	var fitted := region.size * factor
	return Rect2(Vector2((element.size.x - fitted.x) / 2, element.size.y - fitted.y), fitted)

func orb_circle(element: Control, mana: bool) -> Rect2:
	if not _skin_regions.has("orb-frame"):
		return Rect2()
	var region: Rect2 = _skin_regions["orb-frame"]
	var fitted := orb_frame_rect(element)
	var factor := fitted.size.x / region.size.x
	var center := (_frame_circle_center - region.position) * factor
	if mana:
		center.x = fitted.size.x - center.x
	center += fitted.position
	var radius := (_frame_circle_radius + _liquid_overlap) * factor
	return Rect2(center - Vector2.ONE * radius, Vector2.ONE * radius * 2)

func _place_orb_layers(element: Control, mana: bool) -> void:
	var frame := element.get_node_or_null("FramePNG") as TextureRect
	var liquid := element.get_node_or_null("LiquidPNG") as TextureRect
	var glass := element.get_node_or_null("EmptyGlassPNG") as TextureRect
	if frame == null or liquid == null:
		return
	var fitted := orb_frame_rect(element)
	frame.position = fitted.position
	frame.size = fitted.size
	var circle := orb_circle(element, mana)
	if glass:
		glass.position = circle.position
		glass.size = circle.size
	liquid.position = circle.position
	liquid.size = circle.size
	liquid.material.set_shader_parameter("fill_fraction", (demo_mana_percent if mana else demo_life_percent) / 100.0)

func collect_layout() -> Dictionary:
	var config := ConfigFile.new()
	var frame := get_node("SafeFrame") as Control
	for element_name: String in ELEMENT_NAMES:
		var element := get_element(element_name)
		if element == null:
			return {"ok": false, "error": "Control ausente: " + element_name}
		if element.rotation != 0 or element.scale != Vector2.ONE:
			return {"ok": false, "error": "Preserve escala 1 e rotação 0 em " + element_name}
		var anchor := ""
		for anchor_name: String in ANCHORS:
			var point: Vector2 = ANCHORS[anchor_name]
			if element.anchor_left == point.x and element.anchor_right == point.x and element.anchor_top == point.y and element.anchor_bottom == point.y:
				anchor = anchor_name
		if anchor.is_empty():
			return {"ok": false, "error": "Âncora não suportada em %s: use center, bottom-center, bottom-right, top-right ou top-left, sem esticar." % element_name}
		# Coordinates are read from the actual editable Control, not metadata.
		var transform := frame.get_global_transform().affine_inverse() * element.get_global_transform()
		var point: Vector2 = ANCHORS[anchor]
		var offset := transform.origin - frame.size * point
		var width := roundi(element.size.x)
		var height := roundi(element.size.y)
		var offset_x := roundi(offset.x)
		var offset_y := roundi(offset.y)
		if width < 1 or height < 1 or width > 4096 or height > 4096 or absi(offset_x) > 4096 or absi(offset_y) > 4096:
			return {"ok": false, "error": "Dimensões/offsets fora do contrato em " + element_name}
		config.set_value(element_name, "anchor", anchor)
		config.set_value(element_name, "offsetX", offset_x)
		config.set_value(element_name, "offsetY", offset_y)
		config.set_value(element_name, "width", width)
		config.set_value(element_name, "height", height)
	return {"ok": true, "config": config}

static func resolve_layout_rect(layout: Dictionary, screen: Vector2i) -> Rect2i:
	var origin := Vector2i.ZERO
	match str(layout.anchor):
		"center": origin = Vector2i(screen.x / 2, screen.y / 2)
		"bottom-center": origin = Vector2i(screen.x / 2, screen.y)
		"bottom-right": origin = screen
		"top-right": origin = Vector2i(screen.x, 0)
	var dimensions := Vector2i(mini(int(layout.width), screen.x), mini(int(layout.height), screen.y))
	var offset := Vector2i(int(layout.offsetX), int(layout.offsetY))
	return Rect2i(Vector2i(clampi(origin.x + offset.x, 0, screen.x - dimensions.x), clampi(origin.y + offset.y, 0, screen.y - dimensions.y)), dimensions)

func _export_button_pressed() -> void:
	var result := export_layout()
	status = "Exportado em local/ui/layout.ini. Aplicação no jogo é uma etapa explícita." if result.ok else "Exportação recusada: " + str(result.error)
	if result.ok and not result.menu_available:
		status += " Exporte também o menu antes de usar o aplicador."
	_refresh_visuals()
	if not result.ok:
		push_error(status)
	else:
		print(status)

func export_layout(output_path: String = EXPORT_PATH) -> Dictionary:
	# Export is always local; this tool never installs files into a game profile.
	var clean := output_path.simplify_path()
	if not clean.begins_with("res://local/") or not clean.ends_with("/layout.ini"):
		return {"ok": false, "error": "O destino precisa ser local ao editor, terminado em /layout.ini."}
	var report := collect_layout()
	if not report.ok:
		return report
	var config: ConfigFile = report.config
	var previous := ""
	if FileAccess.file_exists(clean):
		var bytes := FileAccess.get_file_as_bytes(clean)
		if bytes.has(0):
			return {"ok": false, "error": "Conteúdo INI contém NUL; arquivo preservado."}
		if bytes.size() > 32768:
			return {"ok": false, "error": "Arquivo existente excede 32 KiB; preservado."}
		previous = FileAccess.get_file_as_string(clean)
	else:
		# First HUD export includes the menu baseline required by the shared
		# applicator. The repository manifest is a read-only seed, never edited.
		var repository := ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir().get_base_dir()
		var baseline := repository.path_join("assets/d3d-ui/layout.ini")
		if FileAccess.file_exists(baseline):
			var bytes := FileAccess.get_file_as_bytes(baseline)
			if bytes.has(0):
				return {"ok": false, "error": "Manifesto padrão contém NUL; exportação recusada."}
			if bytes.size() > 32768:
				return {"ok": false, "error": "Manifesto padrão excede 32 KiB; exportação recusada."}
			previous = FileAccess.get_file_as_string(baseline)
	var parsed := _read_ini(previous)
	if not parsed.ok:
		return parsed
	var existing: ConfigFile = parsed.config
	if existing.has_section("Layout") and (str(existing.get_value("Layout", "format", "")) != "d3d.ui-layout" or str(existing.get_value("Layout", "schemaVersion", "")) != "1"):
		return {"ok": false, "error": "O formato/schema existente não é compatível; arquivo preservado."}
	existing.set_value("Layout", "format", "d3d.ui-layout")
	existing.set_value("Layout", "schemaVersion", 1)
	for element_name: String in RETIRED_ELEMENTS:
		if existing.has_section(element_name):
			existing.erase_section(element_name)
	for element_name: String in ELEMENT_NAMES:
		for key: String in ["anchor", "offsetX", "offsetY", "width", "height"]:
			existing.set_value(element_name, key, config.get_value(element_name, key))
	if existing.get_sections().size() > 32:
		return {"ok": false, "error": "O contrato permite no máximo 32 seções; arquivo preservado."}
	# ConfigFile.save() quotes strings. DevilutionX's Ini parser expects bare
	# values, so serialize only owned blocks and keep all other blocks verbatim.
	var text := str(parsed.preamble) + _serialize_section(existing, "Layout")
	for block: Dictionary in parsed.blocks:
		if block.section != "Layout" and not block.section in ELEMENT_NAMES and not block.section in RETIRED_ELEMENTS:
			text += str(block.text)
	for element_name: String in ELEMENT_NAMES:
		text += "\n" + _serialize_section(config, element_name)
	if text.to_utf8_buffer().size() > 32768:
		return {"ok": false, "error": "Resultado excede 32 KiB; arquivo preservado."}
	var absolute := ProjectSettings.globalize_path(clean)
	var directory_error := DirAccess.make_dir_recursive_absolute(absolute.get_base_dir())
	if directory_error != OK:
		return {"ok": false, "error": "Não foi possível criar a pasta: %s" % directory_error}
	var pending := FileAccess.open(absolute + ".pending", FileAccess.WRITE)
	if pending == null:
		return {"ok": false, "error": "Não foi possível gravar o arquivo temporário."}
	pending.store_string(text)
	pending.flush()
	var write_error := pending.get_error()
	pending.close()
	if write_error != OK:
		return {"ok": false, "error": "Falha ao gravar; arquivo anterior preservado."}
	if FileAccess.file_exists(absolute):
		var backup := absolute + ".previous"
		if FileAccess.file_exists(backup) and DirAccess.remove_absolute(backup) != OK:
			return {"ok": false, "error": "Não foi possível preparar o backup; arquivo preservado."}
		if DirAccess.rename_absolute(absolute, backup) != OK:
			return {"ok": false, "error": "Não foi possível preservar a exportação anterior."}
		if DirAccess.rename_absolute(absolute + ".pending", absolute) != OK:
			DirAccess.rename_absolute(backup, absolute)
			return {"ok": false, "error": "Falha na troca; restauração da versão anterior solicitada."}
	elif DirAccess.rename_absolute(absolute + ".pending", absolute) != OK:
		return {"ok": false, "error": "Não foi possível concluir a exportação."}
	return {"ok": true, "path": clean, "elements": ELEMENT_NAMES.size(), "menu_available": existing.has_section("MenuLogo") and existing.has_section("MenuList")}

static func _read_ini(source: String) -> Dictionary:
	var config := ConfigFile.new()
	var blocks: Array[Dictionary] = []
	var preamble := ""
	var section := ""
	for line: String in source.split("\n"):
		var stripped := line.strip_edges()
		if stripped.begins_with("["):
			if not stripped.ends_with("]"):
				return {"ok": false, "error": "Seção INI inválida; arquivo preservado."}
			section = stripped.substr(1, stripped.length() - 2)
			if section.is_empty() or section.length() > 64:
				return {"ok": false, "error": "Nome de seção INI inválido; arquivo preservado."}
			for block: Dictionary in blocks:
				if block.section == section:
					return {"ok": false, "error": "Seção duplicada: " + section}
			blocks.append({"section": section, "text": line + "\n"})
		elif section.is_empty():
			if not stripped.is_empty() and not stripped.begins_with(";") and not stripped.begins_with("#"):
				return {"ok": false, "error": "Conteúdo INI fora de seção; arquivo preservado."}
			preamble += line + "\n" if not source.is_empty() else ""
		else:
			blocks[-1].text += line + "\n"
		if section.is_empty() or stripped.is_empty() or stripped.begins_with(";") or stripped.begins_with("#") or stripped.begins_with("["):
			continue
		var equals := line.find("=")
		if equals <= 0:
			return {"ok": false, "error": "Chave INI inválida; arquivo preservado."}
		var key := line.substr(0, equals).strip_edges()
		var value := line.substr(equals + 1).strip_edges()
		if config.has_section_key(section, key):
			return {"ok": false, "error": "Chave duplicada: %s/%s" % [section, key]}
		config.set_value(section, key, int(value) if value.is_valid_int() else value)
	return {"ok": true, "config": config, "blocks": blocks, "preamble": preamble}

static func _serialize_section(config: ConfigFile, section: String) -> String:
	var text := "[%s]\n" % section
	for key: String in config.get_section_keys(section):
		text += "%s=%s\n" % [key, str(config.get_value(section, key))]
	return text
