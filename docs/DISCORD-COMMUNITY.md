# Discord community

The [official Diablo 3D Discord](https://discord.gg/4YxQ7s69S) supports the reconstruction of the whole Diablo 1 game in 3D, including its procedural dungeon levels. Tristram is the first implementation stage. Follow the [execution guide](PROJECT-EXECUTION.md), [contribution guide](CONTRIBUTING.md) and [asset catalog](ASSET-CATALOG.md) for the current scope and work queue.

## Verified organization — 8 October 2026

Six English text channels were created and their saved topics were verified by reopening each channel in the authenticated Discord interface. Following the user's explicit ordering requirement, all six channels are grouped under `English Community`, the first public category, followed by the existing Portuguese categories. The existing `devlog` was moved into the same English-first section with its shared EN/PT-BR topic and existing messages. No duplicate development category was created.

| Category | Channel | Result |
| --- | --- | --- |
| English Community | [general](https://discord.com/channels/1557451354513870878/1557694983195336704) | Created; English discussion and project scope. |
| English Community | [help-and-support](https://discord.com/channels/1557451354513870878/1557695134907371530) | Created; setup, builds, controls and inspector support. |
| English Community | [model-review](https://discord.com/channels/1557451354513870878/1557695478546829322) | Created; existing-model reviews identified by assetId, revision and SHA-256. |
| English Community | [asset-contributions](https://discord.com/channels/1557451354513870878/1557695620683145216) | Created; reservations and contribution coordination. |
| English Community | [bugs-and-performance](https://discord.com/channels/1557451354513870878/1557695778154352771) | Created; reproducible defects and measured performance. |
| English Community | [ideas-and-modding](https://discord.com/channels/1557451354513870878/1557695913609134181) | Created; proposed features and dependencies. |
| English Community | [devlog](https://discord.com/channels/1557451354513870878/1557533144574328842) | Existing shared channel reused and moved; English/Portuguese topic and messages retained. |

The Portuguese channels, existing messages, private channels and four voice rooms were preserved. `Lobby` already provides a shared community voice room, so no duplicate voice channel was created. No announcements, chat messages, DMs or mentions were sent as part of this setup.

No role or permission settings were changed. A read-only check of `general` confirmed that it is public, synchronized with `English Community`, and has no explicit `@everyone` grants for managing the channel, permissions or webhooks. This records the setup performed; it is not an audit of all pre-existing server roles.

## Saved channel topics

### general

Rebuilding the whole Diablo 1 game in 3D, including its procedural dungeons. Tristram is the first stage. English community discussion, questions and feedback.

### help-and-support

English help with setup, builds, controls and the Godot Model Inspector. Include your version, OS, hardware and reproduction steps. Use bugs-and-performance for confirmed defects. Remove private data from logs.

### model-review

English reviews of existing 3D models. Identify every review by assetId, revision and SHA-256; link its asset issue and exact review scope. Include forced-geometry native-camera and 360-degree evidence when relevant. Share only authorized media. Technical validation, partial approval and full acceptance are separate states.

### asset-contributions

English asset coordination. Review the catalog and existing issues first. Reserve a reusable family by assetId and wait for maintainer confirmation before modeling. Track revision and SHA-256 in the asset issue. Preserve selected revisions; current priority is reviewing existing models. GitHub issues and the registry remain the work queue.

### bugs-and-performance

English bug and performance reports. Include build/revision, OS/GPU, settings, steps and expected/actual results. For asset defects, include assetId, revision and SHA-256. Separate measured performance from estimates, and link the matching GitHub issue. Remove private data from logs.

### ideas-and-modding

English ideas for rebuilding the whole Diablo 1 game in 3D, including procedural levels and future modding. Tristram is the first stage. Link relevant issues and dependencies. Proposed features are plans until implemented and verified.

### devlog

Shared EN/PT-BR development log for the whole Diablo 1 reconstruction in 3D. Post verified updates with revision, evidence and known limitations. Technical validation, partial visual approval and full asset acceptance are separate states. Identify planned features as planned. Tristram is the first stage; procedural dungeon levels follow. / Atualizações verificadas com revisão, evidências e limitações; distinguir planos de entregas.

## Maintenance

Keep work reservations in the existing asset issues and registry; Discord is a coordination surface, not a second work queue. Preserve selected revisions and the distinction between technical checks, partial approval and full acceptance. Review existing channel names and purposes before adding another category or room.

Keep `English Community` first in the public channel list, with the existing Portuguese categories below it. Preserve channel IDs, messages and permission overrides when reorganizing channels.

The live channel setup is complete. Future announcements or automated repository notifications require their own authorized task. Private verification evidence is stored outside the public repository in `diagnostics/discord-community-20261008/`.
