@tool
extends Control
## Editable layout for the native DevilutionX HUD. This scene never runs a game.
## Layout offsets use current logical pixels; the reference canvas is 640 x 480.

signal preview_area_clicked(element_name: String)

const EXPORT_PATH := "res://local/ui/layout.ini"
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
@export_group("Exportação explícita — somente layout")
@export_tool_button("Exportar layout HUD", "Save") var export_action: Callable = _export_button_pressed
@export_multiline var status := "Edite os Control em SafeFrame. Salve a cena e exporte o layout. Vida/mana e itens são demonstrativos."

var _last_size := Vector2.ZERO

func _ready() -> void:
	for element_name: String in ELEMENT_NAMES:
		var element := get_element(element_name)
		if element == null:
			continue
		element.draw.connect(_draw_element.bind(element))
		element.resized.connect(element.queue_redraw)
		element.gui_input.connect(_element_input.bind(element_name))
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
			element.queue_redraw()
	var life := get_node_or_null("PreviewTools/Content/LifeLabel") as Label
	var mana := get_node_or_null("PreviewTools/Content/ManaLabel") as Label
	if life:
		life.text = "Vida demonstrativa: %.1f%%" % demo_life_percent
	if mana:
		mana.text = "Mana demonstrativa: %.1f%%" % demo_mana_percent
	var output := get_node_or_null("PreviewStatus") as Label
	if output:
		output.text = status

func _element_input(event: InputEvent, element_name: String) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		status = "Área: %s. A ação nativa é executada no jogo; esta cena só demonstra o layout." % element_name
		preview_area_clicked.emit(element_name)
		_refresh_visuals()

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("101519"))
	for x: int in range(0, int(size.x), 40):
		draw_line(Vector2(x, 0), Vector2(x, size.y), Color(0.17, 0.20, 0.21, 0.22))
	for y: int in range(0, int(size.y), 40):
		draw_line(Vector2(0, y), Vector2(size.x, y), Color(0.17, 0.20, 0.21, 0.22))
	var font := ThemeDB.fallback_font
	var title := "PRÉVIA DO HUD • SEM SIMULAÇÃO DO JOGO"
	var subtitle := "Layout em pixels lógicos • controles e dados nativos permanecem no DevilutionX"
	draw_string(font, Vector2((size.x - font.get_string_size(title, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x) / 2, 220), title, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("b8aa8b"))
	draw_string(font, Vector2((size.x - font.get_string_size(subtitle, HORIZONTAL_ALIGNMENT_LEFT, -1, 9).x) / 2, 239), subtitle, HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("7e898c"))
	var frame := get_node_or_null("SafeFrame") as Control
	if show_safe_frame and frame:
		draw_rect(frame.get_rect().grow(-1), Color(0.66, 0.56, 0.36, 0.3), false, 1)
		draw_string(font, frame.position + Vector2(10, 18), "SafeFrame %d × %d" % [frame.size.x, frame.size.y], HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("877d65"))
	if frame:
		_draw_backplate(Rect2(get_panel_preview_rect()))

func _draw_backplate(rect: Rect2) -> void:
	# Geometric stone/metal approximation only, not an HD art asset or game capture.
	draw_rect(rect, Color("292c2b"))
	for row: int in range(3):
		var y := rect.position.y + row * 34
		draw_line(Vector2(rect.position.x + 3, y + 2), Vector2(rect.end.x - 3, y + 2), Color("343734"), 1)
		for column: int in range(12):
			var x := rect.position.x + column * 54 + (27 if row % 2 else 0)
			if x < rect.end.x - 3:
				draw_line(Vector2(x, y + 3), Vector2(x, minf(y + 33, rect.end.y - 3)), Color("1a1d1e"), 1)
	draw_rect(rect.grow(-1), Color("9a8055"), false, 1)
	draw_rect(rect.grow(-3), Color("14191b"), false, 2)

func _draw_element(element: Control) -> void:
	var rect := Rect2(Vector2.ZERO, element.size)
	match String(element.name):
		"HudHealthOrb": _draw_orb(element, demo_life_percent / 100.0, Color("b51e23"), "VIDA")
		"HudManaOrb": _draw_orb(element, demo_mana_percent / 100.0, Color("2257ce"), "MANA")
		"HudBelt": _draw_belt(element)
		"HudInfo":
			_panel(element, rect)
			var lines := demo_info_text.split("\n")
			var first_baseline := (element.size.y - lines.size() * 10) / 2.0 + 8
			for index: int in range(lines.size()):
				_text(element, lines[index], Vector2(element.size.x / 2, first_baseline + index * 10), 9, Color("d4c3a0"))
		_:
			_panel(element, rect, String(element.name) == "HudSpell")
			if String(element.name) == "HudSpell":
				_draw_spell_glyph(element, rect.grow(-7))
			else:
				_text(element, BUTTON_TEXT.get(String(element.name), ""), Vector2(element.size.x / 2, element.size.y / 2 + 3), 9)
	if show_hitboxes:
		element.draw_rect(rect.grow(-0.5), Color(0.2, 0.9, 0.75, 0.85), false, 1)

func _panel(target: Control, rect: Rect2, selected := false) -> void:
	target.draw_rect(rect, Color("0c1012"))
	target.draw_rect(rect.grow(-1), Color("aa8750") if selected else Color("756347"), false, 1)
	target.draw_rect(rect.grow(-3), Color("302e29"), false, 1)
	for point: Vector2 in [rect.position + Vector2(3, 3), Vector2(rect.end.x - 3, rect.position.y + 3), rect.end - Vector2(3, 3), Vector2(rect.position.x + 3, rect.end.y - 3)]:
		target.draw_circle(point, 1, Color("b79a65"))

func _draw_orb(target: Control, fraction: float, color: Color, caption: String) -> void:
	# A full 88 x 113 ornament column with a ~69 px globe, like the native
	# 112 x 144 column. These simple shapes show its footprint, not final art.
	var column_size := Vector2(88, 113)
	target.draw_set_transform(Vector2.ZERO, 0, target.size / column_size)
	var radius := 34.5
	var center := Vector2(column_size.x / 2, radius + 2)
	var left := PackedVector2Array([Vector2(4, 14), Vector2(12, 24), Vector2(14, 66), Vector2(28, 88), Vector2(43, 102), Vector2(22, 106), Vector2(5, 91)])
	var right := PackedVector2Array()
	for point: Vector2 in left:
		right.append(Vector2(column_size.x - point.x, point.y))
	target.draw_colored_polygon(left, Color("464b49"))
	target.draw_colored_polygon(right, Color("464b49"))
	target.draw_colored_polygon(PackedVector2Array([Vector2(3, 107), Vector2(27, 98), Vector2(44, 104), Vector2(61, 98), Vector2(85, 107), Vector2(85, 112), Vector2(3, 112)]), Color("53564f"))
	target.draw_circle(center, radius + 4, Color("24272a"))
	target.draw_circle(center, radius + 2, Color("82735b"))
	target.draw_circle(center, radius, Color("070c12"))
	var top := center.y + radius - 2 * radius * fraction
	for row: int in range(ceili(top), floori(center.y + radius) + 1):
		var half_width := sqrt(maxf(0, radius * radius - pow(row - center.y, 2)))
		var depth := clampf((row - top) / maxf(1, 2 * radius), 0, 1)
		target.draw_line(Vector2(center.x - half_width, row), Vector2(center.x + half_width, row), color.darkened(depth * 0.5), 1)
	if fraction > 0 and fraction < 1:
		var half_width := sqrt(maxf(0, radius * radius - pow(top - center.y, 2)))
		target.draw_line(Vector2(center.x - half_width, top), Vector2(center.x + half_width, top), color.lightened(0.35), 1)
	target.draw_arc(center, radius - 2, PI * 1.12, PI * 1.8, 20, Color(0.6, 0.65, 0.7, 0.38), 1.5, true)
	target.draw_circle(center + Vector2(radius * 0.34, -radius * 0.42), radius * 0.08, Color(1, 1, 1, 0.7))
	for index: int in range(12):
		var angle := TAU * index / 12.0
		target.draw_line(center + Vector2.from_angle(angle) * (radius + 2), center + Vector2.from_angle(angle) * (radius + 4), Color("a3906c"), 1)
	_text(target, "%s %.0f%%" % [caption, fraction * 100], Vector2(column_size.x / 2, column_size.y - 2), 8)
	target.draw_set_transform(Vector2.ZERO)

func _draw_belt(target: Control) -> void:
	var slot_width := target.size.x / 8.0
	for index: int in range(8):
		var rect := Rect2(Vector2(index * slot_width + 1, 1), Vector2(slot_width - 2, target.size.y - 2))
		_panel(target, rect)
		if index < 2:
			var bottle := Rect2(rect.position + Vector2(rect.size.x * 0.35, 7), Vector2(rect.size.x * 0.3, rect.size.y - 11))
			target.draw_rect(bottle, Color("a5a9a6"), false, 1)
			target.draw_rect(bottle.grow(-1), Color("aa2028"))
			target.draw_line(bottle.position + Vector2(1, -2), bottle.position + Vector2(bottle.size.x - 1, -2), Color("ceb881"), 2)
		_text(target, str(index + 1), Vector2(rect.get_center().x, -3), 7)

func _draw_spell_glyph(target: Control, rect: Rect2) -> void:
	# One prepared-spell placeholder, no extra action or hotkey button.
	var center := rect.get_center()
	var color := Color("d4c3a0")
	target.draw_rect(Rect2(center + Vector2(-5, -1), Vector2(10, 11)), color)
	for finger: int in range(4):
		var x := center.x - 4 + finger * 3
		target.draw_line(Vector2(x, center.y), Vector2(x - 1, center.y - 8 - (2 if finger == 1 else 0)), color, 2)
	target.draw_line(center + Vector2(-4, 5), center + Vector2(-11, -1), color, 3)

func _text(target: Control, text: String, center_baseline: Vector2, font_size: int, color := Color("d4c3a0")) -> void:
	var font := ThemeDB.fallback_font
	var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
	target.draw_string(font, center_baseline - Vector2(width / 2, 0), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)

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
