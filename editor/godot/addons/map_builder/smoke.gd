extends SceneTree
## Run only in an isolated project copy. Reads a supplied receipt; never writes profiles.

const Session := preload("session.gd")
const Snapshot := preload("res://addons/tristram_editor/snapshot.gd")
const Preview := preload("res://addons/tristram_editor/preview.gd")

var checks := 0
var failures: Array[String] = []
var receipt_path := ""
var current_snapshot := ""
var output := "res://local/map-builder-tests"

func _initialize() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--receipt="):
			receipt_path = argument.trim_prefix("--receipt=")
		if argument.begins_with("--current-snapshot="):
			current_snapshot = argument.trim_prefix("--current-snapshot=")
	call_deferred("_run")

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
		push_error(message)

func _write_json(path: String, value: Variant) -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path).get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(value, "\t"))
	file.close()

func _run() -> void:
	if not ProjectSettings.globalize_path("res://").replace("\\", "/").contains("/diagnostics/tristram-map-builder-20261008/tool/"):
		push_error("SMOKE exige cópia privada diagnostics/.../tool; projeto compartilhado recusado.")
		quit(2)
		return
	var snapshot_path := ProjectSettings.globalize_path("res://local/town-snapshot.json")
	var snapshot_before := FileAccess.get_sha256(snapshot_path)
	var saved_before := FileAccess.get_sha256("res://local/tristram.tscn")
	var built: Dictionary = Snapshot.new().build(snapshot_path)
	_check(built.get("ok", false), "Snapshot existente carrega na cópia privada: %s" % [built.get("errors", [])])
	if not built.get("ok", false):
		_finish({})
		return
	var scene: Node3D = built.root
	root.add_child(scene)
	var session := Session.new()
	session.capture_base(scene)
	_check(Session.instances(scene).size() == 14, "IDs existentes: 14 grupos de arquitetura, sem catálogo novo")
	var tavern := Session.find_instance(scene, "tavern-main")
	var cabin := Session.find_instance(scene, "cabin-east")
	_check(tavern != null and cabin != null, "Seleção por IDs conhecidos")
	_check(not Session.editable(cabin), "Cabana leste congelada")
	_check(not Session.transform_for(cabin, cabin.position, Vector3.ZERO, 1.0).ok, "Transformar cabana congelada é recusado")
	_check(not Session.transform_for(tavern, Vector3(INF, 0, 0), Vector3.ZERO, 1.0).ok, "Posição não finita é recusada")
	_check(not Session.transform_for(tavern, tavern.position, Vector3.ZERO, -1.0).ok, "Escala negativa é recusada")
	_check(not Session.transform_for(tavern, tavern.position, Vector3.ZERO, 101.0).ok, "Escala fora da faixa é recusada")
	var original := tavern.transform
	var native_identity: Dictionary = tavern.get_meta("d3d_instance").duplicate(true)
	var original_sha := Session.assembly_sha(tavern)
	var architecture := tavern.get_parent() as Node3D
	var architecture_original := architecture.transform
	architecture.position.x += 0.5
	_check(Session.assembly_sha(tavern) != original_sha, "Transformação ancestral invalida hash da montagem")
	architecture.scale = Vector3(1, 1.5, 1)
	_check(not session.save_proposal(scene, tavern, "r1", "", "pending", "", "scene", output).ok, "Escala anisotrópica ancestral é recusada")
	architecture.transform = architecture_original
	var surface := tavern.get_child(0) as MeshInstance3D
	var initial_override := surface.material_override
	var textured := StandardMaterial3D.new()
	var bytes := PackedByteArray([1, 2, 3, 4, 5, 6])
	textured.albedo_texture = ImageTexture.create_from_image(Image.create_from_data(2, 1, false, Image.FORMAT_RGB8, bytes))
	surface.material_override = textured
	var first_texture_sha := Session.assembly_sha(tavern)
	textured.albedo_texture = ImageTexture.create_from_image(Image.create_from_data(1, 2, false, Image.FORMAT_RGB8, bytes))
	_check(Session.assembly_sha(tavern) != first_texture_sha, "Dimensões da textura invalidam hash mesmo com bytes RGB iguais")
	surface.material_override = initial_override
	var computed := Session.transform_for(tavern, tavern.position + Vector3(1, 0.25, -0.5), Vector3(0, 15, 0), 1.1)
	_check(computed.ok, "Posição/rotação/escala uniforme válidas")
	tavern.transform = computed.transform
	_check(tavern.scale.is_equal_approx(Vector3.ONE * 1.1), "Transformação conserva escala uniforme")
	_check(tavern.get_meta("d3d_instance") == native_identity, "Transformação visual preserva vínculo/limites nativos")
	_check(Session.assembly_sha(tavern) != original_sha, "Hash da montagem muda com transformação")
	var proposal := session.save_proposal(scene, tavern, "smoke-placement-r1", "Fixture técnica; nenhuma aprovação humana.", "pending", "", "scene", output.path_join("proposals"))
	_check(proposal.ok, "Proposta candidata salva em diretório privado")
	if proposal.ok:
		var document: Dictionary = proposal.document
		_check(document.state == "candidate" and not document.installed and not document.exported, "Salvar não aplica/exporta/aprova")
		_check(document.review.state == "pending" and not document.review.catalogAcceptanceModified, "Aceitação no catálogo preservada")
		_check(document.collision.state == "native-fixed" and document.collision.visualTransformDiverges and not document.collision.simulationModified, "Divergência visual/colisão registrada")
		_check(document.base.assemblySha256 == original_sha, "Base da proposta corresponde à montagem anterior à edição")
		_check(FileAccess.get_sha256(proposal.path) == proposal.sha256, "SHA da proposta corresponde aos bytes gravados")
	_check(not bool(tavern.get_meta("d3d_override", true)), "Salvar proposta não marca override")
	_check(not session.save_proposal(scene, tavern, "invalid/revision", "", "pending", "", "scene", output).ok, "Revisão inválida é recusada")
	_check(not session.save_proposal(scene, tavern, "r1", "", "assembly-approved", "", "scene", output).ok, "Aprovação sem responsável é recusada")
	_check(not session.save_proposal(scene, tavern, "r1", "", "assembly-approved", "fixture", "scene", output).ok, "Aprovação sem escopo/evidência é recusada")
	_check(not session.save_proposal(scene, tavern, "r1", "", "pending", "", "original", output).ok, "Original é somente leitura")
	tavern.scale = Vector3(1, 1.1, 1)
	_check(not session.save_proposal(scene, tavern, "r1", "", "pending", "", "scene", output).ok, "Escala anisotrópica é recusada na proposta")
	tavern.transform = Session.baseline(tavern)
	_check(tavern.transform.is_equal_approx(original), "Restaurar retorna exatamente à transformação original")
	_check(Session.assembly_sha(tavern) == original_sha, "Restaurar recupera hash da montagem")
	var box := Session.bounds(tavern)
	_check(box.available and box.triangles > 0 and is_zero_approx(box.groundGap), "Limites/triângulos/contato com o chão do snapshot real")
	# Reuse the actual preview class, never implement a second viewport.
	var preview := Preview.new()
	root.add_child(preview)
	preview.set_source(scene)
	preview.select_instance("tavern-main")
	preview.focus_selected()
	_check(preview.clone != null and preview.selected_id == "tavern-main", "Prévia existente seleciona/enquadra taverna")
	_check(tavern.transform.is_equal_approx(original), "Escala auxiliar da prévia não altera cena")
	var summary: Dictionary = {"oldSnapshotSha256": snapshot_before, "proposal": proposal.get("path", "")}
	if not receipt_path.is_empty() and not current_snapshot.is_empty():
		var receipt_before := FileAccess.get_sha256(receipt_path)
		var verified := session.read_application(receipt_path, scene)
		_check(verified.get("ok", false), "Recibo aplicado atual confere hashes/vínculos: %s" % verified)
		_check(verified.get("bindings", 0) == 12, "Recibo atual tem 12 vínculos, sem seleção por data")
		if verified.get("ok", false):
			var applied: Dictionary = session.applied_preview(current_snapshot)
			_check(applied.get("ok", false), "Snapshot privado aplicado atual monta prévia em memória: %s" % [applied.get("errors", [])])
			if applied.get("ok", false):
				var applied_root: Node3D = applied.root
				root.add_child(applied_root)
				var actual := Session.find_instance(applied_root, "tavern-main")
				var wing := Session.find_instance(applied_root, "tavern-wing")
				_check(actual.get_meta("d3d_instance").sha256 == "11cf73e7ea0b2779e8a6c8b989ea5ad0c062c518ece1e2674bc776d2093d88a0", "Taverna main aplicada identificada pelo SHA real")
				_check(wing.get_meta("d3d_instance").sha256 == "a0adba330232fcd785e0640dcc76a11067ace1347cf326eac6870def0c7efc62", "Taverna wing aplicada identificada pelo SHA real")
				_check(Session.bounds(actual).triangles == 7361 and Session.bounds(wing).triangles == 4081, "Partições main 7361 + wing 4081; não repete master inteiro")
				_check(applied_root.get_meta("d3d_snapshot_sha256") != snapshot_before, "Snapshot antigo é comprovadamente diferente do aplicado atual")
				preview.set_source(applied_root)
				preview.select_instance("tavern-main")
				preview.focus_selected()
				_check(preview.clone.get_node("Architecture/tavern-main/AppliedD3D") != null, "Mesma prévia enquadra bytes D3D aplicados")
				var applied_proposal := session.save_proposal(applied_root, actual, "smoke-applied-base-r1", "Identificação técnica; revisão humana pendente.", "pending", "", "applied", output.path_join("proposals"))
				_check(applied_proposal.ok, "Proposta aplicada conserva recibo, hashes e estado candidato")
				if applied_proposal.ok:
					_check(applied_proposal.document.application.receiptSha256 == receipt_before, "Proposta liga recibo real por hash")
					_check(applied_proposal.document.identity.sha256 == session.application.bindings["tavern-main"].sha256, "Proposta liga o arquivo realmente aplicado")
				var stale_evidence: Dictionary = applied_root.get_meta("map_builder_application").duplicate(true)
				stale_evidence.manifestSha256 = "0".repeat(64)
				applied_root.set_meta("map_builder_application", stale_evidence)
				_check(not session.save_proposal(applied_root, actual, "stale-must-fail", "", "pending", "", "applied", output).ok, "Salvar aplicado revalida manifesto/recibo e recusa evidência stale")
				applied_root.set_meta("map_builder_application", session.application.duplicate(true))
				summary["appliedProposal"] = applied_proposal.get("path", "")
				summary["appliedSnapshotSha256"] = applied_root.get_meta("d3d_snapshot_sha256")
				preview.set_source(scene)
				applied_root.free()
			var receipt: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(receipt_path))
			var corrupt: Dictionary = receipt.duplicate(true)
			corrupt.manifestSha256 = "0".repeat(64)
			var corrupt_path := output.path_join("bad-receipt.json")
			_write_json(corrupt_path, corrupt)
			_check(not session.read_application(corrupt_path, scene).ok and session.application.is_empty(), "Recibo divergente recusa/limpa estado aplicado")
			var traversal_path := output.path_join("traversal/receipt.json")
			DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(traversal_path).get_base_dir().path_join("d3d-maps"))
			DirAccess.copy_absolute(receipt_path.get_base_dir().path_join("d3d-maps/tristram.ini"), ProjectSettings.globalize_path(traversal_path).get_base_dir().path_join("d3d-maps/tristram.ini"))
			corrupt = receipt.duplicate(true)
			corrupt.models[0].path = "d3d-models/editor/../../../outside.d3d"
			_write_json(traversal_path, corrupt)
			_check(not session.read_application(traversal_path, scene).ok and session.application.is_empty(), "Path traversal no recibo é recusado")
			corrupt = receipt.duplicate(true)
			corrupt.models.pop_back()
			_write_json(traversal_path, corrupt)
			_check(not session.read_application(traversal_path, scene).ok and session.application.is_empty(), "Cobertura parcial do recibo não assume aplicação do conjunto")
		_check(FileAccess.get_sha256(receipt_path) == receipt_before, "Recibo/perfil aplicado permaneceu byte a byte")
	else:
		_check(false, "Forneça --receipt e --current-snapshot para verificar a aplicação real")
	_check(FileAccess.get_sha256(snapshot_path) == snapshot_before, "Snapshot existente preservado byte a byte")
	_check(FileAccess.get_sha256("res://local/tristram.tscn") == saved_before, "Cena existente preservada byte a byte")
	preview.set_source(null)
	preview.queue_free()
	scene.queue_free()
	await process_frame
	await process_frame
	_finish(summary)

func _finish(summary: Dictionary) -> void:
	summary["checks"] = checks
	summary["failures"] = failures
	summary["ok"] = failures.is_empty()
	summary["scope"] = "Godot headless em cópia privada; sem build, perfil, aprovação humana ou inspeção física da UI"
	_write_json(output.path_join("report.json"), summary)
	print("MAP_BUILDER_SMOKE: %d verificações, %d falhas" % [checks, failures.size()])
	quit(0 if failures.is_empty() else 1)
