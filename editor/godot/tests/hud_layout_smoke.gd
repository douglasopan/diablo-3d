extends SceneTree
## Layout/export integration only. No game assets, profile, save or C++ build.

const HUD := preload("res://ui/hud.gd")
const SCENE := preload("res://ui/hud.tscn")
const OUTPUT := "res://local/hud-layout-tests/layout.ini"
const DEFAULTS := {
	"HudHealthOrb": [-226, -122, 88, 113], "HudManaOrb": [138, -122, 88, 113],
	"HudBelt": [-116, -103, 232, 29], "HudSpell": [252, -58, 44, 44],
	"HudInfo": [-132, -72, 264, 64],
	"HudCharacter": [-310, -106, 71, 20], "HudInventory": [239, -106, 71, 20],
	"HudSpellbook": [239, -82, 71, 20], "HudQuests": [-310, -82, 71, 20],
	"HudMap": [-310, -54, 71, 20], "HudMenu": [-310, -30, 71, 20],
	"HudChat": [-199, -150, 33, 24], "HudFriendly": [166, -150, 33, 24],
}

var checks := 0
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, description: String) -> void:
	checks += 1
	if not condition:
		failures.append(description)
		push_error(description)

func _layout(config: ConfigFile, section: String) -> Dictionary:
	var values := {}
	for key: String in config.get_section_keys(section):
		values[key] = config.get_value(section, key)
	return values

func _run() -> void:
	var hud := SCENE.instantiate()
	root.add_child(hud)
	await process_frame
	hud.scale = Vector2.ONE
	_check(not hud.get_element("HudChat").visible and not hud.get_element("HudFriendly").visible, "Prévia single-player esconde controles multiplayer")
	hud.demo_multiplayer = true
	_check(hud.get_element("HudChat").visible and hud.get_element("HudFriendly").visible, "Inspector Demo Multiplayer mostra os controles condicionais")
	_check(hud.collect_layout().config.get_sections().size() == 13, "Visibilidade multiplayer não altera o contrato de 13 retângulos originais")
	hud.demo_multiplayer = false
	_check(not hud.get_element("HudChat").visible and not hud.get_element("HudFriendly").visible, "Desligar Demo Multiplayer restaura a prévia single-player")
	_check(hud.get_element("HudMap").tooltip_text.contains("não implementa um minimapa"), "Automapa nativo é identificado sem prometer minimapa")
	_check(hud.get_element("Missing") == null, "Nome fora do contrato não é resolvido")
	for element_name: String in HUD.RETIRED_ELEMENTS:
		_check(hud.get_element(element_name) == null and hud.get_node_or_null("SafeFrame/" + element_name) == null, "Botão extra removido da cena e do contrato: " + element_name)
	for element_name: String in ["HudCharacter", "HudQuests", "HudMap", "HudMenu", "HudInventory", "HudSpellbook"]:
		_check(HUD.BUTTON_TEXT[element_name].length() > 2, "Botão mostra seu nome funcional em vez de somente a tecla: " + element_name)
	for logical_width: int in [640, 720, 800, 843, 844, 853, 854, 1120, 1706]:
		hud.set_logical_size(Vector2i(logical_width, 480))
		await process_frame
		var report: Dictionary = hud.collect_layout()
		_check(report.ok, "Control reais exportam em largura lógica %d" % logical_width)
		if not report.ok:
			continue
		var config: ConfigFile = report.config
		var safe_width := mini(logical_width, 853)
		var frame: Control = hud.get_node("SafeFrame")
		_check(frame.size == Vector2(safe_width, 480), "SafeFrame limitado a 16:9 em %d" % logical_width)
		_check(frame.position.x == (logical_width - safe_width) / 2, "SafeFrame centralizado por divisão inteira em %d" % logical_width)
		var rects := {}
		for element_name: String in DEFAULTS:
			var layout := _layout(config, element_name)
			var expected: Array = DEFAULTS[element_name]
			_check([layout.offsetX, layout.offsetY, layout.width, layout.height] == expected, "Defaults preservados sem translação compacta: %s / %d" % [element_name, logical_width])
			_check(layout.anchor == "bottom-center", "Âncora nativa exportada: " + element_name)
			var rect := HUD.resolve_layout_rect(layout, Vector2i(safe_width, 480))
			_check(Rect2i(0, 0, safe_width, 480).encloses(rect), "Retângulo visível: %s / %d" % [element_name, logical_width])
			var actual: Transform2D = frame.get_global_transform().affine_inverse() * hud.get_element(element_name).get_global_transform()
			_check((actual.origin + Vector2.ONE * 0.0001).floor() == Vector2(rect.position) and hud.get_element(element_name).size == Vector2(rect.size), "Control corresponde ao retângulo inteiro nativo: %s / %d" % [element_name, logical_width])
			rects[element_name] = rect
		for first: int in range(HUD.ELEMENT_NAMES.size()):
			for second: int in range(first + 1, HUD.ELEMENT_NAMES.size()):
				var first_name: String = HUD.ELEMENT_NAMES[first]
				var second_name: String = HUD.ELEMENT_NAMES[second]
				_check(not rects[first_name].intersects(rects[second_name]), "Sem colisão entre controles originais: %s/%s/%d" % [first_name, second_name, logical_width])
		_check(rects.HudBelt.end.y + 2 == rects.HudInfo.position.y, "Cinto fica acima da informação, com intervalo2: %d" % logical_width)
		_check(rects.HudBelt.get_center().x == rects.HudInfo.get_center().x, "Cinto e informação compartilham o centro: %d" % logical_width)
		_check(rects.HudSpell.position.y >= rects.HudSpellbook.end.y + 4, "Magia preparada fica abaixo do livro: %d" % logical_width)
		_check(rects.HudChat.end.y + 4 == rects.HudHealthOrb.position.y and rects.HudFriendly.end.y + 4 == rects.HudManaOrb.position.y, "Controles MP ficam acima dos globos sem cobrir a faixa: %d" % logical_width)
		var plate: Rect2i = hud.get_panel_preview_rect()
		_check(plate.size == Vector2i(640, 104) and plate.position.y == 368, "Faixa de fundo640×104 tem rodapé8: %d" % logical_width)
		plate.position -= Vector2i(frame.position)
		_check(rects.HudHealthOrb.position.y == plate.position.y - 10 and rects.HudManaOrb.position.y == plate.position.y - 10 and rects.HudHealthOrb.end.y <= plate.end.y and rects.HudManaOrb.end.y <= plate.end.y, "Colunas dos globos sobressaem10 sem recortar as esculturas: %d" % logical_width)
		for element_name: String in HUD.ELEMENT_NAMES:
			if element_name not in ["HudChat", "HudFriendly", "HudHealthOrb", "HudManaOrb"]:
				_check(plate.encloses(rects[element_name]), "Faixa contém todo o controle de jogador único: %s/%d" % [element_name, logical_width])
	# Defaults are read from the C++ contract to catch drift between languages.
	var source := FileAccess.get_file_as_string("res://../../Source/control/d3d_hud_layout.hpp")
	_check(source.contains("std::array<D3dHudLayoutEntry, 13>"), "Contrato C++ possui somente os 13 controles originais")
	_check(not source.contains('"HudQuickSpell'), "Contrato C++ não recria os quatro botões extras")
	for element_name: String in DEFAULTS:
		var values: Array = DEFAULTS[element_name]
		var entry := '"%s", false, %d, %d, %d, %d' % [element_name, values[0], values[1], values[2], values[3]]
		_check(source.contains(entry), "Cena e contrato C++ concordam: " + element_name)
	for physical: Vector2i in [Vector2i(640, 480), Vector2i(800, 600), Vector2i(1280, 720), Vector2i(1920, 1080), Vector2i(2560, 1080), Vector2i(3440, 1440)]:
		var logical_width := physical.x * 480 / physical.y
		var safe_width := mini(logical_width, 853)
		hud.set_logical_size(Vector2i(logical_width, 480))
		var config: ConfigFile = hud.collect_layout().config
		for element_name: String in DEFAULTS:
			var rect := HUD.resolve_layout_rect(_layout(config, element_name), Vector2i(safe_width, 480))
			rect.position.x += (logical_width - safe_width) / 2
			var left := rect.position.x * physical.y / 480
			var right := rect.end.x * physical.y / 480
			var top := rect.position.y * physical.y / 480
			var bottom := rect.end.y * physical.y / 480
			_check(Rect2i(Vector2i.ZERO, physical).encloses(Rect2i(left, top, right - left, bottom - top)), "Escala proporcional cabe: %s / %s" % [element_name, physical])
			if element_name == "HudBelt":
				var last := 0
				for slot: int in range(8):
					var slot_left := (right - left) * slot / 8
					var slot_right := (right - left) * (slot + 1) / 8
					_check(slot_left == last and slot_right > slot_left, "Oito slots sem lacuna / %s / %d" % [physical, slot])
					last = slot_right
				_check(last == right - left, "Oito slots cobrem o cinto / %s" % physical)
	# Scene save/reopen retains native edits instead of reapplying defaults.
	hud.set_logical_size(Vector2i(640, 480))
	var edited: Control = hud.get_element("HudInventory")
	edited.offset_left -= 5
	edited.offset_right -= 5
	var packed := PackedScene.new()
	_check(packed.pack(hud) == OK, "Cena com Control editado pode ser empacotada")
	var reopened := packed.instantiate()
	root.add_child(reopened)
	await process_frame
	_check(reopened.collect_layout().config.get_value("HudInventory", "offsetX") == 234, "Edição nativa sobrevive ao reabrir sem translação compacta")
	reopened.free()
	_check(hud.collect_layout().config.get_value("HudInventory", "offsetY") == -106, "Export preserva offset nativo sem apresentação adicional")
	hud.demo_life_percent = -4
	hud.demo_mana_percent = 110
	_check(hud.demo_life_percent == 0 and hud.demo_mana_percent == 100, "Vida/mana demonstrativas respeitam 0..100")
	var click_count := [0]
	hud.preview_area_clicked.connect(func(_name: String): click_count[0] += 1)
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	hud._element_input(click, "HudInventory")
	_check(click_count[0] == 1 and hud.status.contains("só demonstra"), "Clique identifica área sem fingir ação do jogo")
	var foreign := "[MenuLogo]\nanchor=center\noffsetX=-290\noffsetY=-240\nwidth=580\nheight=154\n; manter comentário\n\n[MenuList]\nanchor=center\noffsetX=-255\noffsetY=-48\nwidth=510\nheight=258\n\n[AnotherOwner]\nanchor=top-left\noffsetX=4\noffsetY=5\nwidth=6\nheight=7\n"
	var legacy := ""
	for element_name: String in HUD.RETIRED_ELEMENTS:
		legacy += "\n[%s]\nanchor=bottom-center\noffsetX=-116\noffsetY=-89\nwidth=38\nheight=38\n" % element_name
	_write("[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n\n" + foreign + legacy)
	var result: Dictionary = hud.export_layout(OUTPUT)
	_check(result.ok and result.menu_available and result.elements == 13, "Exportação compartilhada dos 13 controles originais passa")
	var exported := FileAccess.get_file_as_string(OUTPUT)
	_check(exported.contains(foreign), "Seções alheias, ordem e comentários preservados verbatim")
	_check(not exported.contains("[HudQuickSpell"), "Export elimina os blocos legados dos quatro botões extras")
	_check(exported.contains("format=d3d.ui-layout") and not exported.contains('"bottom-'), "Export INI usa strings cruas compatíveis com loader C++")
	var parsed: Dictionary = HUD._read_ini(exported)
	_check(parsed.ok and parsed.config.get_sections().size() == 17, "Export possui header, 13 HUD originais e três seções preservadas")
	_check(parsed.config.get_value("HudInventory", "offsetX") == 234, "Export contém a edição visual real")
	_check(FileAccess.get_file_as_string(OUTPUT + ".previous").contains(foreign), "Versão anterior preservada no backup")
	var invalid := "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n[MenuList]\nanchor=center\n[MenuList]\nwidth=510\n"
	_write(invalid)
	_check(not hud.export_layout(OUTPUT).ok and FileAccess.get_file_as_string(OUTPUT) == invalid, "Seção duplicada é recusada sem perder arquivo existente")
	_write("[Layout]\nformat=d3d.ui-layout\nschemaVersion=99\n")
	_check(not hud.export_layout(OUTPUT).ok, "Schema desconhecido é preservado e recusado")
	_check(not hud.export_layout("res://ui/layout.ini").ok, "Export não instala fora de local/")
	_check(not hud.export_layout("res://local/../../layout.ini").ok, "Travessia de diretório recusada")
	_check(not HUD._read_ini("[X]\na=1\na=2\n").ok, "Chave duplicada recusada")
	var nul_file := FileAccess.open(OUTPUT, FileAccess.WRITE)
	nul_file.store_buffer(PackedByteArray([91, 88, 93, 10, 97, 61, 0]))
	nul_file.close()
	_check(not hud.export_layout(OUTPUT).ok, "NUL recusado como no loader")
	var anchor_before := edited.anchor_right
	edited.anchor_right = 0.8
	_check(not hud.collect_layout().ok, "Âncora esticada não exporta uma semântica diferente")
	edited.anchor_right = anchor_before
	edited.anchor_left = anchor_before
	edited.rotation = 0.1
	_check(not hud.collect_layout().ok, "Rotação sem representação no contrato recusada")
	edited.rotation = 0
	DirAccess.remove_absolute(ProjectSettings.globalize_path(OUTPUT))
	result = hud.export_layout(OUTPUT)
	_check(result.ok and result.get("menu_available", false), "Primeiro export HUD inclui baseline de menu somente por leitura: %s" % result)
	var resolved := HUD.resolve_layout_rect({"anchor": "bottom-right", "offsetX": 999, "offsetY": -999, "width": 999, "height": 999}, Vector2i(640, 480))
	_check(resolved == Rect2i(0, 0, 640, 480), "Clipping de override coincide com o loader nativo")
	hud.free()
	var report := {"checks": checks, "failures": failures, "scope": "Godot HUD logical layout and local export; no in-game/visual approval"}
	var file := FileAccess.open("res://local/hud-layout-tests/results.json", FileAccess.WRITE)
	file.store_string(JSON.stringify(report, "\t"))
	file.close()
	print("HUD_LAYOUT_SMOKE: %d checks; %d failures" % [checks, failures.size()])
	quit(0 if failures.is_empty() else 1)

func _write(text: String) -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUTPUT).get_base_dir())
	var file := FileAccess.open(OUTPUT, FileAccess.WRITE)
	file.store_string(text)
	file.close()
