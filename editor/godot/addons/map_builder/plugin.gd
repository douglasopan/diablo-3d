@tool
extends EditorPlugin
## Cooperates with tristram_editor. No second viewport, scene path or catalogue.

const Session := preload("session.gd")
const Snapshot := preload("res://addons/tristram_editor/snapshot.gd")

var session := Session.new()
var dock: VBoxContainer
var body: VBoxContainer
var search: LineEdit
var list: ItemList
var details: RichTextLabel
var result: Label
var mode: OptionButton
var positions: Array[SpinBox] = []
var rotations: Array[SpinBox] = []
var uniform: SpinBox
var revision: LineEdit
var note: TextEdit
var review: OptionButton
var reviewer: LineEdit
var receipt_dialog: EditorFileDialog
var snapshot_dialog: EditorFileDialog
var edit_button: Button
var restore_button: Button
var selected_id := "tavern-main"
var active_scene: Node3D
var original_root: Node3D
var applied_root: Node3D
var preview: Control
var updating := false
var elapsed := 0.0
var comparison_snapshot_path := ""
var review_fingerprint := ""

func _enter_tree() -> void:
	session.read_registry_names(ProjectSettings.globalize_path("res://").path_join("../../assets/registry.json").simplify_path())
	_create_dock()
	add_control_to_dock(DOCK_SLOT_RIGHT_BL, dock)
	scene_changed.connect(_scene_changed)
	get_editor_interface().get_selection().selection_changed.connect(_editor_selection)
	call_deferred("_scene_changed", get_editor_interface().get_edited_scene_root())

func _exit_tree() -> void:
	_restore_preview_source()
	if is_instance_valid(dock):
		remove_control_from_docks(dock)
		dock.queue_free()
	_free_comparison_roots()

func _create_dock() -> void:
	dock = VBoxContainer.new()
	dock.name = "Montagem Tristram"
	dock.custom_minimum_size.x = 340
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	dock.add_child(scroll)
	body = VBoxContainer.new()
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(body)
	var title := Label.new()
	title.text = "MONTAGEM • proposta por instância"
	body.add_child(title)
	search = LineEdit.new()
	search.placeholder_text = "Buscar nome, ID, asset ou revisão"
	search.text_changed.connect(func(_text: String): _fill_list())
	body.add_child(search)
	list = ItemList.new()
	list.custom_minimum_size.y = 140
	list.item_selected.connect(_choose)
	body.add_child(list)
	mode = OptionButton.new()
	mode.add_item("Cena em edição — candidato", 0)
	mode.add_item("Base — snapshot identificado", 1)
	mode.add_item("Aplicado em disco — recibo verificado", 2)
	mode.item_selected.connect(_change_mode)
	body.add_child(mode)
	var row := HBoxContainer.new()
	body.add_child(row)
	_button("Enquadrar", _focus, row)
	_button("Ângulo original", _original_angle, row)
	_button("Carregar recibo aplicado…", func(): receipt_dialog.popup_centered_ratio(0.7))
	_button("Snapshot de comparação…", func(): snapshot_dialog.popup_centered_ratio(0.7))
	details = RichTextLabel.new()
	details.custom_minimum_size.y = 210
	details.selection_enabled = true
	body.add_child(details)
	positions = _vector_controls("Posição X / altura / Z", 0.05)
	rotations = _vector_controls("Rotação X / Y / Z (graus)", 1.0)
	var scale_title := Label.new()
	scale_title.text = "Escala uniforme (preserva proporções)"
	body.add_child(scale_title)
	uniform = SpinBox.new()
	uniform.min_value = 0.01
	uniform.max_value = 100.0
	uniform.step = 0.01
	uniform.value = 1.0
	body.add_child(uniform)
	row = HBoxContainer.new()
	body.add_child(row)
	edit_button = _button("Editar transformação", _apply_transform, row)
	restore_button = _button("Restaurar transformação", _restore_transform, row)
	var caveat := Label.new()
	caveat.text = "Colisão/SOL, portas e triggers continuam fixos. Alterar esta malha pode deixar o visual fora da colisão. Base do snapshot pode ser procedural; não é arte original. Home no jogo é outro backend."
	caveat.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.add_child(caveat)
	revision = LineEdit.new()
	revision.placeholder_text = "Revisão da proposta: tavern-placement-r1"
	body.add_child(revision)
	review = OptionButton.new()
	for label in ["Revisão pendente", "Montagem revisada", "Ajuste solicitado", "Montagem aprovada (escopo abaixo)"]:
		review.add_item(label)
	review.item_selected.connect(func(index: int): review_fingerprint = Session.assembly_sha(_selected()) if index > 0 and _selected() != null else "")
	body.add_child(review)
	reviewer = LineEdit.new()
	reviewer.placeholder_text = "Responsável pela revisão (não inventar aprovação)"
	body.add_child(reviewer)
	note = TextEdit.new()
	note.custom_minimum_size.y = 90
	note.placeholder_text = "Defeito, escopo da revisão e evidências; mesma perspectiva + órbita"
	body.add_child(note)
	_button("Salvar proposta local", _save_proposal)
	var separation := Label.new()
	separation.text = "Salvar cria CANDIDATO imutável; não salva a cena, exporta, instala ou promove aceitação no catálogo. Ctrl+S salva a cena atual. Exportar/aplicar seguem o dock/contrato existentes."
	separation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.add_child(separation)
	result = Label.new()
	result.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	result.text = "Abra sua cena existente. Nenhuma reconstrução é feita por este painel."
	body.add_child(result)
	receipt_dialog = EditorFileDialog.new()
	receipt_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	receipt_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	receipt_dialog.title = "Recibo aplicado — leitura e hashes; sem escrita no perfil"
	receipt_dialog.add_filter("*.json", "godot-pack-receipt.json")
	receipt_dialog.file_selected.connect(_load_receipt)
	dock.add_child(receipt_dialog)
	snapshot_dialog = EditorFileDialog.new()
	snapshot_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	snapshot_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	snapshot_dialog.title = "Snapshot de comparação existente — somente memória; cena preservada"
	snapshot_dialog.add_filter("*.json", "town-snapshot.json")
	snapshot_dialog.file_selected.connect(_load_comparison_snapshot)
	dock.add_child(snapshot_dialog)

func _vector_controls(label: String, step: float) -> Array[SpinBox]:
	var title := Label.new()
	title.text = label
	body.add_child(title)
	var row := HBoxContainer.new()
	body.add_child(row)
	var controls: Array[SpinBox] = []
	for axis in 3:
		var field := SpinBox.new()
		field.min_value = -10000
		field.max_value = 10000
		field.step = step
		field.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(field)
		controls.append(field)
	return controls

func _button(label: String, action: Callable, parent: Control = null) -> Button:
	var button := Button.new()
	button.text = label
	button.pressed.connect(action)
	(parent if parent != null else body).add_child(button)
	return button

func _scene_changed(scene: Node) -> void:
	_restore_preview_source()
	_free_comparison_roots()
	session.application.clear()
	session.base_fingerprints.clear()
	session.loaded_scenes.clear()
	comparison_snapshot_path = ""
	active_scene = scene as Node3D if scene != null and scene.has_meta("d3d_snapshot_path") else null
	session.capture_base(active_scene)
	mode.select(0)
	review.select(0)
	review_fingerprint = ""
	preview = _find_preview()
	_fill_list()
	_update_details()

func _find_preview() -> Control:
	for candidate in get_editor_interface().get_editor_main_screen().get_children():
		var script: Script = candidate.get_script()
		if script != null and script.resource_path == "res://addons/tristram_editor/preview.gd":
			return candidate as Control
	return null

func _free_comparison_roots() -> void:
	if is_instance_valid(original_root):
		original_root.free()
	if is_instance_valid(applied_root):
		applied_root.free()
	original_root = null
	applied_root = null

func _restore_preview_source() -> void:
	if is_instance_valid(preview):
		preview.set_source(active_scene if is_instance_valid(active_scene) else null)

func _working_root() -> Node3D:
	return original_root if mode.selected == 1 else (applied_root if mode.selected == 2 else active_scene)

func _selected() -> Node3D:
	return Session.find_instance(_working_root(), selected_id)

func _fill_list() -> void:
	list.clear()
	var filter := search.text.to_lower().strip_edges()
	for node in Session.instances(_working_root()):
		var identity: Dictionary = node.get_meta("d3d_instance", {})
		var id := str(identity.get("instanceId", node.name))
		var label := session.label_for(node)
		var haystack := (label + " " + id + " " + str(identity.get("assetId", "")) + " " + str(node.get_meta("review_selected_revision", identity.get("revision", "")))).to_lower()
		if not filter.is_empty() and not haystack.contains(filter):
			continue
		list.add_item(label)
		list.set_item_metadata(list.item_count - 1, id)
		if id == selected_id:
			list.select(list.item_count - 1)

func _choose(index: int) -> void:
	_reset_review()
	selected_id = str(list.get_item_metadata(index))
	var scene_node := Session.find_instance(active_scene, selected_id)
	if scene_node != null:
		var selection := get_editor_interface().get_selection()
		selection.clear()
		selection.add_node(scene_node)
		get_editor_interface().edit_node(scene_node)
	_sync_preview()
	_update_details()

func _editor_selection() -> void:
	for selected_node in get_editor_interface().get_selection().get_selected_nodes():
		var ancestor: Node = selected_node
		while ancestor != null:
			if ancestor.has_meta("d3d_instance"):
				var next_id := str(ancestor.get_meta("d3d_instance", {}).get("instanceId", ""))
				if selected_id != next_id:
					_reset_review()
				selected_id = next_id
				_fill_list()
				# Existing dock changes the preview too; finish cooperation after it.
				call_deferred("_sync_preview")
				_update_details()
				return
			ancestor = ancestor.get_parent()

func _snapshot_path() -> String:
	return comparison_snapshot_path if not comparison_snapshot_path.is_empty() else (str(active_scene.get_meta("d3d_snapshot_path", "")) if is_instance_valid(active_scene) else "")

func _load_comparison_snapshot(path: String) -> void:
	if not is_instance_valid(active_scene):
		result.text = "Abra a cena existente primeiro."
		return
	var built: Dictionary = Snapshot.new().build(path)
	if not built.get("ok", false):
		result.text = "Snapshot recusado: " + "; ".join(built.get("errors", []))
		return
	for node in Session.instances(built.root):
		var identity: Dictionary = node.get_meta("d3d_instance", {})
		var known := Session.find_instance(active_scene, str(identity.get("instanceId", "")))
		if known == null or known.get_meta("d3d_instance", {}).get("assetId", "") != identity.get("assetId", ""):
			built.root.free()
			result.text = "Snapshot tem vínculos desconhecidos; nenhum catálogo/ID foi criado."
			return
	mode.select(0)
	_sync_preview()
	_free_comparison_roots()
	original_root = built.root
	comparison_snapshot_path = path
	session.application.clear()
	mode.select(1)
	_change_mode(1)
	_reset_review()
	result.text = "Snapshot existente carregado somente em memória. A cena em edição foi preservada. Recarregue o recibo para conferir este snapshot com os arquivos aplicados."

func _change_mode(index: int) -> void:
	_reset_review()
	if not is_instance_valid(active_scene):
		mode.select(0)
		return
	if index == 1 and not is_instance_valid(original_root):
		var built: Dictionary = Snapshot.new().build(_snapshot_path())
		if not built.get("ok", false):
			result.text = "Snapshot não disponível: " + "; ".join(built.get("errors", []))
			mode.select(0)
		else:
			original_root = built.root
	if index == 2 and not is_instance_valid(applied_root):
		result.text = "Carregue primeiro godot-pack-receipt.json. O hash do modelo visível na cena não comprova aplicação."
		mode.select(0)
	_fill_list()
	_sync_preview()
	_update_details()

func _sync_preview() -> void:
	if mode.selected == 2 and is_instance_valid(applied_root) and not Session.application_is_current(applied_root.get_meta("map_builder_application", {})):
		mode.select(0)
		result.text = "Recibo/manifesto/modelos mudaram desde a verificação. Recarregue explicitamente o recibo; estado aplicado suspenso."
		_reset_review()
	if not is_instance_valid(preview):
		preview = _find_preview()
	if is_instance_valid(preview):
		preview.set_source(_working_root())
		preview.set_display_mode(0)
		preview.select_instance(selected_id)
		if is_instance_valid(preview.caption):
			preview.caption.text = ["CENA / CANDIDATO", "BASE / SNAPSHOT IDENTIFICADO (PODE SER PROCEDURAL)", "APLICADO EM DISCO / TRANSFORM CANDIDATO"][mode.selected] + " • Godot: luz/paleta auxiliares • botão direito: órbita • meio: mover • roda: zoom"

func _load_receipt(path: String) -> void:
	if not is_instance_valid(active_scene):
		result.text = "Abra uma cena Tristram existente primeiro."
		return
	var verified: Dictionary = session.read_application(path, active_scene)
	if not verified.get("ok", false):
		# Drop the old display too: never leave a stale applied label after rejection.
		mode.select(0)
		_sync_preview()
		if is_instance_valid(applied_root):
			applied_root.free()
		applied_root = null
		result.text = verified.error
		_update_details()
		return
	var built: Dictionary = session.applied_preview(_snapshot_path())
	if not built.get("ok", false):
		result.text = "; ".join(built.get("errors", []))
		return
	mode.select(0)
	_sync_preview()
	if is_instance_valid(applied_root):
		applied_root.free()
	applied_root = built.root
	mode.select(2)
	_change_mode(2)
	result.text = "%d vínculos conferidos por hash. Prévia lê os D3D do disco; não comprova qual cena a partida aberta tem em cache. Resíduos nativos fora do snapshot podem não aparecer aqui." % verified.bindings

func _focus() -> void:
	_sync_preview()
	if is_instance_valid(preview):
		get_editor_interface().set_main_screen_editor("Tristram 3D")
		preview.focus_selected()
	else:
		result.text = "Habilite o plugin Tristram existente; este addon reutiliza sua prévia."

func _original_angle() -> void:
	_sync_preview()
	if is_instance_valid(preview):
		get_editor_interface().set_main_screen_editor("Tristram 3D")
		preview.reset_camera()

func _update_details() -> void:
	var node := _selected()
	var enabled := node != null and mode.selected != 1 and Session.editable(node)
	edit_button.disabled = not enabled
	restore_button.disabled = not enabled
	if node == null:
		details.text = "Nenhuma instância nesta origem. IDs e seleções continuam nos catálogos existentes."
		return
	var identity: Dictionary = node.get_meta("d3d_instance", {})
	var box := Session.bounds(node)
	var moved := not (Session._parent_transform(node) * node.transform).is_equal_approx(Session.baseline(node))
	var applied := str(node.get_meta("map_builder_applied_sha256", ""))
	details.text = "%s\n%s / %s\nRevisão snapshot/D3D: %s\nSHA snapshot/D3D: %s\nRevisão cena: %s\nSHA fonte cena: %s\nEstado: %s\nNativeMin/Max fixos: %s → %s\nLimites visuais: %s → %s\nAltura mínima/chão: %.4f (chão = 0)\nTransformação: %s\n%s" % [selected_id, identity.get("assetId", ""), identity.get("variantId", ""), identity.get("revision", ""), identity.get("sha256", "não informado"), node.get_meta("review_selected_revision", "herdada"), node.get_meta("review_selected_sha256", "não vinculada"), "APLICADO EM DISCO verificado ao carregar; carga em runtime não comprovada" if not applied.is_empty() else ("BASE do snapshot; pode ser procedural" if mode.selected == 1 else "CANDIDATO da cena; aplicação não comprovada"), identity.get("nativeMin", []), identity.get("nativeMax", []), box.min, box.max, float(box.groundGap), Session.transform_data(node.transform), "AVISO: VISUAL DIVERGE DA COLISÃO FIXA." if moved else "Colisão nativa fixa; escala de GLB pode ter seu próprio encaixe."]
	var recorded := str(_working_root().get_meta("d3d_snapshot_sha256", ""))
	details.text += "\nSnapshot de comparação: " + _snapshot_path()
	if FileAccess.file_exists(_snapshot_path()) and recorded != FileAccess.get_sha256(_snapshot_path()):
		details.text += "\nSNAPSHOT mudou no disco: a cena tem outra base; comparação requer revisão explícita."
	if mode.selected == 0 and not comparison_snapshot_path.is_empty() and str(active_scene.get_meta("d3d_snapshot_sha256", "")) != FileAccess.get_sha256(comparison_snapshot_path):
		details.text += "\nCENA com snapshot diferente da comparação; não representa automaticamente o pacote aplicado."
	if not enabled and mode.selected != 1:
		details.text += "\nInstância congelada/bloqueada."
	if not updating:
		updating = true
		var position := node.position
		var rotation := node.rotation_degrees
		for axis in 3:
			if not positions[axis].get_line_edit().has_focus():
				positions[axis].value = position[axis]
			if not rotations[axis].get_line_edit().has_focus():
				rotations[axis].value = rotation[axis]
		if not uniform.get_line_edit().has_focus():
			uniform.value = node.scale.x
		updating = false

func _apply_transform() -> void:
	var node := _selected()
	if node == null or mode.selected == 1:
		return
	var computed := Session.transform_for(node, Vector3(positions[0].value, positions[1].value, positions[2].value), Vector3(rotations[0].value, rotations[1].value, rotations[2].value), uniform.value)
	if not computed.ok:
		result.text = computed.error
		return
	_change_transform(node, computed.transform)

func _restore_transform() -> void:
	var node := _selected()
	if node != null and mode.selected != 1 and Session.editable(node):
		_change_transform(node, Session.baseline(node))

func _change_transform(node: Node3D, value: Transform3D) -> void:
	review.select(0)
	review_fingerprint = ""
	if mode.selected == 0:
		var undo := get_undo_redo()
		undo.create_action("Montagem Tristram: " + selected_id)
		undo.add_do_property(node, "transform", value)
		undo.add_undo_property(node, "transform", node.transform)
		undo.commit_action()
		get_editor_interface().mark_scene_as_unsaved()
	else:
		node.transform = value
	_sync_preview()
	_update_details()
	result.text = "Transformação candidata. Colisão/SOL continuam fixos; restaure para voltar à posição do snapshot/D3D. Salve a proposta para registrar esta revisão."

func _save_proposal() -> void:
	if review.selected > 0 and (_selected() == null or Session.assembly_sha(_selected()) != review_fingerprint):
		review.select(0)
		result.text = "A montagem mudou depois da revisão. Confira e selecione o estado de revisão novamente; aprovação anterior não foi reaproveitada."
		return
	var states := ["pending", "reviewed", "changes-requested", "assembly-approved"]
	var report := session.save_proposal(_working_root(), _selected(), revision.text.strip_edges(), note.text, states[review.selected], reviewer.text, ["scene", "original", "applied"][mode.selected])
	result.text = "CANDIDATO salvo: %s\nSHA: %s\nRevisão registrada separadamente; exportar/aplicar continuam explícitos." % [report.path, report.sha256] if report.get("ok", false) else str(report.get("error", "Falha ao salvar proposta."))

func _reset_review() -> void:
	if is_instance_valid(review):
		review.select(0)
		review_fingerprint = ""
	if is_instance_valid(revision):
		revision.text = ""
	if is_instance_valid(note):
		note.text = ""

func _process(delta: float) -> void:
	elapsed += delta
	if elapsed >= 1.0 and is_instance_valid(details):
		elapsed = 0.0
		_update_details()
