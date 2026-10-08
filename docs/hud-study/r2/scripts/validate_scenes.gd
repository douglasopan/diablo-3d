extends SceneTree
## Headless node/layout/input validation only; never captures or renders.
var failures: Array[String] = []
var checks: int = 0
var scene_results: Array[Dictionary] = []

func _initialize() -> void:
	root.visible = false
	root.size = Vector2i(1920, 1080)
	call_deferred("_run")

func _check(condition: bool, description: String) -> void:
	checks += 1
	if not condition:
		failures.append(description)

func _collect(node: Node, tally: Dictionary) -> void:
	tally["nodes"] += 1
	if node is Control:
		tally["controls"] += 1
	if node is Label:
		tally["live_labels"] += 1
	if node is Button:
		tally["buttons"] += 1
		_check(not node.pressed.get_connections().is_empty(), "local input connected " + str(node.name))
	for child in node.get_children():
		_collect(child, tally)

func _grid(grid: GridContainer, row_count: int, cell_size: Vector2, gap: float, description: String, instance: Control) -> void:
	_check(grid.columns == 10, description + " 10 columns")
	_check(grid.get_child_count() == 10 * row_count, description + " exact cell count")
	var index := 0
	for cell in grid.get_children():
		_check(cell is Button, description + " real editable Button cell")
		var expected := Vector2((index % 10) * (cell_size.x + gap), (index / 10) * (cell_size.y + gap))
		_check(cell.position.is_equal_approx(expected), description + " row/column geometry " + str(index))
		_check(cell.size.is_equal_approx(cell_size), description + " cell dimensions " + str(index))
		index += 1
	var expected_extent := Vector2(10 * cell_size.x + 9 * gap, row_count * cell_size.y + (row_count - 1) * gap)
	_check(grid.size.is_equal_approx(expected_extent), description + " exact extent")
	var first_cell := grid.get_child(0) as Button
	first_cell.pressed.emit()
	var status := instance.get_node("PreviewStatus") as Label
	_check(status.text.contains(str(first_cell.name)), description + " input updates local status")

func _run() -> void:
	for scene_path in ["res://scenes/configuracoes.tscn", "res://scenes/inventario.tscn", "res://scenes/comercio.tscn"]:
		var scene := load(scene_path) as PackedScene
		_check(scene != null, "parse " + scene_path)
		if scene == null:
			continue
		var instance := scene.instantiate() as Control
		_check(instance != null, "Control root " + scene_path)
		if instance == null:
			continue
		root.add_child(instance)
		await process_frame
		await process_frame
		var tally := {"scene":scene_path, "nodes":0, "controls":0, "live_labels":0, "buttons":0}
		_collect(instance, tally)
		for navigation in instance.get_node("Navigation").get_children():
			_check(navigation.pressed.get_connections().size() == 1, "single scene navigation input " + str(navigation.name))
		if scene_path.ends_with("configuracoes.tscn"):
			var rows := instance.get_node("SettingsPanel/Rows") as VBoxContainer
			var footer := instance.get_node("SettingsPanel/Footer") as HBoxContainer
			_check(rows.get_child_count() == 18, "18 editable settings rows")
			_check(footer.get_child_count() == 3, "horizontal settings footer")
			_check(rows.position.y + rows.size.y < footer.position.y, "18 rows clear the footer")
			_check(rows.position.y + rows.size.y < instance.get_node("SettingsPanel/Description").position.y, "18 rows clear the description")
			for row in rows.get_children():
				_check(row.get_node("Name") is Label and row.get_node("Value") is Button, "live row controls " + str(row.name))
				_check(row.size.y >= 31 and row.size.y <= 32, "settings row fits available description budget " + str(row.name))
			var value_button := rows.get_child(0).get_node("Value") as Button
			value_button.pressed.emit()
			_check(instance.get_node("PreviewStatus").text.contains(value_button.text), "settings input remains local")
			tally["settings_rows"] = rows.get_child_count()
			tally["footer_buttons"] = footer.get_child_count()
			tally["settings_row_height"] = rows.get_child(0).size.y
			tally["settings_rows_bottom"] = rows.position.y + rows.size.y
			tally["footer_top"] = footer.position.y
		else:
			var panel := instance.get_node("Inventory") as NinePatchRect
			_check(panel != null and panel.texture != null, "inventory NinePatchRect uses imported kit")
			_check(panel.get_node("Equipment").get_child_count() == 7, "7 equipment slots")
			_grid(panel.get_node("Backpack") as GridContainer, 4, Vector2(54, 54), 2, "backpack", instance)
			_check(panel.get_node("ItemSpecimens").get_child_count() == 3, "3 local item specimens")
			tally["equipment_slots"] = 7
			tally["backpack_cells"] = 40
			if scene_path.ends_with("comercio.tscn"):
				_grid(instance.get_node("ShopPanel/ShopGrid") as GridContainer, 9, Vector2(64, 48), 4, "shop", instance)
				_check(instance.get_node("ShopPanel/ShopItems").get_child_count() == 3, "3 editable shop item specimens")
				var modal := instance.get_node("Confirmation") as Control
				_check(not modal.visible, "confirmation starts hidden")
				instance.get_node("ShopPanel/Actions/Buy").pressed.emit()
				_check(modal.visible, "buy opens local confirmation")
				_check(instance.get_node("Confirmation/Modal").texture != null, "modal kit imported")
				instance.get_node("Confirmation/Modal/Actions/CancelPreview").pressed.emit()
				_check(not modal.visible, "cancel closes local confirmation")
				instance.get_node("ShopPanel/Actions/Buy").pressed.emit()
				instance.get_node("Confirmation/Modal/Actions/ConfirmPreview").pressed.emit()
				_check(not modal.visible, "confirm closes local preview without transaction")
				tally["shop_cells"] = 90
		var navigation_button := instance.get_node("Navigation/Configuracoes") as Button
		var normal_style := navigation_button.get_theme_stylebox("normal") as StyleBoxTexture
		_check(normal_style != null and normal_style.texture is AtlasTexture, "button uses original PNG via AtlasTexture")
		if normal_style != null:
			_check((normal_style.texture as AtlasTexture).region == Rect2(17, 330, 1740, 245), "button region exact")
		scene_results.append(tally)
		instance.queue_free()
		await process_frame
	print(JSON.stringify({"kind":"isolated-hud-r2-headless-structure-layout-input", "checks":checks, "failures":failures, "scenes":scene_results, "rendered":false, "game_started":false, "game_files_written":false}))
	quit(0 if failures.is_empty() else 1)
