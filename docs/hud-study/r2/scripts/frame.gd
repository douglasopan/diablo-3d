@tool
extends NinePatchRect
## The imported kit is optional while the concept assets are being prepared.
## Margins stay editable in the Inspector; no game resource is touched.
@export_file("*.png") var kit_texture: String = "res://assets/kit-panel.png":
	set(value):
		kit_texture = value
		if is_inside_tree():
			_refresh_texture()
@export var slice_left: int = 64:
	set(value):
		slice_left = value
		patch_margin_left = value
@export var slice_top: int = 96:
	set(value):
		slice_top = value
		patch_margin_top = value
@export var slice_right: int = 64:
	set(value):
		slice_right = value
		patch_margin_right = value
@export var slice_bottom: int = 64:
	set(value):
		slice_bottom = value
		patch_margin_bottom = value

func _ready() -> void:
	patch_margin_left = slice_left
	patch_margin_top = slice_top
	patch_margin_right = slice_right
	patch_margin_bottom = slice_bottom
	_refresh_texture()

func _refresh_texture() -> void:
	if ResourceLoader.exists(kit_texture):
		texture = load(kit_texture) as Texture2D
	else:
		texture = null
	var fallback := get_node_or_null("PanelFill") as ColorRect
	if fallback != null:
		fallback.visible = texture == null
