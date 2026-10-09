// Included inside the existing settings runner's anonymous namespace.
KeymapperOptions::Action &MovementSetting(std::string_view id)
{
	for (auto *entry : GetOptions().Keymapper.GetEntries())
		if (entry->key == id)
			return static_cast<KeymapperOptions::Action &>(*entry);
	throw std::runtime_error("Missing native movement key action: " + std::string(id));
}

void CheckMovementKeymapping(const ConfigFixture &fixture)
{
	auto &mapper = GetOptions().Keymapper;
	const std::array<std::pair<std::string_view, uint32_t>, 8> defaults {{
	    { "TownMoveForward", 'W' }, { "TownMoveForwardAlternate", SDLK_UP },
	    { "TownMoveBackward", 'S' }, { "TownMoveBackwardAlternate", SDLK_DOWN },
	    { "TownMoveLeft", 'A' }, { "TownMoveLeftAlternate", SDLK_LEFT },
	    { "TownMoveRight", 'D' }, { "TownMoveRightAlternate", SDLK_RIGHT }
	}};
	for (const auto &[id, key] : defaults) {
		auto &action = MovementSetting(id);
		Check(action.context == KeymapperContext::TownMovement && mapper.KeyForAction(id) == key,
		    "movement defaults use the shared native option model: " + std::string(id));
		Check(mapper.findAction(key, KeymapperContext::TownMovement) == &action,
		    "the contextual map resolves exactly the selected movement action");
	}
	Check(mapper.KeyForAction("DisplaySpells") == 'S' && mapper.findAction('S')->key == "DisplaySpells",
	    "adding backward S never unbinds or replaces the native spell shortcut");
	auto &forward = MovementSetting("TownMoveForward");
	auto &backward = MovementSetting("TownMoveBackward");
	OpenCategory(mapper);
	ActivateNamed(forward.GetName());
	Check(MenuCount() == 4 && FindHere(_("Restore movement defaults")).has_value(),
	    "movement binding reuses Bind/Unbind/Previous Menu and adds contextual reset");
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_F24);
	SendKey(SDLK_F24, true);
	Check(mapper.KeyForAction(forward.key) == SDLK_F24 && InventoryKey().GetValueDescription() == "F24",
	    "movement remap preserves a native binding on the same key in its other context");
	Check(forward.ContextDescription().find(std::string(InventoryKey().GetName())) != std::string::npos,
	    "the binding description names the preserved native shortcut");
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_ESCAPE);
	SendKey(SDLK_ESCAPE, true);
	Check(mapper.KeyForAction(forward.key) == SDLK_F24 && IsInGameSettingsOpen(),
	    "Escape cancels movement capture without changing the current binding");
	ActivateNamed(_("Bind key"));
	SendMouseButton(SDL_BUTTON_X1);
	SendMouseButton(SDL_BUTTON_X1, true);
	Check(mapper.KeyForAction(forward.key) == SDLK_F24 && FindHere(_("Press any key to change.")).has_value(),
	    "continuous movement rejects mouse bindings and remains in capture");
	SendKey(SDLK_ESCAPE);
	SendKey(SDLK_ESCAPE, true);
	Back();
	ActivateNamed(backward.GetName());
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_F24);
	SendKey(SDLK_F24, true);
	Check(mapper.KeyForAction(backward.key) == 'S' && mapper.KeyForAction(forward.key) == SDLK_F24
	        && !backward.BindingError(SDLK_F24).empty(),
	    "a movement conflict is reported without stealing either binding");
	SendKey(SDLK_ESCAPE);
	SendKey(SDLK_ESCAPE, true);
	ActivateNamed(_("Restore movement defaults"));
	for (const auto &[id, key] : defaults)
		Check(mapper.KeyForAction(id) == key, "contextual reset restores each of the eight movement slots");
	Check(InventoryKey().GetValueDescription() == "F24" && mapper.KeyForAction("DisplaySpells") == 'S'
	        && mapper.KeyForAction("ToggleTown3D") == SDLK_F4 && mapper.KeyForAction("Town3DCameraMode") == 'K',
	    "movement reset preserves legacy, camera and custom native shortcuts");
	Check(!forward.SetValue(SDLK_F4) && mapper.KeyForAction(forward.key) == 'W'
	        && !forward.BindingError(SDLK_F4).empty(),
	    "movement cannot silently mask the native camera toggle");
	Check(forward.SetValue(SDLK_UNKNOWN), "free the movement default for a reserved native shortcut");
	auto &cameraMode = MovementSetting("Town3DCameraMode");
	Check(cameraMode.SetValue('W'), "native camera shortcut can occupy an explicitly freed movement default");
	const auto partialReset = mapper.RestoreMovementDefaults();
	Check(mapper.KeyForAction(forward.key) == SDLK_UNKNOWN && mapper.KeyForAction(cameraMode.key) == 'W'
	        && partialReset.find(std::string(forward.GetName())) != std::string::npos,
	    "partial reset reports the unavailable slot and preserves its reserved native occupant");
	Check(cameraMode.SetValue('K'), "restore the native camera fixture binding");
	mapper.RestoreMovementDefaults();
	Back();
	auto &rightAlternate = MovementSetting("TownMoveRightAlternate");
	Check(rightAlternate.SetValue(SDLK_UNKNOWN), "free a contextual arrow for real UI capture");
	auto &forwardAlternate = MovementSetting("TownMoveForwardAlternate");
	ActivateNamed(forwardAlternate.GetName());
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_RIGHT);
	SendKey(SDLK_RIGHT, true);
	Check(mapper.KeyForAction(forwardAlternate.key) == SDLK_RIGHT && forwardAlternate.GetValueDescription() == "RIGHT",
	    "real menu capture accepts arrow keys with stable native display and persistence names");
	ActivateNamed(_("Restore movement defaults"));
	// The UI capture above is real; the remaining model writes isolate reload
	// ordering, empty entries and swaps without creating another browser.
	CloseInGameSettings();
	Check(forward.SetValue('T') && backward.SetValue('G'), "set a private custom movement layout");
	SaveOptions();
	mapper.ClearMovementBindings();
	Check(forward.SetValue('G') && backward.SetValue('T'), "create the reversed in-memory layout before reload");
	LoadOptions();
	Check(mapper.KeyForAction(forward.key) == 'T' && mapper.KeyForAction(backward.key) == 'G',
	    "reload clears the contextual map first, so swapped old bindings cannot block the persisted layout");
	auto &alternate = MovementSetting("TownMoveForwardAlternate");
	Check(alternate.SetValue(SDLK_UNKNOWN), "unbind only the alternate forward slot");
	SaveOptions();
	Check(alternate.SetValue(SDLK_UP), "temporarily restore an alternate before persistence verification");
	LoadOptions();
	Check(mapper.KeyForAction(alternate.key) == SDLK_UNKNOWN && mapper.KeyForAction(forward.key) == 'T',
	    "explicitly empty alternate persists independently of its main binding");
	std::ifstream saved(fixture.iniPath, std::ios::binary);
	const std::string contents { std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>() };
	const auto parsed = Ini::parse(contents);
	Check(parsed.has_value() && parsed->getString("Keymapping", "TownMoveForward") == "T"
	        && parsed->getString("Keymapping", "TownMoveForwardAlternate").empty(),
	    "the isolated INI uses stable native action IDs and the existing empty-unbound format");
	mapper.RestoreMovementDefaults();
	SaveOptions();
	LoadOptions();
	for (const auto &[id, key] : defaults)
		Check(mapper.KeyForAction(id) == key, "restored movement defaults survive save/reload");
	Check(DelegatedEvents == 0, "movement key capture and rejected conflicts never reach gameplay input");
}
