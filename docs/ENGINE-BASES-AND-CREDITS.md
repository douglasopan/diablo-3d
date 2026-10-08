# Engine bases, presentation references and credits

Evidence reviewed on 07 October 2026. This record distinguishes code used by D3D, locally inspected reference material and proposed presentation changes. The owner-requested [binary reverse-engineering study](BELZEBUB-BINARY-ANALYSIS.md) examines the installed Belzebub executable without requiring a source repository. This record does not implement a new menu, grant additional permissions or replace any license notice.

**Resumo em português.** O motor usado pelo D3D é o DevilutionX modificado, sob a Sustainable Use License existente. Belzebub serve como referência suplementar de apresentação e de algumas vistas de personagens; não foi incorporado como motor. Resolução maior, filtros e interface melhorada não comprovam uma substituição geral das texturas por arte HD. Não localizamos um repositório oficial de código ou licença de reutilização do Belzebub nas fontes consultadas. A entrada e o menu futuros devem ser próprios do D3D, conservar os créditos e usar apenas materiais com proveniência e permissões registradas.

## What D3D actually uses

| Base or material | Current relationship to D3D | Attribution and distribution record |
| --- | --- | --- |
| [DevilutionX](https://github.com/diasurgical/devilutionX), including its Devilution lineage | The source engine being modified: simulation, map data, asset loading, native renderer, menus, input and platform support. The fork baseline recorded in the README is `dac104babfb6187415432f428ac2516747ffc154`. | Preserve [LICENSE.md](../LICENSE.md), existing source notices and the upstream credits. [The preserved upstream README](UPSTREAM-README.md) explains the lineage and legal terms. |
| Original Diablo / Hellfire | User-supplied game data and the canonical visual, positional and simulation reference. D3D reads the user's supported archives locally. | Original game data is proprietary and is not included with the public source. Preserve the Blizzard notices; owning a game copy does not make its archived art a project-owned asset. |
| D3D additions | Tristram's alternate CPU rendering, volume reconstruction, lighting, diagnostics and optional local model import are project additions to the inherited engine. | Keep the prominent modified-software notice and inherited license. Record authorship and asset provenance separately from engine authorship. Current public builds remain the offline `NONET=ON` prototype. |
| Belzebub / Diablo 1 HD MOD | A separately installed mod, inspected locally as a supplementary reference. No Belzebub source code or executable is incorporated into the current D3D engine. Its extra NPC views are reference evidence, not evidence of a runtime asset integration. | Credit the reference by its correct name and official source. Do not describe D3D as based on Belzebub code, distribute its archives or infer copying permission from attribution. See [REFERENCE-SOURCES.md](REFERENCE-SOURCES.md). |
| D3D branding | The owner-provided/generated project identity and 240-frame logo animation already used in title, main and Escape menus. | See [branding provenance](../assets/branding/README.md) and [ANIMATED-LOGO.md](ANIMATED-LOGO.md). This artwork is distinct from the older local experiment that adapts the user's native game logo. |
| Optional generated/imported models | Separate local visual candidates, with their own input, generation and conversion records. They do not replace the engine or establish historical accuracy. | Follow [MESHY-WORKFLOW.md](MESHY-WORKFLOW.md) and the per-asset provenance record. Do not publish source-game extracts, credentials or unreviewed candidate files. |

The current Tristram 3D path rasterizes scene geometry on the CPU into the game's indexed surface. Existing SDL presentation may scale that completed image. Calling the result “3D” does not mean D3D currently uses Belzebub's OpenGL renderer. Home selects the actual original rendering backend; matching pixels there verify dispatch to that backend, while forced-mesh comparisons measure reconstruction separately.

## What the official Belzebub sources establish

The [official features page](https://mod.diablo.noktis.pl/features) documents larger resolutions, panoramic displays and UI improvements. It also describes substantial gameplay additions, rather than a presentation-only patch. The [official download page](https://mod.diablo.noktis.pl/download) lists Belzebub v1.045 as the 10 September 2014 public beta; Tchernobog is a separate download. [GOG's listing](https://www.gog.com/en/game/diablo_1_hd_mod_belzebub) confirms widescreen/interface features, names Noktis and identifies the product as single-player.

The project's published requirements include OpenGL 2.0 support. The installed author's `readme.txt` instead specifies OpenGL 1.5 or greater. GOG lists DirectX 9.0c-compatible graphics for its package. These requirement descriptions are different; they do not identify the exact context version, render passes or filtering configuration used in a particular running build. No official renderer source or technical specification was located in this review. [Official feature requirements](https://mod.diablo.noktis.pl/features), [GOG package requirements](https://www.gog.com/en/game/diablo_1_hd_mod_belzebub).

The installed author's `change log.txt` records graphical-effect and light-radius work, resolution-related fixes and expanded resolution testing. Those are evidence of presentation changes, not a specification of their implementation. A local executable-string audit found identifiers associated with game/UI screen textures, framebuffers, blitting, viewport setup, texture filtering, zoom, fullscreen, resolution and gamma. The subsequent [binary study](BELZEBUB-BINARY-ANALYSIS.md) follows cross-references and disassembles selected routines to confirm call arguments and data flow. It distinguishes confirmed static behavior from unmeasured runtime quality and performance.

Neither the official feature list nor GOG supplies dimensions demonstrating a general high-resolution redraw of the original sprites. The completed local audit gives a narrower, measurable result: the base `DIABDAT.MPQ` copies match; queried town imagery remains the original artwork; supplementary Griswold/Ogden direction sheets remain **96×96 per frame**, with their original direction preserved exactly. Additional views can be useful shape references without being higher-resolution pixels. Coverage of extra archive names is partial, so this does not establish that every mod asset is unchanged. The evidence and coverage limits are recorded in [REFERENCE-SOURCES.md](REFERENCE-SOURCES.md).

## Source availability and permission findings

The reviewed official features, download and contact pages did not provide a Belzebub source repository or a source-code reuse license. Targeted searches of the official domain did not locate one. This is a bounded finding, **not proof that source has never been shared elsewhere**. The official pages and GOG carry all-rights-reserved notices. The installed `License.txt` contains the original game's Blizzard terms; it is not an identified permissive license for the mod's implementation. [Official download](https://mod.diablo.noktis.pl/download), [official contact](https://mod.diablo.noktis.pl/contact), [GOG listing](https://www.gog.com/en/game/diablo_1_hd_mod_belzebub).

The practical path is to use the binary study to describe useful behavior and implement it independently in the codebase we already have. Examples include readable UI at a large output size, separate world and UI layout rules, fullscreen/window handling and an explicit choice of output sampling. Use the actual DevilutionX source for implementation and record the supplementary reference accurately. If a future contribution proposes copied Belzebub code or assets, attach their source and applicable permission terms to that contribution. A credit line does not supply those terms.

The inherited [Sustainable Use License v1.0](../LICENSE.md) allows the uses and modifications stated in its text and limits distribution to free-of-charge, non-commercial purposes. It requires retention of licensing/copyright notices, inclusion of the terms with copies and a prominent notice on modified software. [The upstream README](UPSTREAM-README.md) likewise states non-commercial source use. Do not label this an MIT/GPL release or an unrestricted OSI-approved license. This document leaves the license and all notices unchanged; it summarizes the checked record rather than providing additional rights.

Third-party dependencies, fonts and artwork retain their own notices. Keep their applicable license files in distributed packages as well as the engine license. Project credits must not erase that distinction or imply endorsement by Blizzard, DevilutionX or the Belzebub team.

## Presentation improvements that can be implemented here

DevilutionX already distinguishes internal game resolution from output resolution and exposes fullscreen, fit-to-screen, upscaling, nearest/bilinear/anisotropic output choices and integer scaling in [GraphicsOptions](../Source/options.cpp). Its [display layer](../Source/utils/display.cpp) and [presentation path](../Source/engine/dx.cpp) implement the output side. A clearer D3D image should first be assessed using these existing controls and the actual geometry/material quality; it does not require importing another mod's renderer.

For each presentation experiment, record internal world dimensions, physical output dimensions, UI scale/layout, output sampling and scene/camera/lighting state separately. Compare the same object at the same scale before claiming additional detail. Measure frame time and memory alongside screenshots. A larger view area, a larger displayed sprite and a more detailed source texture are three different changes.

## Proposed D3D entry and menu

This is a design and implementation plan. The existing animated D3D logo is already implemented; a complete new entry/menu layout is not implemented by this document.

1. **Design a project-owned entry screen.** Retain the approved D3D identity and eight-second logo cycle. Use a separately authored background with its provenance recorded. Keep skip behavior, readable copyright text and a direct route into the main menu. Any atmosphere or typography borrowed as a reference must remain identified as reference material rather than copied mod artwork.
2. **Build the main menu on existing UI flows.** Start with clear play/character, settings, credits and exit actions, delegating to the existing selection and save flows. Preserve keyboard, mouse and controller navigation, Escape behavior and localization. The offline build must not advertise a working online mode. Optional community links should open only through an explicit user action.
3. **Separate layout from world rendering.** Define safe UI bounds and readable text independently of the game camera. Exercise 4:3, 16:9 and ultrawide layouts, output scaling and window/fullscreen changes. UI hit regions must follow their displayed bounds. Do not enlarge source art and call it newly detailed artwork.
4. **Keep legal and provenance credits accessible.** Provide clearly separated engine/lineage, original game, D3D contributors, artwork/dependencies and supplementary-reference entries. Preserve upstream credits rather than replacing their screen with project branding. Label Belzebub as a presentation/reference influence, not a code dependency.
5. **Retain fallback and validate the full route.** Missing or invalid custom assets should retain the existing usable menu/title behavior. Check first launch, missing-data guidance, character loading, settings return, input focus, intro/attract behavior and exit at supported resolutions. Ensure the menu work does not alter simulation, saves, F4 switching or Home's native backend.

Relevant implementation entry points are [title.cpp](../Source/DiabloUI/title.cpp), [mainmenu.cpp](../Source/DiabloUI/mainmenu.cpp), [settingsmenu.cpp](../Source/DiabloUI/settingsmenu.cpp), [credits.cpp](../Source/DiabloUI/credits.cpp) and the existing D3D logo loader. Their current behavior is the integration starting point, not evidence that the redesigned menu has shipped.

## Credit wording for the future menu

Keep these categories explicit, with full notices still available:

- **Engine:** modified DevilutionX, with its Devilution lineage and upstream contributors; distributed under the included Sustainable Use License.
- **Original game:** Diablo / Hellfire and their original creators; retain the existing Blizzard copyright and trademark notices. Original game data is supplied by the player.
- **D3D:** project contributors and separately identified original/generated branding or authored assets, with their recorded provenance.
- **Supplementary presentation reference:** Belzebub / Diablo 1 HD MOD, Noktis and the mod team, linked to the official project. No claim of code incorporation or endorsement.
- **Dependencies and contributed assets:** preserve their actual authors, license names, source links and modification notices.

The current upstream credit text, license files and in-game notices remain authoritative. Implementing the future credits screen should add accurate project context while retaining them.
