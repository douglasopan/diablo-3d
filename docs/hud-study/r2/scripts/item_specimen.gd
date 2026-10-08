@tool
extends Panel
## An editable geometric specimen, not a final native item icon.
@export var item_label: String = "ITEM":
	set(value):
		item_label = value
		if is_inside_tree():
			_update_label()
@export_range(1, 2) var native_width_cells: int = 1
@export_range(1, 3) var native_height_cells: int = 1
func _ready() -> void:
	_update_label()
func _update_label() -> void:
	var label := get_node_or_null("ItemLabel") as Label
	if label != null:
		label.text = item_label
