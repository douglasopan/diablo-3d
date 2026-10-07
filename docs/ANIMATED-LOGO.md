# Owner-provided animated D3D logo

The project owner supplied the animation through [this shared conversation](https://chatgpt.com/share/6ac68fd7-4898-83e8-9ce3-9e4587c49645). The downloaded `diablo3d_frames_leve_640x160.zip` contains 240 transparent RGBA PNG frames at 640×160, 30 frames per second and an eight-second loop. Its manifest and the actual frame sequence were checked before conversion.

The supplied lettering and animation are used as delivered. The importer adapts their size, palette and coverage to the software renderer; it does not redraw or generate the artwork.

| Asset | Frame size | Frames | Purpose |
| --- | --- | --- | --- |
| `assets/ui_art/d3d-menu.pcx` | 580×154 | 240 | Main menu and its related UI dialogs |
| `assets/ui_art/d3d-title.pcx` | 620×216 | 240 | Diablo title screen |
| `assets/ui_art/d3d-pause.pcx` | 360×90 | 240 | In-game Escape menu |

PCX sheets stack their frames vertically. Their unsigned 16-bit header dimensions are validated, including sheets taller than 32767 pixels. These independently generated project images are different from the proprietary logo files extracted from a game archive.

The runtime selects frames over an exact **8000 ms** cycle, rather than rounding 30 fps to a 33 ms interval. It maps the artwork's palette to the active UI or dungeon palette. The Escape logo therefore does not assume the main-menu palette. Opaque dark letter faces remain visible; index 250 represents transparency. Partial flame coverage is adapted using a fixed ordered pattern, since the current CLX renderer has binary coverage.

When a custom logo is absent or invalid, its screen uses the existing game logo and animation behavior. Other UI animations retain their original timing. The original game archives remain read-only.

## Reproduce the conversion

With Python, Pillow and NumPy installed, provide the original downloaded archive:

```powershell
python .\tools\import_logo_frames.py 'C:\path\diablo3d_frames_leve_640x160.zip' --output '.\branding\d3d-animated'
```

The importer checks the manifest, all numbered frames, uniform image sizes, preservation of opaque dark pixels and an exact indexed-pixel/palette PCX round trip. It writes the three PCX sheets and a source/output hash report. One shared palette across the full cycle avoids changes caused by independent frame quantization.

The built-in source assets are copied during the normal game build. For a local profile override, copy the three generated PCX files from the output's `ui_art/` directory into `perfil-tristram/ui_art/`, then close and reopen the game.

The old `Atualizar-Logo.ps1` utility remains a separate experiment that reconstructs a 15-frame logo from the user's native game data. Those proprietary-art outputs are kept local. The new supplied 240-frame animation takes precedence when present.

Source and adapted-file hashes are recorded in [animated-logo-provenance.json](../assets/branding/animated-logo-provenance.json). The square [D3D avatar](../assets/branding/d3d-avatar.png) and full [README banner](../assets/branding/diablo3d-banner.png) are separate project identity assets.
