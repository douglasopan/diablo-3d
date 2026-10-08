@tool
extends Control

@export_enum("640x480", "1280x720", "1920x1080", "2560x1080") var preview_size: String = "640x480":
	set(value):
		preview_size = value
		if is_inside_tree():
			_apply_size()

@export_tool_button("Exportar layout do menu", "Save") var export_action = export_layout
@export_multiline var status: String = "Arraste MenuLogo e MenuList na área 2D. Salve a cena e exporte."

func _ready() -> void:
	(get_node("MenuList") as Control).resized.connect(_fit_project_action_label)
	(get_node("MenuList/ProjectSupportCredits") as Button).minimum_size_changed.connect(_fit_project_action_label)
	_apply_size()

func _apply_size() -> void:
	var parts := preview_size.split("x")
	size = Vector2(float(parts[0]), float(parts[1]))
	_fit_project_action_label()

func _fit_project_action_label() -> void:
	var menu := get_node("MenuList") as Control
	var action := menu.get_node("ProjectSupportCredits") as Button
	var font := action.get_theme_font("font")
	var padding := 0.0
	for style_name: String in ["normal", "hover", "pressed", "disabled", "focus"]:
		padding = maxf(padding, action.get_theme_stylebox(style_name).get_minimum_size().x)
	var available_width := maxf(1.0, menu.size.x - padding - 12.0)
	var font_size := 25
	while font_size > 12 and font.get_string_size(action.text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x > available_width:
		font_size -= 1
	if action.get_theme_font_size("font_size") != font_size:
		action.add_theme_font_size_override("font_size", font_size)

func export_layout(output_path: String = "res://local/ui/layout.ini") -> bool:
	var logo := get_node("MenuLogo") as Control
	var menu := get_node("MenuList") as Control
	# The animated in-game logo is fixed-size in v1. Only its position is authored.
	if not logo or not menu or logo.size != Vector2(580, 154) or menu.size.x < 300 or menu.size.x > 640 or menu.size.y < 258 or menu.size.y > 384:
		status = "Recusado: logo 580x154; lista largura 300..640 e altura 258..384."
		push_error(status)
		return false
	for element: Control in [logo, menu]:
		if element.rotation != 0 or element.scale != Vector2.ONE or element.anchor_left != 0.5 or element.anchor_right != 0.5 or element.anchor_top != 0.5 or element.anchor_bottom != 0.5:
			status = "Recusado: preserve âncoras no centro, escala 1 e rotação 0. Edite os offsets."
			push_error(status)
			return false
		if not Rect2(Vector2.ZERO, size).encloses(element.get_rect()):
			status = "Recusado: o elemento precisa caber no tamanho de prévia."
			return false
	# Preserve unrelated HUD sections verbatim; never install by saving a scene.
	var preserved := ""
	if FileAccess.file_exists(output_path):
		var keep := false
		for line: String in FileAccess.get_file_as_string(output_path).split("\n"):
			if line.strip_edges().begins_with("["):
				keep = not line.strip_edges() in ["[Layout]", "[MenuLogo]", "[MenuList]"]
			if keep:
				preserved += line + "\n"
	var text := "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n"
	for element: Control in [logo, menu]:
		var offset := element.position - size / 2.0
		text += "\n[%s]\nanchor=center\noffsetX=%d\noffsetY=%d\nwidth=%d\nheight=%d\n" % [element.name, roundi(offset.x), roundi(offset.y), roundi(element.size.x), roundi(element.size.y)]
	text += "\n" + preserved
	var absolute := ProjectSettings.globalize_path(output_path)
	DirAccess.make_dir_recursive_absolute(absolute.get_base_dir())
	var file := FileAccess.open(absolute + ".pending", FileAccess.WRITE)
	if not file:
		status = "Não foi possível gravar a exportação."
		return false
	file.store_string(text)
	file.close()
	if FileAccess.file_exists(absolute):
		var backup := absolute + ".previous"
		if FileAccess.file_exists(backup):
			DirAccess.remove_absolute(backup)
		if DirAccess.rename_absolute(absolute, backup) != OK:
			return false
	if DirAccess.rename_absolute(absolute + ".pending", absolute) != OK:
		return false
	status = "Exportado. Use Aplicar-Menu-Godot.cmd e reabra o menu do jogo."
	print(status)
	return true
