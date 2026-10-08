@tool
extends Control
## Only local preview interactions. No INI, save, MPQ, C++ bridge or export.
@export var button_texture_path: String = "res://assets/kit-controls.png"
@export var button_atlas_region: Rect2 = Rect2(17, 330, 1740, 245)
@export var button_slice_margin: int = 24
@export var ui_font_size: int = 22
@export var settings_value_font_size: int = 18
const SCENES := {"Configuracoes": "res://scenes/configuracoes.tscn", "Inventario": "res://scenes/inventario.tscn", "Comercio": "res://scenes/comercio.tscn"}

func _ready() -> void:
	_apply_styles(self)
	if Engine.is_editor_hint():
		return
	for key in SCENES:
		var navigation := get_node_or_null("Navigation/" + key) as Button
		if navigation != null:
			navigation.pressed.connect(_navigate.bind(SCENES[key]))
	_connect_preview(self)

func _navigate(path: String) -> void:
	get_tree().change_scene_to_file(path)

func _connect_preview(node: Node) -> void:
	for child in node.get_children():
		if child is Button and child.get_parent().name != "Navigation":
			child.pressed.connect(_preview_pressed.bind(child))
		_connect_preview(child)

func _preview_pressed(button: Button) -> void:
	var status := get_node_or_null("PreviewStatus") as Label
	if status != null:
		status.text = "Prévia local: " + (button.text if not button.text.is_empty() else str(button.name)) + " · nenhuma alteração no jogo"
	var confirmation := get_node_or_null("Confirmation") as Control
	if confirmation != null:
		if button.name == "Buy":
			confirmation.show()
		elif button.name in ["ConfirmPreview", "CancelPreview"]:
			confirmation.hide()

func _apply_styles(node: Node) -> void:
	if node is Control:
		node.add_theme_color_override("font_color", Color("#e7dfce"))
		node.add_theme_color_override("font_hover_color", Color("#fff1ca"))
		node.add_theme_color_override("font_pressed_color", Color("#ffcf76"))
		if node is Button:
			node.add_theme_font_size_override("font_size", ui_font_size)
			if node.name == "Value" and node.get_parent().is_in_group("settings_row"):
				node.add_theme_font_size_override("font_size", settings_value_font_size)
			for state in ["normal", "hover", "pressed", "focus", "disabled"]:
				var style: StyleBox
				if not node.is_in_group("native_cell") and ResourceLoader.exists(button_texture_path):
					var textured := StyleBoxTexture.new()
					var button_atlas := AtlasTexture.new()
					button_atlas.atlas = load(button_texture_path) as Texture2D
					button_atlas.region = button_atlas_region
					textured.texture = button_atlas
					textured.texture_margin_left = button_slice_margin
					textured.texture_margin_top = button_slice_margin
					textured.texture_margin_right = button_slice_margin
					textured.texture_margin_bottom = button_slice_margin
					textured.content_margin_left = 12.0
					textured.content_margin_right = 12.0
					textured.content_margin_top = 2.0
					textured.content_margin_bottom = 2.0
					textured.modulate_color = Color("#ffc969") if state in ["hover", "pressed", "focus"] else Color.WHITE
					style = textured
				else:
					var flat := StyleBoxFlat.new()
					flat.bg_color = Color("#211c15") if state in ["hover", "pressed", "focus"] else Color("#111315")
					flat.set_border_width_all(1)
					flat.border_color = Color("#c49348") if state in ["hover", "pressed", "focus"] else Color("#666051")
					flat.content_margin_left = 12.0
					flat.content_margin_right = 12.0
					flat.content_margin_top = 2.0
					flat.content_margin_bottom = 2.0
					style = flat
				node.add_theme_stylebox_override(state, style)
	for child in node.get_children():
		_apply_styles(child)
