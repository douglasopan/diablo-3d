extends SceneTree

var checks := 0
var failures := 0

func check(value: bool, label: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error(label)
	else:
		print("PASS ", label)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var packed := load("res://ui/main_menu.tscn") as PackedScene
	check(packed != null, "Menu scene loads")
	if not packed:
		quit(1)
		return
	var scene := packed.instantiate() as Control
	root.add_child(scene)
	await process_frame
	var menu := scene.get_node("MenuList") as Control
	const ActionNames := ["SinglePlayer", "MultiPlayer", "Settings", "ProjectSupportCredits", "Exit"]
	const ActionLabels := ["Um jogador", "Multijogador", "Configurações", "Suporte e Créditos ao Projeto", "Sair"]
	check(menu.get_child_count() == ActionNames.size(), "Main menu previews five runtime actions")
	for index in mini(menu.get_child_count(), ActionNames.size()):
		var action := menu.get_child(index) as Button
		check(action != null, "Action is a button: " + ActionNames[index])
		if action:
			check(String(action.name) == ActionNames[index], "Action order: " + ActionNames[index])
			check(action.text == ActionLabels[index], "Action label: " + ActionNames[index])
	const Output := "res://local/tests/menu-layout.ini"
	var file := FileAccess.open(Output, FileAccess.WRITE)
	if not file:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(Output).get_base_dir())
		file = FileAccess.open(Output, FileAccess.WRITE)
	file.store_string("[HudBelt]\nanchor=bottom-center\noffsetX=-116\noffsetY=-43\nwidth=232\nheight=29\n")
	file.close()
	check(scene.export_layout(Output), "Default menu exports")
	var original := FileAccess.get_file_as_string(Output)
	check(original.contains("[HudBelt]\nanchor=bottom-center"), "Unrelated HUD sections preserved without quotes")
	check(original.contains("offsetY=-48\nwidth=510\nheight=258"), "Export matches C++ defaults")
	menu.position += Vector2(12, 6)
	check(scene.export_layout(Output), "Moved menu exports")
	check(FileAccess.get_file_as_string(Output).contains("offsetX=-243\noffsetY=-42"), "Moved visual coordinates reach manifest")
	menu.size.x = 200
	check(not scene.export_layout(Output), "Unsupported width rejected")
	check(FileAccess.get_file_as_string(Output).contains("offsetX=-243"), "Rejected export preserves previous bytes")
	menu.size.x = 510
	menu.position -= Vector2(12, 6)
	var project_action := menu.get_node("ProjectSupportCredits") as Button
	check(not project_action.clip_text, "Grouped action never clips its label")
	for label: String in ["Suporte e Créditos ao Projeto", "Project Support and Credits"]:
		project_action.text = label
		for width: int in [300, 510, 640]:
			menu.offset_left = -width / 2.0
			menu.offset_right = width / 2.0
			await process_frame
			await process_frame
			var font_size := project_action.get_theme_font_size("font_size")
			var text_width := project_action.get_theme_font("font").get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
			var content_width := project_action.size.x - project_action.get_theme_stylebox("normal").get_minimum_size().x
			check(menu.size.x == width, "Label keeps authored width " + str(width) + ": " + label)
			check(text_width <= content_width, "Whole label fits width " + str(width) + ": " + label)
			check(font_size >= 12 and font_size <= 25, "Label font remains readable at width " + str(width))
			check(scene.export_layout(Output), "Supported width exports: " + str(width))
			check(FileAccess.get_file_as_string(Output).contains("[HudBelt]\nanchor=bottom-center"), "Width changes preserve HUD sections")
	project_action.text = "Suporte e Créditos ao Projeto"
	menu.offset_left = -255
	menu.offset_right = 255
	await process_frame
	await process_frame
	for dimensions: String in ["640x480", "1280x720", "1920x1080", "2560x1080"]:
		scene.preview_size = dimensions
		await process_frame
		check(scene.export_layout(Output), "Export at " + dimensions)
		check(FileAccess.get_file_as_string(Output).contains("offsetX=-255\noffsetY=-48"), "Anchors remain stable at " + dimensions)
		for action: Control in menu.get_children():
			check(Rect2(Vector2.ZERO, menu.size).encloses(action.get_rect()), "Action stays inside list at " + dimensions + ": " + String(action.name))
			check(action.size.y >= 43, "Action retains minimum runtime row height at " + dimensions + ": " + String(action.name))
	scene.queue_free()
	print("Menu layout: ", checks, " checks, ", failures, " failures")
	quit(0 if failures == 0 else 1)
