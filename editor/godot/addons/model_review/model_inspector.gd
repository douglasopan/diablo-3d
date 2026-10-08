extends Control
## Read-only inspector. Catalogs and external models remain on the user's computer.

const READER = preload("res://addons/model_review/d3d_reader.gd")
const LOCAL_CATALOG := "res://local/model-review/inventory.json"
const PUBLIC_CATALOG := "res://review/catalog.json"
const GOLD := Color("d9bc81")
const MUTED := Color("a8b09c")

var models: Array = []
var filtered: Array = []
var selected: Dictionary = {}
var search: LineEdit
var status_filter: OptionButton
var category_filter: OptionButton
var list: ItemList
var title_label: Label
var identity_label: Label
var info: RichTextLabel
var list_count: Label
var tabs: TabContainer
var reference_area: VBoxContainer
var file_area: RichTextLabel
var group_select: OptionButton
var material_select: OptionButton
var scale_select: OptionButton
var source_caption: Label
var runtime_caption: Label
var source_view: SubViewport
var runtime_view: SubViewport
var source_world: Node3D
var runtime_world: Node3D
var source_camera: Camera3D
var runtime_camera: Camera3D
var source_model: Node3D
var runtime_model: Node3D
var source_bounds := AABB()
var runtime_bounds := AABB()
var yaw := PI / 4.0
var pitch := 0.3
var distance := 10.0
var pan := Vector3.ZERO
var focus_height := 2.5
var loaded := false
var ready_for_capture := false
var errors: Array[String] = []
var catalog_path := ""
var catalog_dialog: FileDialog
var model_dialog: FileDialog
var notes_dialog: FileDialog
var note_status: OptionButton
var note_comment: TextEdit
var local_models: Dictionary = {}
var thumbnail_cache: Dictionary = {}
var thumbnail_jobs: Array[int] = []
var thumbnail_elapsed := 0.0
var grids: Array[Node3D] = []

func _ready() -> void:
	get_window().title = "Diablo 3D — Inspetor de modelos"
	get_window().size = Vector2i(1460, 940)
	get_window().min_size = Vector2i(1080, 720)
	_build_ui()
	catalog_path = LOCAL_CATALOG if FileAccess.file_exists(LOCAL_CATALOG) else PUBLIC_CATALOG
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--catalog="): catalog_path = argument.trim_prefix("--catalog=")
	_load_inventory(catalog_path)
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--model="):
			var needle := argument.trim_prefix("--model=").to_lower()
			for model in models:
				if needle in str(model.get("name", "")).to_lower() or needle in str(model.get("sha256", "")):
					_select_model(model)
					break
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--capture="): call_deferred("_capture", argument.trim_prefix("--capture="))

func _panel_style(background: Color, border := Color("343a2d")) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(6)
	style.content_margin_left = 12
	style.content_margin_right = 12
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	return style

func _build_ui() -> void:
	theme = Theme.new()
	theme.default_font_size = 14
	theme.set_color("font_color", "Label", Color("e8ebde"))
	theme.set_color("font_color", "Button", Color("e8ebde"))
	theme.set_stylebox("normal", "Button", _panel_style(Color("30352a")))
	theme.set_stylebox("hover", "Button", _panel_style(Color("414733"), GOLD))
	theme.set_stylebox("panel", "ItemList", _panel_style(Color("20241d")))
	theme.set_stylebox("selected", "ItemList", _panel_style(Color("4c4a32"), GOLD))
	var background := ColorRect.new()
	background.color = Color("171b15")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 18)
	add_child(margin)
	var root_box := VBoxContainer.new()
	root_box.add_theme_constant_override("separation", 14)
	margin.add_child(root_box)
	var header := HBoxContainer.new()
	root_box.add_child(header)
	var brand := _label("DIABLO 3D  /  MODELOS DE TRISTRAM", 23, GOLD)
	brand.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(brand)
	header.add_child(_label("ARQUIVOS LOCAIS  •  SOMENTE LEITURA", 11, MUTED))
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.split_offset = 300
	root_box.add_child(split)
	var sidebar := VBoxContainer.new()
	sidebar.custom_minimum_size.x = 300
	sidebar.add_theme_constant_override("separation", 10)
	split.add_child(sidebar)
	sidebar.add_child(_label("Biblioteca de arquivos GLB", 17, GOLD))
	_button(sidebar, "Abrir catálogo local…", func(): catalog_dialog.popup_centered_ratio(0.7))
	search = LineEdit.new()
	search.placeholder_text = "Buscar nome, revisão ou hash…"
	search.text_changed.connect(func(_value): _filter())
	sidebar.add_child(search)
	status_filter = OptionButton.new()
	for entry in ["Todos os modelos", "Aplicados no perfil", "Gerados, não aplicados", "Rejeitados / descartados"]:
		status_filter.add_item(entry)
	status_filter.item_selected.connect(func(_index): _filter())
	sidebar.add_child(status_filter)
	category_filter = OptionButton.new()
	for entry in ["Todas as famílias", "Arquitetura", "Vegetação", "Objetos e rochas", "Personagens e animais", "Outros"]:
		category_filter.add_item(entry)
	category_filter.item_selected.connect(func(_index): _filter())
	sidebar.add_child(category_filter)
	list_count = _label("Carregando inventário…", 11, MUTED)
	sidebar.add_child(list_count)
	list = ItemList.new()
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list.allow_reselect = true
	list.fixed_icon_size = Vector2i(55, 55)
	list.icon_mode = ItemList.ICON_MODE_LEFT
	list.add_theme_constant_override("v_separation", 10)
	list.item_selected.connect(func(index): _select_model(filtered[index]))
	list.get_v_scroll_bar().value_changed.connect(func(_value): _queue_thumbnails())
	sidebar.add_child(list)
	sidebar.add_child(_label("Cópias com o mesmo SHA ficam reunidas.\nSelecionar aqui não altera o jogo.", 11, MUTED))
	var body := VBoxContainer.new()
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 10)
	split.add_child(body)
	title_label = _label("Modelo", 25, Color("edf0e2"))
	body.add_child(title_label)
	identity_label = _label("", 12, GOLD)
	body.add_child(identity_label)
	tabs = TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(tabs)
	var three_d := VBoxContainer.new()
	three_d.name = "Comparação 3D"
	three_d.add_theme_constant_override("separation", 9)
	tabs.add_child(three_d)
	var toolbar := HFlowContainer.new()
	three_d.add_child(toolbar)
	_button(toolbar, "Reenquadrar", _reset_camera)
	_button(toolbar, "Localizar GLB…", func(): model_dialog.popup_centered_ratio(0.7))
	scale_select = OptionButton.new()
	scale_select.add_item("Mesma altura · proporções intactas")
	scale_select.add_item("Unidades reais · sem ajuste")
	scale_select.item_selected.connect(func(_index): _apply_display_transform())
	toolbar.add_child(scale_select)
	material_select = OptionButton.new()
	material_select.add_item("Cor sem iluminação")
	material_select.add_item("Luz de inspeção aproximada")
	material_select.add_item("GLB PBR original / D3D RGB")
	material_select.item_selected.connect(func(_index): _set_materials())
	toolbar.add_child(material_select)
	group_select = OptionButton.new()
	group_select.item_selected.connect(func(_index): _load_runtime())
	toolbar.add_child(group_select)
	var previews := HBoxContainer.new()
	previews.size_flags_vertical = Control.SIZE_EXPAND_FILL
	previews.add_theme_constant_override("separation", 10)
	three_d.add_child(previews)
	var source := _make_view(previews, "GLB ORIGINAL", "Arquivo gerado • sem deformação")
	source_view = source.viewport
	source_world = source.world
	source_camera = source.camera
	source_caption = source.caption
	var runtime := _make_view(previews, "MODELO INSTALADO", "Albedo do pacote • sem mapas PBR ou suavização original")
	runtime_view = runtime.viewport
	runtime_world = runtime.world
	runtime_camera = runtime.camera
	runtime_caption = runtime.caption
	three_d.add_child(_label("Arraste: girar   •   Roda: aproximar   •   Botão direito/meio: deslocar   •   Câmeras sincronizadas", 11, MUTED))
	var limit := _label("Prévia dos arquivos. A paleta, luz e recortes da partida são aplicados depois. A grade é apenas uma referência de chão.", 11, Color("cfbc8c"))
	limit.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	three_d.add_child(limit)
	info = RichTextLabel.new()
	info.bbcode_enabled = true
	info.custom_minimum_size.y = 160
	info.size_flags_vertical = Control.SIZE_SHRINK_END
	three_d.add_child(info)
	var reference_scroll := ScrollContainer.new()
	reference_scroll.name = "Referências e imagens"
	tabs.add_child(reference_scroll)
	reference_area = VBoxContainer.new()
	reference_area.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	reference_area.add_theme_constant_override("separation", 14)
	reference_scroll.add_child(reference_area)
	file_area = RichTextLabel.new()
	file_area.name = "Identidade e limites"
	file_area.bbcode_enabled = true
	tabs.add_child(file_area)
	var contribution := VBoxContainer.new()
	contribution.name = "Minha revisão"
	contribution.add_theme_constant_override("separation", 14)
	tabs.add_child(contribution)
	var instructions := _label("Sua avaliação fica num JSON que você escolhe onde salvar. Compartilhe a nota ou proponha uma contribuição seguindo as regras do projeto. O inspetor não publica arquivos nem altera a seleção do jogo.", 15, MUTED)
	instructions.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	contribution.add_child(instructions)
	note_status = OptionButton.new()
	for value in ["Em revisão", "Aprovado visualmente", "Requer correção", "Rejeitado"]: note_status.add_item(value)
	contribution.add_child(note_status)
	note_comment = TextEdit.new()
	note_comment.placeholder_text = "Descreva o que foi conferido, o problema e o ângulo/estado em que aparece…"
	note_comment.size_flags_vertical = Control.SIZE_EXPAND_FILL
	contribution.add_child(note_comment)
	_button(contribution, "Salvar minha revisão…", func():
		if not selected.is_empty():
			notes_dialog.current_file = "review-" + str(selected.get("sha256", "model")).left(12) + ".json"
			notes_dialog.popup_centered_ratio(0.7)
	)
	catalog_dialog = _file_dialog("Abrir catálogo JSON", FileDialog.FILE_MODE_OPEN_FILE, PackedStringArray(["*.json ; Catálogo de modelos"]))
	catalog_dialog.file_selected.connect(_load_inventory)
	model_dialog = _file_dialog("Localizar GLB pelo SHA do catálogo", FileDialog.FILE_MODE_OPEN_FILE, PackedStringArray(["*.glb ; Modelo GLB"]))
	model_dialog.file_selected.connect(_bind_local_glb)
	notes_dialog = _file_dialog("Salvar revisão local", FileDialog.FILE_MODE_SAVE_FILE, PackedStringArray(["*.json ; Nota de revisão"]))
	notes_dialog.file_selected.connect(_save_note)

func _file_dialog(title: String, mode: FileDialog.FileMode, filters: PackedStringArray) -> FileDialog:
	var dialog := FileDialog.new()
	dialog.title = title
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.file_mode = mode
	dialog.filters = filters
	add_child(dialog)
	return dialog

func _resolve_path(value: Variant) -> String:
	var path := str(value)
	if path.is_empty(): return ""
	if path.is_absolute_path() or path.begins_with("res://") or path.begins_with("user://"): return path
	return catalog_path.get_base_dir().path_join(path).simplify_path()

func _bind_local_glb(path: String) -> void:
	if selected.is_empty(): return
	var expected := str(selected.get("sha256", selected.get("glb", {}).get("sha256", "")))
	var actual := FileAccess.get_sha256(path)
	if not expected.is_empty() and actual != expected:
		info.text = "[color=#e3a791]O SHA-256 deste arquivo diverge do modelo selecionado. A associação foi recusada para preservar a identidade.[/color]\nEsperado: " + expected + "\nEncontrado: " + actual
		return
	local_models[str(selected.get("id", actual))] = path
	_select_model(selected)

func _save_note(path: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		info.text = "Não foi possível salvar a revisão no local escolhido."
		return
	file.store_string(JSON.stringify({"schemaVersion": 1, "id": selected.get("id", ""), "sha256": selected.get("sha256", ""), "revision": selected.get("revision", ""), "status": ["in-review", "visually-approved", "needs-correction", "rejected"][note_status.selected], "comment": note_comment.text, "scope": "Godot file preview; not an in-game capture or automatic project acceptance", "createdUtc": Time.get_datetime_string_from_system(true)}, "\t"))
	file.close()
	identity_label.text = "Revisão salva localmente: " + path.get_file()

func _label(text: String, size := 14, color := Color.WHITE) -> Label:
	var result := Label.new()
	result.text = text
	result.add_theme_font_size_override("font_size", size)
	result.add_theme_color_override("font_color", color)
	return result

func _button(parent: Control, text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.pressed.connect(action)
	parent.add_child(button)
	return button

func _make_view(parent: Control, title: String, subtitle: String) -> Dictionary:
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	parent.add_child(box)
	box.add_child(_label(title, 12, GOLD))
	box.add_child(_label(subtitle, 10, MUTED))
	var container := SubViewportContainer.new()
	container.stretch = true
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	container.custom_minimum_size = Vector2(200, 280)
	container.gui_input.connect(_preview_input)
	box.add_child(container)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(600, 460)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(viewport)
	var world := Node3D.new()
	viewport.add_child(world)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color("242c20")
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color("d2dbbe")
	environment.environment.ambient_light_energy = 0.7
	world.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -25, 0)
	light.light_energy = 1.0
	world.add_child(light)
	var grid := _make_grid()
	world.add_child(grid)
	grids.append(grid)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.near = 0.001
	camera.far = 10000
	world.add_child(camera)
	camera.make_current()
	var caption := _label("Carregando…", 10, MUTED)
	caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(caption)
	return {"viewport": viewport, "world": world, "camera": camera, "caption": caption}

func _make_grid() -> Node3D:
	var grid := MeshInstance3D.new()
	grid.name = "ReferenceGrid_NotModelGeometry"
	var mesh := ImmediateMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color("37432e")
	mesh.surface_begin(Mesh.PRIMITIVE_LINES, material)
	for index in range(-10, 11):
		mesh.surface_add_vertex(Vector3(index, -0.015, -10))
		mesh.surface_add_vertex(Vector3(index, -0.015, 10))
		mesh.surface_add_vertex(Vector3(-10, -0.015, index))
		mesh.surface_add_vertex(Vector3(10, -0.015, index))
	mesh.surface_end()
	grid.mesh = mesh
	return grid

func _load_inventory(path: String) -> void:
	catalog_path = path
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	if not parsed is Dictionary:
		title_label.text = "Abra um catálogo para começar"
		info.text = "Use ‘Abrir catálogo local…’ para selecionar um JSON. Modelos e referências só serão lidos no seu computador. Consulte o README para o formato."
		list_count.text = "Nenhum catálogo aberto"
		return
	models = parsed.get("models", [])
	_filter()
	var first: Dictionary = models[0] if not models.is_empty() else {}
	for model in models:
		if "catedral" in str(model.get("name", "")).to_lower() or "cathedral" in str(model.get("name", "")).to_lower():
			first = model
			break
	if not first.is_empty():
		_select_model(first)

func _category(model: Dictionary) -> int:
	var value := str(model.get("category", "")).to_lower()
	if "arch" in value or "building" in value or "arquitet" in value: return 1
	if "veg" in value or "tree" in value or "plant" in value: return 2
	if "prop" in value or "rock" in value or "object" in value or "terrain" in value: return 3
	if "char" in value or "actor" in value or "animal" in value or "person" in value or "hero" in value: return 4
	return 5

func _rejected(model: Dictionary) -> bool:
	var state := str(model.get("status", "")).to_lower()
	return "reject" in state or "rejeit" in state or "discard" in state

func _filter() -> void:
	list.clear()
	filtered.clear()
	thumbnail_jobs.clear()
	var term := search.text.to_lower().strip_edges()
	for model in models:
		var applied: bool = not model.get("runtime", []).is_empty()
		if status_filter.selected == 1 and not applied: continue
		if status_filter.selected == 2 and (applied or _rejected(model)): continue
		if status_filter.selected == 3 and not _rejected(model): continue
		if category_filter.selected != 0 and category_filter.selected != _category(model): continue
		if not term.is_empty() and term not in str(model).to_lower(): continue
		filtered.append(model)
		var state := "APLICADO" if applied else ("REJEITADO" if _rejected(model) else "NÃO APLICADO")
		var revision := str(model.get("revision", ""))
		list.add_item(("✓  " if applied else ("×  " if _rejected(model) else "·  ")) + str(model.get("name", "Sem nome")))
		var index := list.item_count - 1
		list.set_item_tooltip(index, str(model.get("name", "")) + "\n" + state + " · " + revision + "\n" + str(model.get("sha256", "")))
		if applied: list.set_item_custom_fg_color(index, Color("c0d3a9"))
		elif _rejected(model): list.set_item_custom_fg_color(index, Color("d6a398"))
		var thumbnail_path := _resolve_path(model.get("thumbnail", {}).get("path", "")) if model.get("thumbnail") is Dictionary else ""
		if thumbnail_cache.has(thumbnail_path): list.set_item_icon(index, thumbnail_cache[thumbnail_path])
		if str(model.get("id", "")) == str(selected.get("id", "?")):
			list.select(index)
	list_count.text = "%d de %d arquivos GLB únicos" % [filtered.size(), models.size()]
	call_deferred("_queue_thumbnails")

func _queue_thumbnails() -> void:
	if not is_instance_valid(list): return
	var scroll := list.get_v_scroll_bar().value
	for index in list.item_count:
		var rect := list.get_item_rect(index)
		if rect.position.y + rect.size.y < scroll - 100 or rect.position.y > scroll + list.size.y + 100: continue
		if list.get_item_icon(index) == null and index not in thumbnail_jobs: thumbnail_jobs.append(index)

func _process(delta: float) -> void:
	thumbnail_elapsed += delta
	if thumbnail_jobs.is_empty() or thumbnail_elapsed < 0.04: return
	thumbnail_elapsed = 0
	var index: int = thumbnail_jobs.pop_front()
	if index >= filtered.size(): return
	var rect := list.get_item_rect(index)
	var scroll := list.get_v_scroll_bar().value
	if rect.position.y + rect.size.y < scroll - 100 or rect.position.y > scroll + list.size.y + 100: return
	var thumbnail: Variant = filtered[index].get("thumbnail")
	if not thumbnail is Dictionary: return
	var path := _resolve_path(thumbnail.get("path", ""))
	if not FileAccess.file_exists(path): return
	if not thumbnail_cache.has(path):
		var image := Image.load_from_file(path)
		if image == null or image.is_empty(): return
		image.resize(64, 64, Image.INTERPOLATE_LANCZOS)
		thumbnail_cache[path] = ImageTexture.create_from_image(image)
	list.set_item_icon(index, thumbnail_cache[path])
	_queue_thumbnails()

func _select_model(model: Dictionary) -> void:
	selected = model
	note_status.select(0)
	note_comment.clear()
	ready_for_capture = false
	errors.clear()
	title_label.text = str(model.get("name", "Modelo"))
	var applied: bool = not model.get("runtime", []).is_empty()
	identity_label.text = "%s   •   Revisão %s   •   SHA %s   •   %d cópia(s) idêntica(s)" % ["APLICADO" if applied else ("REJEITADO" if _rejected(model) else "NÃO APLICADO"), str(model.get("revision", "sem rótulo")), str(model.get("sha256", "")).left(16), model.get("aliases", []).size()]
	_clear_models()
	_load_source()
	group_select.clear()
	var groups: Array[String] = []
	for entry in model.get("runtime", []):
		var group := str(entry.get("group", entry.get("binding", "Instância")))
		if group not in groups: groups.append(group)
	for group in groups: group_select.add_item(group)
	group_select.visible = not groups.is_empty()
	_load_runtime()
	_update_references()
	_filter()
	ready_for_capture = true

func _clear_models() -> void:
	if is_instance_valid(source_model):
		source_world.remove_child(source_model)
		source_model.free()
	if is_instance_valid(runtime_model):
		runtime_world.remove_child(runtime_model)
		runtime_model.free()
	source_model = null
	runtime_model = null
	source_bounds = AABB()
	runtime_bounds = AABB()

func _load_source() -> void:
	var path := str(local_models.get(str(selected.get("id", "")), _resolve_path(selected.get("glb", {}).get("path", ""))))
	if path.is_empty() or not FileAccess.file_exists(path):
		source_caption.text = "Arquivo não distribuído no catálogo. Use ‘Localizar GLB…’ para associar sua cópia pelo SHA."
		return
	var expected := str(selected.get("sha256", selected.get("glb", {}).get("sha256", "")))
	if not expected.is_empty() and FileAccess.get_sha256(path) != expected:
		source_caption.text = "O SHA-256 do GLB diverge do catálogo. Arquivo não carregado."
		errors.append(source_caption.text)
		return
	source_caption.text = "Lendo GLB original…"
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_file(path, state)
	if error != OK:
		source_caption.text = "GLB não pôde ser aberto (erro %d)." % error
		errors.append(source_caption.text)
		return
	var imported := document.generate_scene(state)
	if imported == null:
		source_caption.text = "Godot não criou a cena do GLB."
		errors.append(source_caption.text)
		return
	source_model = Node3D.new()
	source_world.add_child(source_model)
	source_model.add_child(imported)
	_remember_materials(imported)
	source_bounds = _bounds(imported, Transform3D.IDENTITY)
	source_caption.text = "Original: %s · escala original intacta antes do enquadramento" % _dimensions(source_bounds.size)
	loaded = true

func _load_runtime() -> void:
	if is_instance_valid(runtime_model):
		runtime_world.remove_child(runtime_model)
		runtime_model.free()
	runtime_model = Node3D.new()
	runtime_world.add_child(runtime_model)
	var group := group_select.get_item_text(group_select.selected) if group_select.item_count else ""
	var triangle_count := 0
	var chunks := 0
	var textures: Array[String] = []
	var hashes: Array[String] = []
	for entry in selected.get("runtime", []):
		if str(entry.get("group", entry.get("binding", "Instância"))) != group: continue
		var runtime_path := _resolve_path(entry.get("path", ""))
		if runtime_path.is_empty() or not FileAccess.file_exists(runtime_path): continue
		var result := READER.read_model(runtime_path, entry.get("origin", [0, 0, 0]))
		if not result.ok:
			errors.append(str(result.error))
			continue
		var expected_hash := str(entry.get("sha256", ""))
		if not expected_hash.is_empty() and str(result.sha256) != expected_hash:
			errors.append("SHA-256 D3D divergente: " + str(entry.get("binding", "")))
			result.node.free()
			continue
		runtime_model.add_child(result.node)
		triangle_count += int(result.triangles)
		chunks += 1
		textures.append("%d×%d" % result.texture)
		hashes.append(str(result.sha256).left(12))
	runtime_bounds = _bounds(runtime_model, Transform3D.IDENTITY)
	if chunks == 0:
		runtime_caption.text = "Arquivo aplicado não disponível localmente; metadados preservados." if not selected.get("runtime", []).is_empty() else "Sem modelo instalado neste perfil."
	else:
		runtime_caption.text = "%s · %s triângulos · %d parte(s) · RGB %s · SHA %s" % [_dimensions(runtime_bounds.size), triangle_count, chunks, ", ".join(textures), ", ".join(hashes)]
	_apply_display_transform()
	_update_info()

func _bounds(node: Node3D, inherited: Transform3D) -> AABB:
	var transform := inherited * node.transform
	var result := AABB()
	var initialized := false
	if node is MeshInstance3D and node.mesh != null:
		result = transform * node.get_aabb()
		initialized = true
	for child in node.get_children():
		if not child is Node3D: continue
		var child_box := _bounds(child, transform)
		if child_box.size == Vector3.ZERO: continue
		result = result.merge(child_box) if initialized else child_box
		initialized = true
	return result

func _dimensions(size: Vector3) -> String:
	return "%.3f × %.3f × %.3f (largura × altura × profundidade)" % [size.x, size.y, size.z]

func _apply_display_transform() -> void:
	var uniform := scale_select.selected == 0
	for pair in [[source_model, source_bounds], [runtime_model, runtime_bounds]]:
		var model: Node3D = pair[0]
		var bounds: AABB = pair[1]
		if not is_instance_valid(model): continue
		var factor := 5.0 / maxf(bounds.size.y, 0.00001) if uniform else 1.0
		model.scale = Vector3.ONE * factor
		model.position = -Vector3(bounds.get_center().x, bounds.position.y, bounds.get_center().z) * factor
	_set_materials()
	_reset_camera()

func _remember_materials(node: Node) -> void:
	if node is MeshInstance3D and node.mesh != null:
		var originals: Array = []
		for surface in node.mesh.get_surface_count(): originals.append(node.get_active_material(surface))
		node.set_meta("review_original_materials", originals)
	for child in node.get_children(): _remember_materials(child)

func _set_materials() -> void:
	if is_instance_valid(source_model): _apply_materials(source_model, true)
	if is_instance_valid(runtime_model): _apply_materials(runtime_model, false)

func _apply_materials(node: Node, source: bool) -> void:
	if node is MeshInstance3D and node.mesh != null:
		var originals: Array = node.get_meta("review_original_materials", [])
		for surface in node.mesh.get_surface_count():
			var original: Material = originals[surface] if surface < originals.size() else null
			var material: BaseMaterial3D = original.duplicate() if original is BaseMaterial3D else StandardMaterial3D.new()
			if material_select.selected == 0:
				material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			elif not source or material_select.selected == 1:
				material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
				material.metallic = 0
				material.roughness = 0.85
				material.normal_enabled = false
			material.cull_mode = BaseMaterial3D.CULL_DISABLED
			if node.material_override != null: node.material_override = null
			node.set_surface_override_material(surface, material)
	for child in node.get_children(): _apply_materials(child, source)

func _reset_camera() -> void:
	yaw = PI / 4.0
	pitch = 0.3
	pan = Vector3.ZERO
	var height := 5.0
	var max_extent := 7.0
	if scale_select.selected == 1:
		height = maxf(source_bounds.size.y, runtime_bounds.size.y)
		max_extent = maxf(maxf(source_bounds.size.x, source_bounds.size.z), maxf(runtime_bounds.size.x, runtime_bounds.size.z))
	else:
		for bounds in [source_bounds, runtime_bounds]:
			if bounds.size.y > 0.00001: max_extent = maxf(max_extent, maxf(bounds.size.x, bounds.size.z) * 5.0 / bounds.size.y)
	focus_height = height / 2.0
	distance = maxf(height * 1.5, max_extent * 1.3)
	for grid in grids:
		grid.scale = Vector3.ONE * maxf(1, max_extent / 10.0)
	_update_cameras()

func _update_cameras() -> void:
	var target := Vector3(0, focus_height, 0) + pan
	var direction := Vector3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw))
	for camera in [source_camera, runtime_camera]:
		if not is_instance_valid(camera): continue
		camera.size = distance
		camera.position = target + direction * maxf(distance * 4.0, 100.0)
		camera.look_at(target)

func _preview_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP: distance = maxf(0.02, distance * 0.88)
		if event.button_index == MOUSE_BUTTON_WHEEL_DOWN: distance = minf(1000, distance * 1.12)
	if event is InputEventMouseMotion:
		if event.button_mask & MOUSE_BUTTON_MASK_LEFT:
			yaw -= event.relative.x * 0.008
			pitch = clampf(pitch + event.relative.y * 0.008, -1.45, 1.45)
		elif event.button_mask & (MOUSE_BUTTON_MASK_RIGHT | MOUSE_BUTTON_MASK_MIDDLE):
			pan += source_camera.global_basis.x * (-event.relative.x * distance / 450.0) + source_camera.global_basis.y * (event.relative.y * distance / 450.0)
	_update_cameras()

func _update_info() -> void:
	var content := "[color=#d9bc81][b]Aplicação e proporções[/b][/color]\n"
	for entry in selected.get("runtime", []):
		content += "• %s  ·  origem %s" % [str(entry.get("binding", "")), str(entry.get("origin", []))]
		var scale: Variant = entry.get("scale", entry.get("transform", {}))
		content += "  ·  escala XYZ " + str(scale) + "\n"
		if scale is Array and scale.size() == 3:
			var low := minf(float(scale[0]), minf(float(scale[1]), float(scale[2])))
			var high := maxf(float(scale[0]), maxf(float(scale[1]), float(scale[2])))
			if high - low > 0.001: content += "[color=#e1ba77]  Escala não uniforme registrada na conversão: as proporções instaladas diferem do original.[/color]\n"
		for warning in entry.get("warnings", []): content += "[color=#d6bd89]  " + str(warning) + "[/color]\n"
	if selected.get("runtime", []).is_empty(): content += "Este GLB está preservado para revisão e não consta aplicado no perfil habitual.\n"
	for warning in selected.get("warnings", []): content += "[color=#d6bd89]• " + str(warning) + "[/color]\n"
	for error in errors: content += "[color=#e3a791]• " + error + "[/color]\n"
	content += "\nGLB: PBR original disponível. D3D: RGB + UV; sem normal, roughness, metalness, rig ou clipes.\nA paleta de 256 cores e os 64 níveis de luz do jogo não são simulados neste visor.\nO modo ‘mesma altura’ usa apenas escala uniforme de visualização, sem alterar os arquivos."
	info.text = content
	var provenance := "[b]GLB original[/b]\n" + str(selected.get("glb", {}).get("path", "")) + "\nSHA-256: " + str(selected.get("sha256", "")) + "\n\n[b]Revisão e estado[/b]\n" + str(selected.get("revision", "")) + "\n" + str(selected.get("status", "")) + "\n\n[b]Cópias idênticas[/b]\n"
	for alias in selected.get("aliases", []): provenance += str(alias) + "\n"
	provenance += "\n[b]Arquivos instalados[/b]\n"
	for entry in selected.get("runtime", []): provenance += str(entry.get("binding", "")) + "\n" + str(entry.get("path", "")) + "\n" + str(entry.get("sha256", "")) + "\n\n"
	provenance += "\n[b]Limites[/b]\nPrévia Godot dos arquivos reais, sem substituir a partida. Origem nativa aplicada às partes D3D antes de centralizar o grupo. A escala uniforme de apresentação mantém as proporções; unidades reais ficam disponíveis no seletor. O arquivo D3D não contém recortes adicionados por código, luzes, sombras, colisão ou a lógica nativa. Seleção no inspetor não promove nem modifica um candidato. Nenhum arquivo é enviado para a rede."
	file_area.text = provenance

func _update_references() -> void:
	for child in reference_area.get_children():
		reference_area.remove_child(child)
		child.queue_free()
	reference_area.add_child(_label("Referências de origem", 22, GOLD))
	var explanation := _label("A imagem comprovadamente enviada à API está separada da arte nativa e das vistas geradas. Miniaturas de resultado não são referências nativas.", 12, MUTED)
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	reference_area.add_child(explanation)
	var groups := {"api": "IMAGENS COMPROVADAMENTE ENVIADAS À API", "native": "REFERÊNCIAS NATIVAS ORIGINAIS", "generated": "VISTAS E REFERÊNCIAS GERADAS", "unverified": "ORIGEM / ENVIO NÃO COMPROVADO", "preview": "PRÉVIAS DO RESULTADO"}
	for kind in groups:
		var entries: Array = []
		for entry in selected.get("references", []):
			if str(entry.get("kind", "unverified")) == kind: entries.append(entry)
		if kind == "preview": entries.append_array(selected.get("previews", []))
		if entries.is_empty(): continue
		reference_area.add_child(_label(groups[kind], 13, GOLD))
		var grid := GridContainer.new()
		grid.columns = 3
		grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		grid.add_theme_constant_override("h_separation", 12)
		grid.add_theme_constant_override("v_separation", 14)
		reference_area.add_child(grid)
		for entry in entries:
			var box := VBoxContainer.new()
			box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			box.custom_minimum_size.x = 220
			grid.add_child(box)
			var path := _resolve_path(entry.get("path", ""))
			var image := Image.load_from_file(path) if FileAccess.file_exists(path) else null
			if image != null and not image.is_empty():
				var picture := TextureRect.new()
				picture.texture = ImageTexture.create_from_image(image)
				picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
				picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
				picture.custom_minimum_size = Vector2(220, 190)
				picture.tooltip_text = path
				box.add_child(picture)
				_button(box, "Ampliar imagem", _show_image.bind(path))
			var label := _label(str(entry.get("label", path.get_file())), 11, Color("dfe4d1"))
			label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			box.add_child(label)
			var source_kind := str(entry.get("sourceKind", kind))
			box.add_child(_label("Origem: " + source_kind + "  ·  " + str(entry.get("role", "referência")), 10, MUTED))
	if selected.get("references", []).is_empty():
		reference_area.add_child(_label("Não há referência local com vínculo comprovado neste inventário. Não foi inventada uma referência.", 12, MUTED))

func _show_image(path: String) -> void:
	var image := Image.load_from_file(path)
	if image == null or image.is_empty(): return
	var dialog := AcceptDialog.new()
	dialog.title = path.get_file()
	dialog.min_size = Vector2i(720, 600)
	var picture := TextureRect.new()
	picture.texture = ImageTexture.create_from_image(image)
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture.custom_minimum_size = Vector2(900, 650)
	dialog.add_child(picture)
	add_child(dialog)
	dialog.close_requested.connect(dialog.queue_free)
	dialog.confirmed.connect(dialog.queue_free)
	dialog.popup_centered_ratio(0.85)

func _capture(path: String) -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	if image != null and not image.is_empty():
		image.save_png(path)
		print("MODEL_REVIEW_CAPTURE=" + path)
	print("MODEL_REVIEW_READY models=%d errors=%d" % [models.size(), errors.size()])
	get_tree().quit(0 if errors.is_empty() else 1)
