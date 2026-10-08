@tool
extends EditorPlugin

const Snapshot := preload("snapshot.gd")
const Exporter := preload("exporter.gd")
const Preview := preload("preview.gd")
const GLBSource := preload("glb_source.gd")
const SCENE_PATH := "res://local/tristram.tscn"
const SNAPSHOT_PATH := "res://local/town-snapshot.json"

var dock: VBoxContainer
var content: VBoxContainer
var preview: Control
var instances: OptionButton
var details: RichTextLabel
var output: RichTextLabel
var override_check: CheckBox
var revision: LineEdit
var file_dialog: EditorFileDialog
var rebuild_dialog: ConfirmationDialog
var replacement_mode := false
var updating := false
var selected: Node3D

func _enter_tree() -> void:
	# Fontes importadas pelo GLTFDocument e evidências não precisam de uma
	# segunda importação automática do editor (nem de miniaturas de fixtures).
	for folder in ["source", "sources", "exporter-tests", "snapshot-test", "roundtrip", "visual-qa"]:
		var directory: String = "res://local/" + str(folder)
		if DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(directory)):
			var ignore := FileAccess.open(directory.path_join(".gdignore"), FileAccess.WRITE)
			if ignore != null:
				ignore.close()
	preview = Preview.new()
	get_editor_interface().get_editor_main_screen().add_child(preview)
	preview.hide()
	preview.instance_selected.connect(_select_id)
	preview.scene_visual_changed.connect(_update_details)
	_create_dock()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, dock)
	scene_changed.connect(_scene_changed)
	scene_saved.connect(func(path: String):
		if path == SCENE_PATH:
			_log("Cena salva em local/tristram.tscn.")
	)
	get_editor_interface().get_selection().selection_changed.connect(_selection_changed)
	call_deferred("_scene_changed", get_editor_interface().get_edited_scene_root())

func _exit_tree() -> void:
	if is_instance_valid(dock):
		remove_control_from_docks(dock)
		dock.queue_free()
	if is_instance_valid(preview):
		preview.queue_free()

func _has_main_screen() -> bool:
	return true

func _get_plugin_name() -> String:
	return "Tristram 3D"

func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon("MeshInstance3D", "EditorIcons")

func _make_visible(value: bool) -> void:
	if is_instance_valid(preview):
		preview.visible = value

func _create_dock() -> void:
	dock = VBoxContainer.new()
	dock.name = "Tristram"
	dock.custom_minimum_size.x = 300
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	dock.add_child(scroll)
	content = VBoxContainer.new()
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(content)
	var title := Label.new()
	title.text = "TRISTRAM • arquitetura estática"
	content.add_child(title)
	_button("Abrir / reabrir cena salva", _open_scene, "Preserva suas edições. Cria a cena pelo snapshot apenas na primeira abertura.")
	_button("Reconstruir a partir do snapshot…", _request_rebuild, "Cria uma nova baseline local e guarda uma cópia da cena salva.")
	var row := HBoxContainer.new()
	content.add_child(row)
	_button("Salvar cena", _save_scene, "Salva local/tristram.tscn; não exporta automaticamente.", row)
	_button("Exportar pacote", _export_scene, "Somente overrides explicitamente marcados. Erros conservam o pacote anterior.", row)
	instances = OptionButton.new()
	instances.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	instances.item_selected.connect(_choose_instance)
	content.add_child(instances)
	row = HBoxContainer.new()
	content.add_child(row)
	_button("Selecionar cabana", func(): _select_id("cabin-east"), "Cabana leste completa do snapshot; não é o preview do Meshy.", row)
	_button("Enquadrar", _focus, "Enquadra a instância na prévia Tristram 3D.", row)
	details = RichTextLabel.new()
	details.custom_minimum_size.y = 180
	details.bbcode_enabled = false
	details.fit_content = false
	details.selection_enabled = true
	content.add_child(details)
	override_check = CheckBox.new()
	override_check.text = "Substituir esta instância no jogo"
	override_check.toggled.connect(_override_toggled)
	content.add_child(override_check)
	revision = LineEdit.new()
	revision.placeholder_text = "Revisão explícita do override (obrigatória)"
	revision.text_submitted.connect(_set_revision)
	revision.focus_exited.connect(func(): _set_revision(revision.text))
	content.add_child(revision)
	_button("Inspecionar GLB fonte vinculado", _inspect_source, "Abre sourceGLB do snapshot como referência. Não substitui a baseline.")
	_button("Importar GLB como referência…", func(): _show_glb_dialog(false), "Referência local excluída da exportação.")
	_button("Importar GLB substituto…", func(): _show_glb_dialog(true), "Preserva a baseline oculta. Ainda é necessário marcar o override e informar revisão.")
	_button("Restaurar geometria herdada", _restore_baseline, "Desmarca override e volta a mostrar a baseline. Mantém GLBs locais para inspeção.")
	var collision := CheckBox.new()
	collision.text = "Mostrar colisão nativa (referência bloqueada)"
	collision.button_pressed = true
	collision.toggled.connect(func(value: bool):
		preview.show_collision = value
		preview.refresh()
	)
	content.add_child(collision)
	var caveat := Label.new()
	caveat.text = "Godot auxilia a revisão. Paleta, luz e aparência final precisam ser conferidas no jogo. Mover a malha não move a colisão."
	caveat.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	content.add_child(caveat)
	output = RichTextLabel.new()
	output.custom_minimum_size.y = 130
	output.size_flags_vertical = Control.SIZE_EXPAND_FILL
	output.selection_enabled = true
	content.add_child(output)
	_log("Abra a cena salva ou prepare local/town-snapshot.json pelo launcher do editor.")
	file_dialog = EditorFileDialog.new()
	file_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.glb", "Modelo GLB 2.0")
	file_dialog.file_selected.connect(_import_glb)
	dock.add_child(file_dialog)
	rebuild_dialog = ConfirmationDialog.new()
	rebuild_dialog.title = "Reconstruir a baseline local"
	rebuild_dialog.dialog_text = "A cena salva será copiada para local/backups. Salve antes de continuar se quiser conservar as edições ainda abertas. Esta ação cria a cena novamente a partir do snapshot."
	rebuild_dialog.confirmed.connect(_build_snapshot)
	dock.add_child(rebuild_dialog)

func _button(title: String, action: Callable, hint: String = "", parent: Control = null) -> void:
	var button := Button.new()
	button.text = title
	button.tooltip_text = hint
	button.pressed.connect(action)
	(parent if parent != null else content).add_child(button)

func _log(message: String) -> void:
	output.text = message

func _root() -> Node3D:
	return get_editor_interface().get_edited_scene_root() as Node3D

func _is_tristram(root: Node) -> bool:
	return root != null and root.has_meta("d3d_snapshot_path")

func _open_scene() -> void:
	if FileAccess.file_exists(SCENE_PATH):
		get_editor_interface().open_scene_from_path(SCENE_PATH)
		get_editor_interface().set_main_screen_editor("Tristram 3D")
	else:
		_build_snapshot()

func _request_rebuild() -> void:
	if FileAccess.file_exists(SCENE_PATH) or _is_tristram(_root()):
		rebuild_dialog.popup_centered(Vector2i(540, 180))
	else:
		_build_snapshot()

func _build_snapshot() -> void:
	var result: Dictionary = Snapshot.new().build(ProjectSettings.globalize_path(SNAPSHOT_PATH))
	if not result.get("ok", false):
		_log("Snapshot não carregado:\n" + "\n".join(result.get("errors", [])))
		return
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://local/backups"))
	if FileAccess.file_exists(SCENE_PATH):
		var backup := "res://local/backups/tristram-%s.tscn" % str(Time.get_unix_time_from_system()).replace(".", "-")
		if DirAccess.copy_absolute(ProjectSettings.globalize_path(SCENE_PATH), ProjectSettings.globalize_path(backup)) != OK:
			result.root.free()
			_log("Falha ao guardar backup; cena anterior preservada.")
			return
	var scene := PackedScene.new()
	var error := scene.pack(result.root)
	if error == OK:
		error = ResourceSaver.save(scene, SCENE_PATH)
	result.root.free()
	if error != OK:
		_log("Falha ao salvar a cena do snapshot (%s)." % error)
		return
	# reload_scene_from_path evita um tab antigo de mesma origem após reconstrução.
	get_editor_interface().reload_scene_from_path(SCENE_PATH)
	get_editor_interface().open_scene_from_path(SCENE_PATH)
	get_editor_interface().set_main_screen_editor("Tristram 3D")
	_log("Snapshot carregado. Nenhum override marcado.\n" + "\n".join(result.get("warnings", [])))

func _save_scene() -> void:
	if not _is_tristram(_root()):
		_log("Abra uma cena Tristram antes de salvar.")
		return
	_set_revision(revision.text)
	get_editor_interface().save_scene_as(SCENE_PATH)

func _export_scene() -> void:
	if not _is_tristram(_root()):
		_log("Abra uma cena Tristram antes de exportar.")
		return
	_set_revision(revision.text)
	var result: Dictionary = Exporter.new().export_scene(_root(), ProjectSettings.globalize_path("res://local/export"))
	if not result.get("ok", false):
		_log("Pacote não alterado:\n" + "\n".join(result.get("errors", [])))
	else:
		_log("Pacote exportado em local/export.\nOverrides: %s\n%s\nAplique pelo launcher em um perfil de revisão." % [result.get("instances", 0), "\n".join(result.get("warnings", []))])

func _scene_changed(scene: Node) -> void:
	selected = null
	instances.clear()
	if _is_tristram(scene):
		preview.set_source(scene)
		var architecture := scene.get_node_or_null("Architecture")
		if architecture != null:
			for instance in architecture.get_children():
				if not instance.has_meta("d3d_instance"):
					continue
				var identity: Dictionary = instance.get_meta("d3d_instance")
				instances.add_item(str(identity.get("instanceId", instance.name)))
				instances.set_item_metadata(instances.item_count - 1, instance)
		_select_id("cabin-east")
	else:
		preview.set_source(null)
	_update_details()

func _choose_instance(index: int) -> void:
	if index >= 0 and index < instances.item_count:
		_select_id(instances.get_item_text(index))

func _select_id(id: String) -> void:
	for index in instances.item_count:
		if instances.get_item_text(index) == id:
			selected = instances.get_item_metadata(index) as Node3D
			if not is_instance_valid(selected):
				return
			instances.select(index)
			var selection := get_editor_interface().get_selection()
			selection.clear()
			selection.add_node(selected)
			get_editor_interface().edit_node(selected)
			preview.select_instance(id)
			_update_details()
			return

func _selection_changed() -> void:
	for node in get_editor_interface().get_selection().get_selected_nodes():
		var ancestor: Node = node
		while ancestor != null:
			if ancestor.has_meta("d3d_instance"):
				selected = ancestor as Node3D
				var id := str(ancestor.get_meta("d3d_instance").get("instanceId", ""))
				for index in instances.item_count:
					if instances.get_item_text(index) == id:
						instances.select(index)
				preview.select_instance(id)
				_update_details()
				return
			ancestor = ancestor.get_parent()

func _update_details() -> void:
	updating = true
	if not is_instance_valid(selected):
		details.text = "Nenhuma instância de arquitetura selecionada."
		override_check.disabled = true
		override_check.button_pressed = false
		revision.text = ""
		updating = false
		return
	var identity: Dictionary = selected.get_meta("d3d_instance", {})
	var has_fire := bool(selected.get_meta("d3d_has_fire", false)) or str(identity.get("instanceId", "")) == "cabin-east"
	var status := str(selected.get_meta("d3d_registry_status", "não verificado"))
	var mode := "OVERRIDE explícito" if bool(selected.get_meta("d3d_override", false)) else "HERDADA • baseline preservada"
	details.text = "%s\n%s / %s\n%s\nCatálogo: %s (estado técnico não é aprovação)\nRevisão baseline: %s\nSHA fonte: %s\nSHA geometria: %s\nVínculo nativo fixo: %s → %s" % [identity.get("instanceId", ""), identity.get("assetId", ""), identity.get("variantId", ""), mode, status, identity.get("revision", "não informada"), identity.get("sha256", "não informado"), identity.get("geometrySha256", "não informado"), identity.get("nativeMin", []), identity.get("nativeMax", [])]
	if has_fire:
		details.text += "\nFOGO/LUZ NATIVOS: inspeção permitida; override bloqueado nesta versão. O GLB fonte não contém o interior acrescentado pelo jogo."
	var low: Array = identity.get("nativeMin", [0, 0])
	if selected.position.distance_to(Vector3(float(low[0]), 0, float(low[1]))) > 0.0001 or not selected.basis.is_equal_approx(Basis.IDENTITY):
		details.text += "\nAVISO: transformação visual alterada; a colisão nativa continua no vínculo original."
	if not selected.get_meta("d3d_variant_valid", true):
		details.text += "\nVARIANTE DIVERGENTE do catálogo: exportação será recusada."
	override_check.disabled = has_fire
	override_check.button_pressed = bool(selected.get_meta("d3d_override", false))
	if not revision.has_focus():
		revision.text = str(selected.get_meta("d3d_revision", ""))
	updating = false

func _mark_changed() -> void:
	get_editor_interface().mark_scene_as_unsaved()
	preview.refresh()
	_update_details()

func _override_toggled(value: bool) -> void:
	if updating or not is_instance_valid(selected):
		return
	var identity: Dictionary = selected.get_meta("d3d_instance", {})
	var needs_revision := value and str(selected.get_meta("d3d_revision", "")) == str(identity.get("revision", ""))
	if needs_revision:
		selected.set_meta("d3d_revision", "")
	selected.set_meta("d3d_override", value)
	_mark_changed()
	if needs_revision:
		revision.grab_focus()
		_log("Informe uma revisão própria para este override. A revisão herdada permanece identificada no vínculo da baseline.")

func _set_revision(value: String) -> void:
	if updating or not is_instance_valid(selected):
		return
	if str(selected.get_meta("d3d_revision", "")) != value.strip_edges():
		selected.set_meta("d3d_revision", value.strip_edges())
		get_editor_interface().mark_scene_as_unsaved()

func _focus() -> void:
	get_editor_interface().set_main_screen_editor("Tristram 3D")
	preview.focus_selected()

func _show_glb_dialog(replacement: bool) -> void:
	if not is_instance_valid(selected):
		_log("Selecione a instância que receberá o vínculo GLB.")
		return
	replacement_mode = replacement
	file_dialog.title = "GLB substituto — revisão explícita" if replacement else "GLB fonte — somente referência"
	file_dialog.popup_centered_ratio(0.75)

func _inspect_source() -> void:
	if not is_instance_valid(selected):
		return
	var identity: Dictionary = selected.get_meta("d3d_instance", {})
	var source_path := str(identity.get("sourceGlb", identity.get("sourceGLB", "")))
	if source_path.is_empty():
		_log("Esta instância não tem GLB fonte no snapshot. Use Importar GLB como referência para vinculá-lo explicitamente.")
		return
	if not source_path.is_absolute_path():
		source_path = ProjectSettings.globalize_path(SNAPSHOT_PATH).get_base_dir().path_join(source_path)
	var expected := str(identity.get("sourceSha256", ""))
	if not expected.is_empty() and FileAccess.get_sha256(source_path) != expected:
		_log("GLB fonte divergente: hash não corresponde ao snapshot. Fonte não importada.")
		return
	replacement_mode = false
	_import_glb(source_path)

func _import_glb(path: String) -> void:
	if not is_instance_valid(selected):
		return
	var result: Dictionary = GLBSource.import_glb(path, selected, replacement_mode)
	if not result.get("ok", false):
		_log("GLB não importado:\n" + "\n".join(result.get("errors", [])))
		return
	var identity: Dictionary = selected.get_meta("d3d_instance", {})
	var recorded_fit := false
	if not replacement_mode and result.get("sha256", "") == identity.get("sourceSha256", ""):
		recorded_fit = GLBSource.apply_recorded_fit(result.branch, identity.get("sourceFit", {}))
	_mark_changed()
	preview.set_display_mode(0 if replacement_mode else 2)
	get_editor_interface().set_main_screen_editor("Tristram 3D")
	_log("GLB %s vinculado a %s.\nSHA: %s\n%s\n%s" % ["substituto" if replacement_mode else "fonte (somente referência)", selected.name, result.get("sha256", ""), "Fit registrado da revisão selecionada aplicado." if recorded_fit else "Ajuste inicial uniforme ao footprint; confira escala, orientação e altura no editor 3D.", "Marque o override e informe a revisão para exportar. Materiais/animação incompatíveis serão recusados." if replacement_mode else "Selecione GLB fonte (PBR) no topo da prévia para inspecionar. Baseline continua herdada."])
	preview.focus_selected()

func _restore_baseline() -> void:
	if not is_instance_valid(selected):
		return
	for child in selected.get_children():
		if child is Node3D:
			child.visible = str(child.get_meta("d3d_source_mode", "")).is_empty()
	selected.set_meta("d3d_override", false)
	_mark_changed()
	preview.set_display_mode(0)
	_log("Geometria herdada restaurada; override desmarcado. GLBs preservados localmente.")
