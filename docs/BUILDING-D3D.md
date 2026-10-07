# Build D3D from a clean clone on Windows

This guide covers the D3D Windows prototype. The public repository places `CMakeLists.txt`, `Source/`, `CMake/` and `tools/` at its root. The build script also accepts the existing nested developer layout with the engine in `devilutionx/`. The upstream [engine build guide](../docs/building.md) remains the reference for general engine setup and other platforms.

The public source does not include a compiler, a `.tools` bundle, Diablo game archives, extracted game artwork, player saves or a ready-made player profile. The current script builds a local-only executable with `NONET=ON`; this does not establish multiplayer or voice support. A first build may download source dependencies through CMake. Disabling game networking does not make the initial dependency setup an offline build.

## Início rápido em português

Instale Git, Visual Studio 2022 ou posterior com desenvolvimento C++ para desktop e Windows SDK, CMake 3.22 ou posterior e Ninja. Abra PowerShell na raiz do clone e execute:

```powershell
.\build.ps1 -WithSmoke -Targets devilutionx,town_view_smoke
.\Iniciar-Tristram.ps1 -DataDirectory 'C:\caminho\do\seu\Diablo'
```

A pasta informada deve conter seu próprio `DIABDAT.MPQ` ou `spawn.mpq`. O iniciador cria um perfil separado em `perfil-tristram`. F4 alterna a visão em Tristram; Home usa o desenho original do jogo. Comparar as malhas exige as capturas de geometria forçada. Os dados extraídos e os diagnósticos ficam locais.

## Prerequisites

- Git available on `PATH`, including for CMake's source dependency downloads.
- Visual Studio 2022 or newer, or Visual Studio Build Tools, with current updates, the **Desktop development with C++** workload, MSVC x64 tools and a Windows SDK. The engine requests C++23.
- CMake **3.22 or newer**, matching the minimum in the engine's `CMakeLists.txt`.
- Ninja. Visual Studio's optional **C++ CMake tools for Windows** component can supply CMake and Ninja; otherwise make their executables available on `PATH`.
- Your own local retail Diablo data, or your own shareware data. No game archives are distributed by this repository.

Clone the repository with Git and open PowerShell in the clone's root. The build script locates Visual Studio through `vswhere` and prepares the x64 compiler environment in its own process. It looks for CMake and Ninja in an existing local `.tools` folder, then on `PATH`, then in the selected Visual Studio installation. A clean public clone does not need `.tools`. The script does not install missing tools.

## Configure and build

For the game and the offscreen diagnostic:

```powershell
.\build.ps1 -WithSmoke -Targets devilutionx,town_view_smoke
```

The game executable is `build/devilutionx-tristram-v4.exe`; the diagnostic is `build/town_view_smoke.exe`. The game target also prepares the engine's built assets in `build/assets`. These engine support assets do not replace the user's Diablo archive.

Useful script options include:

```powershell
.\build.ps1 -ConfigureOnly -WithSmoke
.\build.ps1 -WithSmoke -Targets devilutionx,town_view_smoke -Configuration RelWithDebInfo -Jobs 4
```

`-SkipConfigure` reuses an existing configuration; use it only after configuring the required optional targets. `-WithSmoke` enables the diagnostic target and `-WithBranding` enables the logo tool. Pass the target names you want to compile through `-Targets`.

Read `build/configure.log` and `build/build.log` if a build fails. Missing `vswhere` or MSVC means the Visual Studio C++ installation must be completed. Missing CMake or Ninja means those tools must be supplied through `PATH` or the Visual Studio CMake component. When moving a build between different clones, layouts or compilers, configure into a new build directory rather than reusing a cache that refers to the old source tree.

## Run with local game data

Point the launcher at the directory containing your archive:

```powershell
.\Iniciar-Tristram.ps1 -DataDirectory 'C:\Games\Diablo'
```

The launcher reads `DIABDAT.MPQ` for retail or `spawn.mpq` for shareware and creates configuration and saves in `perfil-tristram/`. It does not modify the original installation. With no explicit directory, it checks its known GOG installation path and then the local `data/` directory; a clean clone does not populate that directory for you.

The executable can also be launched directly with explicit data, save and configuration paths:

```powershell
.\build\devilutionx-tristram-v4.exe --diablo --data-dir 'C:\Games\Diablo' --save-dir '.\perfil-tristram' --config-dir '.\perfil-tristram'
```

Use `--spawn` instead of `--diablo` for a shareware-only directory. Prefer the launcher for its initial window settings and separate profile. Save and close an already running game before starting a newly built executable.

F4 changes the view only in Tristram. The Home pose deliberately dispatches to the actual original renderer and its cursor rules. A rotated camera renders reconstructed 3D meshes. Home pixel identity therefore confirms correct original-backend reuse; it does not prove that the mesh reconstruction is identical.

## Local validation

The smoke tool takes exactly three positional directories: game data, built engine assets and output captures. It runs without opening a game window:

```powershell
.\build\town_view_smoke.exe 'C:\Games\Diablo' '.\build\assets' '.\diagnostics\local-retail'
```

Use a separate shareware-only input directory when testing `spawn.mpq`; a directory containing the retail archive selects retail. Report the edition and commit tested. Generated images can contain proprietary artwork, so do not commit archive extracts or diagnostic asset dumps.

The diagnostic checks state preservation, resource reloads, selection, closed and oriented volumes, depth, ground contact and fixed camera fixtures. It emits original-backend references, Home/backend-reuse references and forced-geometry captures separately. Passing mechanical checks is not an approval of artistic fidelity. Inspect rotated views for wrong silhouettes, texture projection, clipping, unseen faces and actor occlusion.

The optional image-analysis tools need Python, Pillow and NumPy. A local environment can be prepared with:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install Pillow numpy
```

Compare matched `native-*` and `calibrated-*` captures and assemble turntables:

```powershell
.\.venv\Scripts\python.exe .\tools\compare_native_views.py .\diagnostics\local-retail --out-dir .\diagnostics\local-retail-comparison
.\.venv\Scripts\python.exe .\tools\qa_360.py .\diagnostics\local-retail\turntables
```

`compare_native_views.py` compares RGB at the same coordinates without aligning or resizing images, and includes opaque black. It writes numeric reports and labeled contact sheets. `qa_360.py` reads the turntable manifest and assembles native-angle and 360° review sheets without changing game artwork. Neither tool declares the art correct from a global percentage.

`audit_v4_release.py` is a local release-summary helper with fixed input names. To use it, generate both retail and shareware outputs in `diagnostics/v4-final-gog` and `diagnostics/v4-final-shareware`, save their console logs as `diagnostics/v4-final-gog-run.log` and `diagnostics/v4-final-shareware-run.log`, and run the raw comparison into `diagnostics/qa-v4-final-gog`. Save console logs with UTF-8 encoding, especially in Windows PowerShell 5.1. Once those inputs exist:

```powershell
.\.venv\Scripts\python.exe .\tools\compare_native_views.py .\diagnostics\v4-final-gog --out-dir .\diagnostics\qa-v4-final-gog
.\.venv\Scripts\python.exe .\tools\audit_v4_release.py
```

The release helper records an executable hash and summarizes the existing reports. It does not generate missing game data, run the game itself, perform a network test or approve raw mesh fidelity. If only one data edition is available, report that limitation rather than fabricating the second dataset.

## Optional animated logo

After building the game, compile the local logo tool and generate profile overrides from your own archive:

```powershell
.\build.ps1 -WithBranding -Targets diablo_logo_build
.\Atualizar-Logo.ps1 -DataDirectory 'C:\Games\Diablo'
```

The repository includes the image-generated `branding/numeral-3.png` used by the tool. The remaining letters and animated fire are read from the local game data. The tool preserves the native animation frames and verifies its PCX output; `Atualizar-Logo.ps1` installs the overrides into `perfil-tristram/ui_art`. Close and reopen the game to load them. The original archive remains read-only. Generated PCX overrides and extracted inspection images are local outputs, not redistributable game data shipped by the repository.

## Scope of this guide

These commands reproduce the current Windows rendering prototype. They do not add dynamic geometry-cast shadows, multiplayer capacity, voice, or Meshy assets to the game. See the [contribution guide](CONTRIBUTING.md), [roadmap](ROADMAP.md) and [networking research](NETWORKING-RESEARCH.md) for implemented behavior, contribution requirements and future work.
