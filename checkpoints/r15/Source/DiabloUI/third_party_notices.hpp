#pragma once

#include <array>

namespace devilution {

inline constexpr std::array<const char *, 3> ThirdPartyCopyrightFooter {
	"SDL_audiolib © 2014-2019, 2021-2022 Nikos Chantziaras (LGPL3+)",
	"libsmackerdec © 2011 Barry Duncan; © 2006 Konstantin Shishkov (LGPL2.1+)",
	"GPL/LGPL copies: notices/licenses/ — Project Credits > Third-Party Notices",
};

inline constexpr std::array<const char *, 28> ThirdPartyNoticeLines {
	"Third-Party Notices",
	"",
	"This build uses SDL_audiolib under GNU LGPL version 3 or later.",
	"Copyright (C) 2014, 2015, 2016, 2017, 2018, 2019 Nikos Chantziaras.",
	"Copyright (C) 2021 Nikos Chantziaras.",
	"Copyright (C) 2022 Nikos Chantziaras.",
	"GNU LGPL copy: notices/licenses/SDL_audiolib/COPYING.LESSER",
	"GNU GPL copy: notices/licenses/SDL_audiolib/COPYING",
	"",
	"This build uses libsmackerdec under GNU LGPL version 2.1 or later.",
	"Copyright (C) 2011 Barry Duncan",
	"Smacker decoder from FFmpeg: Copyright (c) 2006 Konstantin Shishkov",
	"GNU LGPL copy: notices/licenses/libsmackerdec/COPYING",
	"",
	"LGPL modification and reverse-engineering permissions apply within the cited LGPL terms.",
	"Source and relink kit: private candidate; final availability remains pending.",
	"Full notices: notices/THIRD-PARTY-NOTICES.md",
	"Application license: notices/licenses/engine/LICENSE.md",
	"",
	"SDL2 / SDL2main: Copyright (C) 1997-2025 Sam Lantinga (zlib).",
	"SDL_image: Copyright (C) 1997-2019 Sam Lantinga (zlib). PNG-only local modification.",
	"mpqfs: Copyright (c) 2025 mpqfs contributors (MIT).",
	"Other components: libpng, zlib, bzip2, SheenBidi, Lua, Speex resampler, dr_libs/minimp3,",
	"magic_enum, unordered_dense, sol2, find_steam_game, PicoSHA2, tl::function_ref, HIDAPI, yuv2rgb.",
	"Their original copyrights, disclaimers and licenses are preserved in notices/licenses/.",
	"",
	"Original Diablo/Hellfire data is supplied separately by the player.",
	"The library licenses do not license commercial game data or custom assets.",
};

} // namespace devilution
