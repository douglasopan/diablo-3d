// Prepared only. Existing ConfigFixture owns all file writes; no live profile.
void CheckMovementKeymappingR2(const ConfigFixture &fixture)
{
	auto &mapper = GetOptions().Keymapper;
	auto &forward = MovementSetting("TownMoveForward");
	mapper.RestoreMovementDefaults();
	std::vector<std::pair<std::string, uint32_t>> nativeSnapshot;
	std::vector<KeymapperOptions::Action *> reserved;
	for (auto *entry : mapper.GetEntries()) {
		auto &action = static_cast<KeymapperOptions::Action &>(*entry);
		if (action.context != KeymapperContext::Native) continue;
		nativeSnapshot.emplace_back(action.key, mapper.KeyForAction(action.key));
		if (action.key.starts_with("Town3D") || action.key == "ToggleTown3D"
			|| action.key.starts_with("PauseGame") || action.key == "Screenshot" || action.key == "OpenConsole")
			reserved.push_back(&action);
	}
	Check(!reserved.empty(), "fixture identifies existing native reserved families without inventing actions");
	CloseInGameSettings();
	OpenCategory(mapper);
	for (auto *action : reserved) {
		const auto previous = mapper.KeyForAction(action->key);
		Check(!action->SetValue('W') && mapper.KeyForAction(action->key) == previous
			&& mapper.KeyForAction(forward.key) == 'W'
			&& mapper.findAction('W', KeymapperContext::TownMovement) == &forward,
			"every reserved native action rejects a movement-owned key before changing either map: " + std::string(action->key));
		Check(action->BindingError('W').find(std::string(forward.GetName())) != std::string::npos,
			"reserved-to-movement conflict names the movement occupant");
		ActivateNamed(action->GetName());
		ActivateNamed(_("Bind key"));
		SendKey(SDLK_W); SendKey(SDLK_W, true);
		Check(mapper.KeyForAction(action->key) == previous && FindHere(_("Press any key to change.")).has_value(),
			"real capture refuses each reserved native takeover and remains cancellable");
		SendKey(SDLK_ESCAPE); SendKey(SDLK_ESCAPE, true);
		Back();
	}
	CloseInGameSettings();
	Check(DelegatedEvents == 0, "reserved native key captures never leak events to gameplay");
	for (const auto &[id, key] : nativeSnapshot)
		Check(mapper.KeyForAction(id) == key, "all native bindings survive reserved takeover attempts");

	// Multiple unavailable defaults: snapshot AFTER intentionally assigning reservations.
	mapper.ClearMovementBindings();
	const std::array<std::pair<std::string_view, uint32_t>, 4> occupied {{
		{ "Town3DCameraMode", 'W' }, { "ToggleTown3D", 'A' },
		{ "PauseGame", 'D' }, { "Screenshot", SDLK_UP }
	}};
	for (const auto &[id, key] : occupied)
		Check(MovementSetting(id).SetValue(key), "explicitly reserve a freed default for native action");
	std::vector<std::pair<std::string, uint32_t>> afterReservation;
	for (auto *entry : mapper.GetEntries()) {
		auto &action = static_cast<KeymapperOptions::Action &>(*entry);
		if (action.context == KeymapperContext::Native)
			afterReservation.emplace_back(action.key, mapper.KeyForAction(action.key));
	}
	const std::array<std::string_view, 4> unavailable {{
		"TownMoveForward", "TownMoveLeft", "TownMoveRight", "TownMoveForwardAlternate"
	}};
	for (unsigned repeat = 0; repeat < 2; ++repeat) {
		const auto notice = mapper.RestoreMovementDefaults();
		for (const auto id : unavailable)
			Check(mapper.KeyForAction(id) == SDLK_UNKNOWN
				&& notice.find(std::string(MovementSetting(id).GetName())) != std::string::npos,
				"partial reset names each unavailable default without overwriting any native reservation");
		Check(mapper.KeyForAction("TownMoveBackward") == 'S'
			&& mapper.KeyForAction("TownMoveBackwardAlternate") == SDLK_DOWN
			&& mapper.KeyForAction("TownMoveLeftAlternate") == SDLK_LEFT
			&& mapper.KeyForAction("TownMoveRightAlternate") == SDLK_RIGHT,
			"partial reset restores all independently available slots");
		for (const auto &[id, key] : afterReservation)
			Check(mapper.KeyForAction(id) == key, "repeated partial reset preserves every post-reservation native binding");
	}
	// Batch release avoids incidental conflicts while restoring the fixture itself.
	for (auto *entry : mapper.GetEntries()) {
		auto &action = static_cast<KeymapperOptions::Action &>(*entry);
		if (action.context == KeymapperContext::Native) action.SetValue(SDLK_UNKNOWN);
	}
	for (const auto &[id, key] : nativeSnapshot)
		Check(MovementSetting(id).SetValue(key), "restore exact pre-fixture native binding snapshot");
	mapper.RestoreMovementDefaults();
	SaveOptions();
	std::ifstream saved(fixture.iniPath, std::ios::binary);
	const std::string original { std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>() };
	saved.close();
	const auto loadCase = [&](auto edit) {
		auto parsed = Ini::parse(original);
		Check(parsed.has_value(), "saved isolated configuration is parseable");
		edit(*parsed);
		std::ofstream out(fixture.iniPath, std::ios::binary | std::ios::trunc);
		out << parsed->serialize(); out.close();
		Check(out.good(), "write only fixture-owned INI for malformed/conflicting key tests");
		LoadOptions();
	};
	const auto forwardKey = std::string(forward.key);
	loadCase([&](Ini &ini) { ini.set("Keymapping", forwardKey, Ini::Values {}); });
	Check(mapper.KeyForAction(forward.key) == 'W', "absent movement entry uses its default");
	loadCase([&](Ini &ini) { ini.set("Keymapping", forwardKey, ""); });
	Check(mapper.KeyForAction(forward.key) == SDLK_UNKNOWN, "explicit empty movement entry remains unbound");
	loadCase([&](Ini &ini) { ini.set("Keymapping", forwardKey, "NOT_A_KEY"); });
	Check(mapper.KeyForAction(forward.key) == 'W', "unknown INI key token uses the native default policy");
	for (const auto token : { "MMOUSE", "F4" }) {
		loadCase([&](Ini &ini) { ini.set("Keymapping", forwardKey, token); });
		Check(mapper.KeyForAction(forward.key) == SDLK_UNKNOWN
			&& mapper.KeyForAction("ToggleTown3D") == SDLK_F4,
			"recognized but prohibited/reserved INI token stays unbound without stealing the native shortcut");
	}
	loadCase([&](Ini &ini) {
		Ini::Values duplicate { Ini::Value { std::nullopt, "T" } };
		duplicate.append({ std::nullopt, "G" });
		ini.set("Keymapping", forwardKey, std::move(duplicate));
	});
	Check(mapper.KeyForAction(forward.key) == 'G', "duplicate action ID uses the last INI value");
	loadCase([&](Ini &ini) {
		Ini::Values duplicate { Ini::Value { std::nullopt, "T" } };
		duplicate.append({ std::nullopt, "" });
		ini.set("Keymapping", forwardKey, std::move(duplicate));
	});
	Check(mapper.KeyForAction(forward.key) == SDLK_UNKNOWN, "last duplicate empty entry explicitly unbinds the action");
	for (bool reverse : { false, true }) {
		loadCase([&](Ini &ini) {
			// Delete/reinsert fixes input line order independently of registration order.
			ini.set("Keymapping", "TownMoveForward", Ini::Values {});
			ini.set("Keymapping", "TownMoveBackward", Ini::Values {});
			if (reverse) ini.set("Keymapping", "TownMoveBackward", "T");
			ini.set("Keymapping", "TownMoveForward", "T");
			if (!reverse) ini.set("Keymapping", "TownMoveBackward", "T");
		});
		Check(mapper.KeyForAction("TownMoveForward") == 'T' && mapper.KeyForAction("TownMoveBackward") == SDLK_UNKNOWN,
			"movement collision uses registration priority regardless of INI line order");
		LoadOptions();
		Check(mapper.KeyForAction("TownMoveForward") == 'T' && mapper.KeyForAction("TownMoveBackward") == SDLK_UNKNOWN,
			"reloading a conflicting INI is stable and cannot inherit old in-memory bindings");
		SaveOptions(); LoadOptions();
		Check(mapper.KeyForAction("TownMoveForward") == 'T' && mapper.KeyForAction("TownMoveBackward") == SDLK_UNKNOWN,
			"saving/reloading serializes the rejected movement binding as explicitly empty");
	}
	loadCase([&](Ini &ini) {
		ini.set("Keymapping", InventoryKey().key, "T");
		ini.set("Keymapping", forwardKey, "T");
	});
	Check(mapper.KeyForAction(InventoryKey().key) == 'T' && mapper.KeyForAction(forward.key) == 'T',
		"ordinary native and movement shortcuts coexist through INI loading");
	loadCase([&](Ini &ini) {
		ini.set("Keymapping", "ToggleTown3D", "T");
		ini.set("Keymapping", forwardKey, "T");
	});
	Check(mapper.KeyForAction("ToggleTown3D") == 'T' && mapper.KeyForAction(forward.key) == SDLK_UNKNOWN,
		"a reserved native INI binding takes precedence without silent movement fallback");
	std::ofstream restore(fixture.iniPath, std::ios::binary | std::ios::trunc);
	restore << original; restore.close();
	Check(restore.good(), "restore exact isolated INI bytes after extended keymapping checks");
	LoadOptions();
	for (const auto &[id, key] : nativeSnapshot)
		Check(mapper.KeyForAction(id) == key, "extended INI fixtures leave original native bindings intact");
	Check(mapper.KeyForAction("TownMoveForward") == 'W' && mapper.KeyForAction("DisplaySpells") == 'S',
		"extended fixture restores the defaults required by following settings checks");
}
