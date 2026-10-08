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
	var menu := scene.get_node("MenuList") as Control
	menu.position += Vector2(12, 6)
	check(scene.export_layout(Output), "Moved menu exports")
	check(FileAccess.get_file_as_string(Output).contains("offsetX=-243\noffsetY=-42"), "Moved visual coordinates reach manifest")
	menu.size.x = 200
	check(not scene.export_layout(Output), "Unsupported width rejected")
	check(FileAccess.get_file_as_string(Output).contains("offsetX=-243"), "Rejected export preserves previous bytes")
	menu.size.x = 510
	menu.position -= Vector2(12, 6)
	for dimensions: String in ["640x480", "1280x720", "1920x1080", "2560x1080"]:
		scene.preview_size = dimensions
		await process_frame
		check(scene.export_layout(Output), "Export at " + dimensions)
		check(FileAccess.get_file_as_string(Output).contains("offsetX=-255\noffsetY=-48"), "Anchors remain stable at " + dimensions)
	scene.queue_free()
	print("Menu layout: ", checks, " checks, ", failures, " failures")
	quit(0 if failures == 0 else 1)
