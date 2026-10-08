@tool
extends VBoxContainer
## Visualização auxiliar: a cena editada permanece nas coordenadas do contrato.

signal instance_selected(instance_id: String)
signal scene_visual_changed

const NATIVE_HEIGHT_SCALE := 0.816496580927726
const NATIVE_FOCAL := 45.25483399593904

var source: Node3D
var viewport: SubViewport
var camera: Camera3D
var world: Node3D
var clone: Node3D
var selection: MeshInstance3D
var canvas: SubViewportContainer
var caption: Label
var selected_id := "cabin-east"
var target := Vector3(72, 1, 69)
var yaw := PI / 4.0
var pitch := PI / 6.0
var zoom := 1.0
var native_proportions := true
var original := true
var proportions: CheckButton
var display_mode := 0
var show_collision := true
var interior_only := false
var mode_selector: OptionButton
var dragging := false
var panning := false
var refresh_elapsed := 0.0
var source_signature := ""

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	var toolbar := HFlowContainer.new()
	add_child(toolbar)
	_button(toolbar, "Ângulo original [Home]", reset_camera)
	_button(toolbar, "Enquadrar [F]", focus_selected)
	_button(toolbar, "Atualizar prévia", refresh)
	proportions = CheckButton.new()
	proportions.text = "Proporção vertical do jogo"
	proportions.button_pressed = true
	proportions.toggled.connect(func(value: bool):
		native_proportions = value
		refresh()
	)
	toolbar.add_child(proportions)
	mode_selector = OptionButton.new()
	mode_selector.add_item("Resultado da cena")
	mode_selector.add_item("Snapshot herdado")
	mode_selector.add_item("GLB fonte (PBR)")
	mode_selector.item_selected.connect(func(index: int):
		display_mode = index
		refresh()
	)
	toolbar.add_child(mode_selector)
	var interior := CheckButton.new()
	interior.text = "Corte para interior"
	interior.tooltip_text = "Oculta o exterior e corta a metade frontal na prévia da instância selecionada. Não altera a cena nem o pacote."
	interior.toggled.connect(func(value: bool):
		interior_only = value
		if value:
			set_display_mode(1)
		refresh()
		focus_selected()
	)
	toolbar.add_child(interior)
	caption = Label.new()
	caption.text = "Geometria real • cores/luz auxiliares • botão direito: órbita • meio: mover • roda: zoom"
	caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(caption)
	canvas = SubViewportContainer.new()
	canvas.stretch = true
	canvas.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	canvas.size_flags_vertical = Control.SIZE_EXPAND_FILL
	canvas.focus_mode = Control.FOCUS_ALL
	canvas.gui_input.connect(_input_preview)
	canvas.mouse_entered.connect(func(): canvas.grab_focus())
	add_child(canvas)
	viewport = SubViewport.new()
	viewport.size = Vector2i(1000, 650)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	canvas.add_child(viewport)
	world = Node3D.new()
	viewport.add_child(world)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.065, 0.073, 0.085)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.83, 0.87, 0.94)
	environment.environment.ambient_light_energy = 0.65
	world.add_child(environment)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -30, 0)
	sun.light_energy = 0.8
	world.add_child(sun)
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.near = 0.05
	camera.far = 600.0
	world.add_child(camera)
	camera.make_current()
	selection = MeshInstance3D.new()
	world.add_child(selection)
	_update_camera()

func _button(parent: Control, title: String, action: Callable) -> void:
	var button := Button.new()
	button.text = title
	button.pressed.connect(action)
	parent.add_child(button)

func set_source(value: Node3D) -> void:
	source = value
	source_signature = ""
	if is_instance_valid(viewport):
		refresh()

func _process(delta: float) -> void:
	if not is_instance_valid(viewport):
		return
	refresh_elapsed += delta
	if refresh_elapsed >= 0.7:
		refresh_elapsed = 0.0
		if is_instance_valid(source):
			var signature := _signature(source)
			if signature != source_signature:
				refresh()
		elif is_instance_valid(clone):
			clone.queue_free()
			clone = null
	_update_camera()

func _signature(node: Node) -> String:
	var result := str(node.get_instance_id(), ":", node.name)
	if node is Node3D:
		result += str(node.transform, node.visible)
	if node is MeshInstance3D:
		result += str(node.mesh, node.material_override)
	for child in node.get_children():
		result += _signature(child)
	return result

func refresh() -> void:
	if not is_instance_valid(world):
		return
	if is_instance_valid(clone):
		world.remove_child(clone)
		clone.queue_free()
		clone = null
	if is_instance_valid(source):
		clone = source.duplicate(0) as Node3D
		world.add_child(clone)
		# Escala só da prévia, nunca da cena salva nem da exportação.
		clone.scale.y *= NATIVE_HEIGHT_SCALE if native_proportions else 1.0
		_apply_display_mode(clone)
		source_signature = _signature(source)
	_update_selection()
	scene_visual_changed.emit()

func set_display_mode(index: int) -> void:
	display_mode = index
	if is_instance_valid(mode_selector):
		mode_selector.select(index)
	refresh()

func _apply_display_mode(node: Node) -> void:
	if node.name == "CollisionReference" and node is Node3D:
		node.visible = show_collision
	if node.has_meta("d3d_instance"):
		for child in node.get_children():
			if not child is Node3D:
				continue
			var mode := str(child.get_meta("d3d_source_mode", ""))
			if display_mode == 1:
				child.visible = mode.is_empty()
			elif display_mode == 2:
				child.visible = mode == "reference"
			elif mode == "reference":
				child.visible = false
			if interior_only and display_mode == 1 and str(node.get_meta("d3d_instance").get("instanceId", "")) == selected_id and child.has_meta("d3d_surface"):
				child.visible = int(child.get_meta("d3d_surface").get("role", 0)) != 0
	for child in node.get_children():
		_apply_display_mode(child)

func select_instance(id: String, focus: bool = false) -> void:
	selected_id = id
	if interior_only:
		refresh()
	_update_selection()
	if focus:
		focus_selected()

func _find_instance(node: Node, id: String) -> Node3D:
	if node.has_meta("d3d_instance") and str(node.get_meta("d3d_instance").get("instanceId", "")) == id:
		return node as Node3D
	for child in node.get_children():
		var found := _find_instance(child, id)
		if found != null:
			return found
	return null

func _bounds(node: Node3D) -> AABB:
	var result := AABB()
	var initialized := false
	for child in _meshes(node):
		if child.mesh == null or not child.is_visible_in_tree():
			continue
		var box: AABB = child.global_transform * child.get_aabb()
		if not initialized:
			result = box
			initialized = true
		else:
			result = result.merge(box)
	return result

func _meshes(node: Node) -> Array[MeshInstance3D]:
	var result: Array[MeshInstance3D] = []
	if node is MeshInstance3D:
		result.append(node)
	for child in node.get_children():
		result.append_array(_meshes(child))
	return result

func _update_selection() -> void:
	if not is_instance_valid(selection):
		return
	selection.mesh = null
	if not is_instance_valid(clone):
		return
	var instance := _find_instance(clone, selected_id)
	if instance == null:
		return
	var box := _bounds(instance)
	var mesh := ImmediateMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.72, 0.22)
	material.no_depth_test = true
	mesh.surface_begin(Mesh.PRIMITIVE_LINES, material)
	var a := box.position - Vector3.ONE * 0.04
	var b := box.end + Vector3.ONE * 0.04
	var corners: Array[Vector3] = []
	for y in [a.y, b.y]:
		for z in [a.z, b.z]:
			for x in [a.x, b.x]:
				corners.append(Vector3(x, y, z))
	for edge in [[0, 1], [0, 2], [1, 3], [2, 3], [4, 5], [4, 6], [5, 7], [6, 7], [0, 4], [1, 5], [2, 6], [3, 7]]:
		mesh.surface_add_vertex(corners[edge[0]])
		mesh.surface_add_vertex(corners[edge[1]])
	mesh.surface_end()
	selection.mesh = mesh

func focus_selected() -> void:
	if not is_instance_valid(clone):
		return
	var instance := _find_instance(clone, selected_id)
	if instance == null:
		return
	var box := _bounds(instance)
	target = box.get_center()
	zoom = clampf(maxf(box.size.x, maxf(box.size.y, box.size.z)) * 1.6 / 14.0, 0.12, 9.0)
	_update_camera()

func reset_camera() -> void:
	yaw = PI / 4.0
	pitch = PI / 6.0
	original = true
	native_proportions = true
	proportions.set_pressed_no_signal(true)
	refresh()
	focus_selected()
	zoom = 1.0

func _update_camera() -> void:
	if not is_instance_valid(camera):
		return
	var direction := Vector3(cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch))
	camera.position = target + direction * 256.0
	camera.look_at(target, Vector3.UP)
	# Corte da câmera apenas para inspecionar as faces reais da sala. A malha
	# editável e as luzes do snapshot permanecem intactas.
	camera.near = 256.0 if interior_only and display_mode == 1 else 0.05
	camera.size = maxf(2.0, float(viewport.size.y) / NATIVE_FOCAL * zoom)

func _input_preview(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			dragging = event.pressed
		elif event.button_index == MOUSE_BUTTON_MIDDLE:
			panning = event.pressed
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
			zoom = maxf(0.12, zoom / 1.12)
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			zoom = minf(20.0, zoom * 1.12)
		elif event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
			_pick(event.position)
		canvas.accept_event()
	elif event is InputEventMouseMotion:
		if dragging:
			yaw -= event.relative.x * 0.008
			pitch = clampf(pitch + event.relative.y * 0.006, 0.1, 1.5)
			original = false
			canvas.accept_event()
		elif panning:
			var units := camera.size / maxf(1.0, viewport.size.y)
			target -= camera.global_basis.x * event.relative.x * units
			target += camera.global_basis.y * event.relative.y * units
			canvas.accept_event()
	elif event is InputEventKey and event.pressed:
		if event.keycode == KEY_HOME:
			reset_camera()
			canvas.accept_event()
		elif event.keycode == KEY_F:
			focus_selected()
			canvas.accept_event()

func _pick(at: Vector2) -> void:
	if not is_instance_valid(clone):
		return
	var from := camera.project_ray_origin(at)
	var direction := camera.project_ray_normal(at)
	var best := INF
	var best_id := ""
	var architecture := clone.get_node_or_null("Architecture")
	if architecture == null:
		return
	for instance in architecture.get_children():
		if not instance is Node3D or not instance.has_meta("d3d_instance"):
			continue
		for visual in _meshes(instance):
			if visual.mesh == null or not visual.is_visible_in_tree():
				continue
			var inv := visual.global_transform.affine_inverse()
			var local_from := inv * from
			var local_direction := inv.basis * direction
			if visual.get_aabb().intersects_ray(local_from, local_direction) == null:
				continue
			var faces := visual.mesh.get_faces()
			for offset in range(0, faces.size(), 3):
				var hit: Variant = Geometry3D.ray_intersects_triangle(local_from, local_direction, faces[offset], faces[offset + 1], faces[offset + 2])
				if hit == null:
					continue
				var distance: float = from.distance_to(visual.global_transform * hit)
				if distance < best:
					best = distance
					best_id = str(instance.get_meta("d3d_instance").get("instanceId", ""))
	if not best_id.is_empty():
		select_instance(best_id)
		instance_selected.emit(best_id)
