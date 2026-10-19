extends Control
## Standalone, read-only comparison. Does not connect to the editor or runtime catalog.

const MANIFEST = preload("res://addons/asset_lod_review/lod_manifest.gd")

var manifest_result: Dictionary = {}
var sides: Array[Dictionary] = []
var identity_label: Label
var compatibility_label: Label
var status_label: Label
var notes: TextEdit
var manifest_dialog: FileDialog
var note_dialog: FileDialog
var yaw := 0.65
var pitch := 0.28
var distance := 5.0
var target := Vector3.ZERO
var fit_target := Vector3.ZERO
var fit_distance := 5.0
var model_radius := 1.0
var master_bounds := AABB()
var master_reference_index := -1
var fov_degrees := 50.0
var errors: Array[String] = []

func _ready() -> void:
	_build_ui()
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--manifest="):
			load_manifest(argument.trim_prefix("--manifest="))
	_update_cameras()

func _build_ui() -> void:
	var background := ColorRect.new()
	background.color = Color(0.065, 0.074, 0.09)
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for edge in ["left", "top", "right", "bottom"]: margin.add_theme_constant_override("margin_" + edge, 14)
	add_child(margin)
	var layout := VBoxContainer.new()
	layout.add_theme_constant_override("separation", 9)
	margin.add_child(layout)
	var heading := HBoxContainer.new()
	layout.add_child(heading)
	var title := Label.new()
	title.text = "Master / LOD — revisão local"
	title.add_theme_font_size_override("font_size", 24)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	heading.add_child(title)
	_button(heading, "Abrir manifesto…", func(): manifest_dialog.popup_centered_ratio(0.75))
	_button(heading, "Reenquadrar", reset_camera)
	_button(heading, "Salvar nota…", func(): note_dialog.popup_centered_ratio(0.75))
	identity_label = _label(layout, "Abra um manifesto privado v2; nenhum asset é selecionado por data ou nome.")
	var approval := _label(layout, "APROVAÇÃO ARTÍSTICA PENDENTE • esta ferramenta não instala, promove nem modifica modelos.")
	approval.add_theme_color_override("font_color", Color(1.0, 0.76, 0.32))
	compatibility_label = _label(layout, "Aberturas e geometria adicionadas pelo jogo não são reproduzidas nesta prévia GLB.")
	var views := HBoxContainer.new()
	views.add_theme_constant_override("separation", 12)
	views.size_flags_vertical = Control.SIZE_EXPAND_FILL
	layout.add_child(views)
	for side_index in 2:
		sides.append(_build_side(views, side_index))
	_label(layout, "Câmera sempre sincronizada · arrastar: órbita · botão direito: deslocar · roda: aproximar · escala comum, sem deformação por eixo")
	status_label = _label(layout, "GLBs são verificados por SHA-256 antes de abrir. Materiais PBR permanecem no importador GLTF do Godot.")
	status_label.add_theme_color_override("font_color", Color(0.68, 0.79, 0.88))
	notes = TextEdit.new()
	notes.placeholder_text = "Nota local: silhueta, UV, normais, material, aberturas e vistas ainda pendentes…"
	notes.custom_minimum_size.y = 66
	layout.add_child(notes)
	manifest_dialog = FileDialog.new()
	manifest_dialog.access = FileDialog.ACCESS_FILESYSTEM
	manifest_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	manifest_dialog.filters = PackedStringArray(["*.json ; Manifesto privado v2"])
	manifest_dialog.file_selected.connect(load_manifest)
	add_child(manifest_dialog)
	note_dialog = FileDialog.new()
	note_dialog.access = FileDialog.ACCESS_FILESYSTEM
	note_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	note_dialog.filters = PackedStringArray(["*.json ; Nota de revisão local"])
	note_dialog.current_file = "asset-lod-review-note.json"
	note_dialog.file_selected.connect(save_note)
	add_child(note_dialog)

func _build_side(parent: Control, index: int) -> Dictionary:
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.size_flags_vertical = Control.SIZE_EXPAND_FILL
	parent.add_child(column)
	var selection := OptionButton.new()
	selection.item_selected.connect(func(selected: int): select_source(index, selected))
	selection.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_child(selection)
	var details := _label(column, "Nenhuma fonte carregada.")
	details.custom_minimum_size.y = 77
	var container := SubViewportContainer.new()
	container.stretch = true
	container.custom_minimum_size = Vector2(280, 300)
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	container.gui_input.connect(_viewport_input)
	column.add_child(container)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(640, 480)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.msaa_3d = Viewport.MSAA_4X
	container.add_child(viewport)
	var world := Node3D.new()
	viewport.add_child(world)
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color(0.12, 0.14, 0.17)
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color = Color(0.77, 0.82, 0.9)
	settings.ambient_light_energy = 0.65
	settings.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	environment.environment = settings
	world.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-43, -34, 0)
	light.light_energy = 1.2
	light.light_color = Color(1.0, 0.95, 0.88)
	world.add_child(light)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-28, 145, 0)
	fill.light_energy = 0.32
	fill.light_color = Color(0.76, 0.84, 1.0)
	world.add_child(fill)
	var frame := Node3D.new()
	world.add_child(frame)
	var camera := Camera3D.new()
	camera.fov = fov_degrees
	camera.near = 0.005
	camera.far = 10000
	camera.current = true
	world.add_child(camera)
	var hud := _label(column, "Erro na tela: não disponível.")
	hud.custom_minimum_size.y = 57
	return {"selection": selection, "details": details, "viewport": viewport, "frame": frame, "camera": camera, "hud": hud, "sourceIndex": -1, "loaded": {}}

func load_manifest(path: String) -> bool:
	errors.clear()
	var result := MANIFEST.read(path)
	if not result.ok:
		_show_errors(result.errors)
		return false
	manifest_result = result
	master_bounds = AABB()
	master_reference_index = -1
	identity_label.text = "%s / %s  •  revisão %s  •  manifesto SHA-256 %s…" % [result.manifest.assetId, result.manifest.variant, result.manifest.revision, result.sha256.substr(0, 16)]
	var openings: Dictionary = result.manifest.openings
	compatibility_label.text = "Aberturas: %s / %s · identidade declarada; a prévia não executa recortes do jogo." % [openings.pipelineId, openings.compatibility]
	var master_index := 0
	var finest_index := 0
	for side in sides:
		side.selection.clear()
		for source_index in result.sources.size():
			var source: Dictionary = result.sources[source_index]
			var role_label := "MASTER · apenas inspeção" if source.role == "master-geometry" else "LOD %d · desenhável declarado" % int(source.lodIndex)
			side.selection.add_item("%s · %s…" % [role_label, str(source.sha256).substr(0, 12)])
			if source.role == "master-geometry" and master_reference_index < 0:
				master_index = source_index
				master_reference_index = source_index
			if source.role == "drawable-lod" and int(source.lodIndex) == 0: finest_index = source_index
	var left_ok := select_source(0, master_index, false)
	var right_ok := select_source(1, finest_index, false)
	fit_camera()
	if left_ok and right_ok:
		status_label.text = "Hashes conferidos · GLB original preservado · erro 0 de LOD 0 refere-se à referência drawable, não comprova equivalência ao master."
	return left_ok and right_ok

func select_source(side_index: int, source_index: int, refit: bool = true) -> bool:
	if manifest_result.is_empty() or side_index < 0 or side_index >= sides.size() or source_index < 0 or source_index >= manifest_result.sources.size():
		return false
	var side: Dictionary = sides[side_index]
	for child in side.frame.get_children(): child.free()
	side.loaded = {}
	side.sourceIndex = source_index
	side.selection.select(source_index)
	var source: Dictionary = manifest_result.sources[source_index]
	var loaded := MANIFEST.load_glb(source)
	if not loaded.ok:
		side.details.text = "Fonte não carregada.\n" + "\n".join(loaded.errors)
		_show_errors(loaded.errors)
		return false
	var manifest: Dictionary = manifest_result.manifest
	var fit: Dictionary = manifest.get("fit", {})
	var anchor := _vector(manifest.anchor.position)
	var scale := float(fit.get("uniformScale", 1.0))
	var basis := Basis(Vector3.UP, float(fit.get("yawRadians", 0.0))).scaled(Vector3.ONE * scale)
	side.frame.transform = Transform3D(basis, _vector(fit.get("translation", [0, 0, 0])) - basis * anchor)
	side.frame.add_child(loaded.node)
	side.loaded = loaded
	if source_index == master_reference_index: master_bounds = MANIFEST.bounds(side.frame)
	var summary: Dictionary = loaded.summary
	var mode := "MASTER · somente geometria cinza; UV/material ausentes" if loaded.gray else ("MASTER · apenas inspeção, material importado" if loaded.inspectOnly else "LOD · materiais importados do GLB")
	side.details.text = "%s\n%s triângulos · UV %s · normais %s · %d materiais\nPBR: %s" % [mode, str(summary.triangles), "sim" if summary.uvPresent else "não", "sim" if summary.normalsPresent else "não", summary.materials, ", ".join(summary.textureChannels) if not summary.textureChannels.is_empty() else "fatores do material; sem mapas"]
	if refit: fit_camera()
	return true

func fit_camera() -> void:
	var box := master_bounds
	var initialized := box.size != Vector3.ZERO
	if not initialized:
		for side in sides:
			if side.loaded.is_empty(): continue
			var side_box: AABB = MANIFEST.bounds(side.frame)
			if side_box.size == Vector3.ZERO: continue
			box = box.merge(side_box) if initialized else side_box
			initialized = true
	if initialized:
		fit_target = box.get_center()
		model_radius = maxf(box.size.length() / 2.0, 0.01)
		var aspect := float(sides[0].viewport.size.x) / maxf(float(sides[0].viewport.size.y), 1.0)
		var half_vertical := deg_to_rad(fov_degrees) / 2.0
		var half_fov := minf(half_vertical, atan(tan(half_vertical) * aspect))
		fit_distance = model_radius / sin(half_fov) * 1.12
	reset_camera()

func reset_camera() -> void:
	yaw = 0.65
	pitch = 0.28
	target = fit_target
	distance = fit_distance
	_update_cameras()

func set_review_camera(new_yaw: float, new_pitch: float, distance_factor: float = 1.0) -> void:
	yaw = new_yaw
	pitch = clampf(new_pitch, -1.45, 1.45)
	distance = maxf(fit_distance * distance_factor, 0.01)
	target = fit_target
	_update_cameras()

func _viewport_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		if event.button_mask & MOUSE_BUTTON_MASK_LEFT:
			yaw -= event.relative.x * 0.008
			pitch = clampf(pitch + event.relative.y * 0.008, -1.45, 1.45)
		elif event.button_mask & MOUSE_BUTTON_MASK_RIGHT:
			var camera: Camera3D = sides[0].camera
			target += (-camera.global_basis.x * event.relative.x + camera.global_basis.y * event.relative.y) * distance * 0.0013
		_update_cameras()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP: distance = maxf(distance / 1.12, 0.01)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN: distance *= 1.12
		_update_cameras()

func _update_cameras() -> void:
	var direction := Vector3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch))
	for side in sides:
		var camera: Camera3D = side.camera
		camera.position = target + direction * distance
		camera.look_at(target, Vector3.UP)
		camera.fov = fov_degrees

func _process(_delta: float) -> void:
	for side in sides:
		if side.loaded.is_empty():
			side.hud.text = "Erro na tela: não disponível."
			continue
		var source: Dictionary = manifest_result.sources[side.sourceIndex]
		if source.role == "master-geometry":
			var material_note := "UV/material desta revisão em inspeção; equivalência artística pendente." if side.loaded.summary.uvPresent else "A ausência de UV não é recuperada por esta ferramenta."
			side.hud.text = "MASTER: nunca é candidato ao seletor de LOD.\n" + material_note
			continue
		var scale := float(manifest_result.manifest.get("fit", {}).get("uniformScale", 1.0))
		var height := float(side.viewport.size.y)
		# A nearest bounding-sphere depth is conservative for the shared orbit.
		var depth_to_center: float = side.camera.global_position.distance_to(fit_target)
		var verified := "declarado verificado" if source.errorVerified else "NÃO VERIFICADO: estimativa, não elegível por erro"
		if int(source.lodIndex) > 0 and source.get("openingEquivalenceVerified") != true:
			verified += " · aberturas NÃO VERIFICADAS"
		if depth_to_center - model_radius <= side.camera.near:
			side.hud.text = "Limite cruza near: exigir drawable mais fino; projeção não limitada.\nErro declarado %.5f unidades · %s · viewport %d × %d" % [float(source.geometricError), verified, side.viewport.size.x, side.viewport.size.y]
			continue
		var depth := depth_to_center - model_radius
		var pixels_per_unit := height / (2.0 * tan(deg_to_rad(fov_degrees) / 2.0) * depth)
		var pixels := float(source.geometricError) * scale * pixels_per_unit
		var diameter_pixels := 2.0 * model_radius * pixels_per_unit
		var size_limit := " · limite coarse %.1f px" % float(source.maxDiameterPixels) if source.has("maxDiameterPixels") and float(source.maxDiameterPixels) > 0 else ""
		var diameter_text := "Diâmetro conservador %.1f px%s" % [diameter_pixels, size_limit] if depth_to_center > model_radius else "Câmera dentro da esfera: diâmetro conservador não limitado"
		side.hud.text = "Erro %.5f unidades → %.3f px · %s\n%s · viewport %d × %d" % [float(source.geometricError), pixels, verified, diameter_text, side.viewport.size.x, side.viewport.size.y]

func review_note() -> Dictionary:
	if manifest_result.is_empty(): return {}
	var compared: Array[Dictionary] = []
	for side in sides:
		if side.loaded.is_empty(): continue
		var source: Dictionary = manifest_result.sources[side.sourceIndex]
		var item := {"role": source.role, "sha256": side.loaded.sha256, "summary": side.loaded.summary}
		for key in ["lodIndex", "geometricError", "errorVerified", "openingEquivalenceVerified", "maxDiameterPixels", "runtimeSha256"]:
			if source.has(key): item[key] = source[key]
		compared.append(item)
	var data: Dictionary = manifest_result.manifest
	return {"format": "town-asset-lod-review-note-v1", "assetId": data.assetId, "variant": data.variant, "revision": data.revision, "geometricReferenceSha256": data.geometricReferenceSha256, "manifestSha256": manifest_result.sha256, "approvalStatus": "pending", "scope": "Godot GLB comparison; no runtime openings or installation", "anchor": data.anchor, "openings": data.openings, "fit": data.get("fit", {}), "sources": compared, "camera": {"yawRadians": yaw, "pitchRadians": pitch, "distance": distance, "target": [target.x, target.y, target.z], "fovDegrees": fov_degrees}, "note": notes.text}

func save_note(path: String) -> bool:
	if manifest_result.is_empty():
		_show_errors(["Abra e confira um manifesto antes de salvar a nota."])
		return false
	if path.get_extension().to_lower() != "json":
		_show_errors(["A nota precisa ser um arquivo JSON separado."])
		return false
	if _same_path(path, str(manifest_result.path)):
		_show_errors(["A nota precisa de outro caminho; o manifesto é somente leitura."])
		return false
	for source in manifest_result.sources:
		if _same_path(path, str(source.resolvedPath)):
			_show_errors(["A nota não pode sobrescrever uma fonte GLB."])
			return false
		if source.has("runtimePath") and _same_path(path, MANIFEST.resolve_path(str(source.runtimePath), str(manifest_result.path).get_base_dir())):
			_show_errors(["A nota não pode sobrescrever um pacote runtime."])
			return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_show_errors(["Não foi possível salvar a nota local."])
		return false
	file.store_string(JSON.stringify(review_note(), "\t") + "\n")
	file.close()
	status_label.text = "Nota salva localmente · aprovação continua pendente · manifesto e fontes preservados."
	return true

func _show_errors(messages: Array) -> void:
	for message in messages: errors.append(str(message))
	status_label.text = "\n".join(messages)

func _button(parent: Control, title: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = title
	button.pressed.connect(callback)
	parent.add_child(button)
	return button

func _label(parent: Control, title: String) -> Label:
	var label := Label.new()
	label.text = title
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(label)
	return label

func _vector(value: Array) -> Vector3:
	return Vector3(float(value[0]), float(value[1]), float(value[2]))

func _same_path(first: String, second: String) -> bool:
	var left := ProjectSettings.globalize_path(first).simplify_path().replace("\\", "/")
	var right := ProjectSettings.globalize_path(second).simplify_path().replace("\\", "/")
	if OS.get_name() == "Windows": return left.to_lower() == right.to_lower()
	return left == right
