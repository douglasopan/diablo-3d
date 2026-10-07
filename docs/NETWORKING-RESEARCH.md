# Networking, shared Tristram, and proximity voice

**Status: research and proposed experiments; no networking or voice feature was implemented by this document.** Reviewed on **2026-10-07**. The local engine is based on DevilutionX commit `dac104babfb6187415432f428ac2516747ffc154`, with local rendering changes. Findings below distinguish inspected code, vendor documentation, and our proposed architecture. Provider requirements must be checked again when an integration is started.

## Resumo em português

O protótipo atual foi compilado com `NONET=ON`: ele não oferece multiplayer pela rede. DevilutionX tem cooperativo para **quatro jogadores**, com TCP ou ZeroTier quando essas opções são compiladas. A opção chamada cliente-servidor usa um servidor de transporte; ela não transforma o jogo em um mundo persistente com validação central de combate e inventário.

Um servidor com muitas pessoas em Tristram é possível como projeto novo, mas exige separar os avatares do hub dos quatro participantes de cada partida. A proposta inicial é um hub social experimental e masmorras instanciadas por grupo, preservando primeiro o cooperativo existente. Quantidades como 16 ou 32 participantes abaixo são metas de ensaio, não capacidades já entregues.

Para voz, Mumble Link é um primeiro experimento aberto e pequeno, com aplicativo de voz externo. Discord Social SDK pode fornecer áudio por participante para espacialização dentro do jogo, mas tem requisitos e limites para produção. TeamSpeak tem APIs posicionais e licenciamento próprio. WebRTC/Opus permite uma solução controlada pelo projeto, com mais infraestrutura e manutenção. Voz deve ser opcional, com botão para falar, mute e isolamento entre mundos. Nada foi instalado, conectado ou publicado nesta pesquisa.

## 1. What the current code actually supports

| Area | Inspected evidence | Consequence |
| --- | --- | --- |
| This prototype build | [`build.ps1`](../build.ps1) explicitly passes `-DNONET=ON`; the inspected `build/CMakeCache.txt` also records `NONET:BOOL=ON` | The current executable is an offline rendering prototype. Network play has not been validated here. |
| Player limit | [`multi.h`](../Source/multi.h) defines `MAX_PLRS 4`; `InitMulti` in [`multi.cpp`](../Source/multi.cpp) resizes `Players` accordingly | Four gameplay participants are the existing capacity, not an arbitrary lobby setting. |
| Transport | [`abstract_net.cpp`](../Source/dvlnet/abstract_net.cpp) selects loopback, TCP, or ZeroTier behind compile guards | Re-enabling networking requires a separate configured build and actual multiplayer tests. |
| Turn coordination | [`base.cpp`](../Source/dvlnet/base.cpp), `AllTurnsArrived` / `SNetReceiveTurns`; [`nthread.cpp`](../Source/nthread.cpp), `nthread_recv_turns` | Connected peers exchange turns and gameplay messages; turn advancement waits for peer turn queues. This is a peer-coordinated simulation, with resynchronization and delta state, not a modern authoritative snapshot server. |
| TCP server | [`tcp_server.cpp`](../Source/dvlnet/tcp_server.cpp), `HandleReceivePacket` forwards to `SendPacket`; [`tcp_server.h`](../Source/dvlnet/tcp_server.h) stores `connections[MAX_PLRS]` | A central TCP listener routes packets and allocates slots. It does not itself validate movement, damage, item creation, or persistent character progression. |
| Protocol IDs | [`packet.h`](../Source/dvlnet/packet.h) uses `uint8_t plr_t`, reserved IDs `0xFE`/`0xFF`, and turn/join/message packet types | The byte-sized ID is not evidence that 254-player gameplay works. Other limits and semantics remain. |
| Join compatibility | [`multi.cpp`](../Source/multi.cpp) packs `GameData`, checks its size, and supplies version fields; [`storm_net.cpp`](../Source/storm/storm_net.cpp) checks turn-array size | A changed protocol needs explicit negotiation and rejection of incompatible clients, not silent changes to a shared constant. |
| Menus | [`DiabloUI/multi/selconn.cpp`](../Source/DiabloUI/multi/selconn.cpp) hides network providers with `NONET`; connection descriptions use `MAX_PLRS`; [`selgame.cpp`](../Source/DiabloUI/multi/selgame.cpp) implements create/join/password flows | A many-user hub needs new membership, party, transfer, reconnect, and capacity UI. The present menu is a game-session menu. |
| Existing Discord option | [`3rdParty/discord/CMakeLists.txt`](../3rdParty/discord/CMakeLists.txt) fetches **Discord Game SDK 3.2.1**; [`Source/discord/discord.cpp`](../Source/discord/discord.cpp) updates activities; current cache has `DISCORD_INTEGRATION=OFF` | The existing optional Rich Presence integration is not the current Discord Social SDK voice integration. Enabling that option alone does not add proximity voice. |

Upstream documents ZeroTier as a session maintained by its peers, TCP as a host listener using port 6112 by default, and Offline as solo play with multiplayer characters. Single-player and multiplayer characters are separate. These are upstream behaviors, not tests performed on this prototype. [DevilutionX multiplayer guide](https://github.com/diasurgical/devilutionX/wiki/Multiplayer)

### Dependencies for a network-enabled experiment

[`CMake/Dependencies.cmake`](../CMake/Dependencies.cmake) adds **Asio** for TCP, **libzt** for ZeroTier, and **libsodium** when `PACKET_ENCRYPTION` is enabled. [`CMakeLists.txt`](../CMakeLists.txt) controls `NONET`, `DISABLE_TCP`, `DISABLE_ZERO_TIER`, and `PACKET_ENCRYPTION`. The local libzt recipe pins a diasurgical fork. Preserve its exact revision and notices when evaluating reproducibility; do not replace it with an unrelated latest package without testing its API.

A future experiment should use a separate build directory and add an explicit build-script option for network support. Keep the tested offline executable available. Do not simply pass an extra CMake flag after this script: its hardcoded `NONET=ON` will be applied again on the next configuration. First validate TCP on loopback, then LAN, then controlled Internet conditions. Review public/private packet behavior in the source; a session password is not a persistent account or an authoritative economy.

## 2. Why increasing `MAX_PLRS` is insufficient

The required audit spans transport slot arrays, command encodings, player masks, collision occupancy, monsters and pets, portals, resynchronization, save layouts, targeting, chat, UI, and gameplay balance. All clients must agree on the new protocol. Legacy character saves require a documented import/migration policy; a multiplayer character file is not a server-owned account database.

The following concrete constraints were independently checked in the local source. They are examples of required changes, not a complete proof that any larger count is safe:

| Constraint | Exact code evidence | Required consequence |
| --- | --- | --- |
| Fixed slots in several layers | `MAX_PLRS` in [`multi.h`](../Source/multi.h):26; [`base_protocol.h`](../Source/dvlnet/base_protocol.h) stores `peers[MAX_PLRS]`; [`tcp_server.h`](../Source/dvlnet/tcp_server.h) stores `connections[MAX_PLRS]`; [`storm_net.cpp`](../Source/storm/storm_net.cpp):57 checks array size | A consistent transport/simulation contract must change across every participant. |
| Eight-bit monster hit credit | [`monster.h`](../Source/monster.h):276 uses `int8_t whoHit`; [`monster.cpp`](../Source/monster.cpp):4973 sets `1 << playerId`; [`player.cpp`](../Source/player.cpp):2463 accepts a `char pmask`; [`msg.h`](../Source/msg.h):691 has `int8_t mWhoHit` | Player IDs 8 and above cannot be represented in this credit mask. Changing it affects experience/credit, sync and persistence; signed conversions also need tests. |
| Saved hit masks | [`loadsave.cpp`](../Source/loadsave.cpp):725 and :1574 read/write the monster mask as `int8_t` | A widened mask requires a versioned save-format policy, including old saves and mixed clients. |
| Thirty-two-bit recipient mask | [`multi.cpp`](../Source/multi.cpp), `multi_send_msg_packet`:647, iterates a shifting `uint32_t`; [`msg.cpp`](../Source/msg.cpp), `NetSendCmdString`, uses this path | IDs 32 and above cannot be addressed by this mask. Replacing it is a protocol/API change, not a bandwidth setting. |
| Signed occupancy IDs | [`levels/dun_tile_data.hpp`](../Source/levels/dun_tile_data.hpp):111 declares `int8_t dPlayer`; occupancy uses positive/negative player indices | The transport's unsigned byte IDs do not match an unlimited occupancy representation. Audit movement, attacks, rendering and save serialization together. |
| Discovery layout scales with slot count | [`base_protocol.h`](../Source/dvlnet/base_protocol.h):373 calculates `sizeof(GameData) + PlayerNameLength * MAX_PLRS` for info parsing | Different player-count builds interpret the discovery payload differently. Negotiate protocol/capacity before accepting a session. |
| Delta state is keyed by level | [`msg.cpp`](../Source/msg.cpp):282 stores `map<uint8_t, DLevel> DeltaLevels`; its lookup uses the level number; [`sync.cpp`](../Source/sync.cpp) synchronizes `bLevel` | Two independent copies of the same floor require a new instance identity and state namespace, or separate processes/sessions. |
| Level save namespace | [`loadsave.cpp`](../Source/loadsave.cpp), `GetLevelNames`:1044, builds `temp`/`perm` names from `currlevel` or `setlvlnum`; `SaveGameData` writes one current level/type/view | Instance/shard persistence cannot be added by reusing these names in a shared save namespace. `SaveLevel` also serializes global world arrays. |

The four words in `FormatGameSeed` are seed data, **not** another four-player limit. Avoid counting every literal `4` as a player assumption.

The engine exposes a single currently loaded world through globals such as `currlevel`, `leveltype`, `dPiece`, `dPlayer`, monsters, items, and objects. Multiplayer already tracks players on different dungeon levels and exchanges level deltas; this does **not** establish support for two unrelated dungeon instances with the same level number. An instance requires its own seed, entities, item/quest state, portals, ownership, persistence, and routing key.

A four-to-eight-player branch can be useful as a bounded protocol experiment. It is a separate feature with compatibility and balance work; it should not be presented as the path to hundreds of players in one combat simulation. More peers also mean more replicated work and more opportunities for slow peers to stall coordinated turns. Measure that behavior rather than promising capacity from the size of an ID field.

## 3. Proposed shared-hub architecture

**Proposal, not implemented:** keep a shared social Tristram distinct from an existing four-player dungeon session.

```text
Client: rendering + input + optional voice adapter
                 |
         authenticated control connection
                 |
Hub service: membership, movement authority, parties, instance allocation
                 |
     party A worker          party B worker
     dungeon instance A      dungeon instance B
                 |
Persistence: character revision + inventory ownership + transfer transaction

Voice service/provider: separate real-time media path, scoped to world membership
```

Hub avatars should have stable entity IDs and a separate replication collection. Do not insert every visitor into the existing `Players[MAX_PLRS]` combat path. In the first hub experiment, visitors move and converse; combat, drops, trades, and character progression remain disabled there. This makes the capacity experiment small enough to test without inventing a shared economy.

Initially, each dungeon worker can be a separate process, because the existing world is global. A supervisor allocates a party, seed, session ID, data directory, and isolated persistence namespace. This accommodates many concurrent **parties**, not many participants in one legacy simulation. A seamless visible entrance and cross-service character transfer remain additional client/protocol work.

An unattended legacy host is a useful intermediate step but is still a peer in the old simulation. A headless host that seats an idle character may consume a gameplay slot; verify that before advertising party capacity. The separate **DevXH** project documents such an idle-player headless host and per-process hosting. It is a research reference, not code installed, audited end-to-end, or integrated here. [DevXH server handbook](https://devxh.com/guides/handbook.html)

### Hosted relay versus authoritative world

| Choice | Suitable initial purpose | Remaining work |
| --- | --- | --- |
| Existing peer-coordinated gameplay, direct or through a hosted relay | Private four-player cooperative tests; compatibility with the current model | Client-owned progression and distributed state remain; a relay does not establish server authority. |
| Authoritative social hub with legacy dungeon workers | Larger shared town, parties, scoped voice, many parallel small groups | Remote-avatar rendering, movement validation, membership routing, reconnects, and transfers are new systems. Dungeon workers can initially retain legacy trust semantics, which must be stated. |
| Authoritative dungeon simulation and persistence | Public progression/economy requiring server ownership of outcomes | Server validates input and simulates combat, RNG, loot and inventory; clients render snapshots/events. This is a major simulation/protocol project, including prediction or interpolation and save migration. |

For persistent public play, the host must own character revisions and accepted inventory transactions. Instance transfer needs a prepare/commit process so reconnects cannot duplicate items or activate one character in two workers. Imported local saves must be explicitly marked as untrusted, allowed only in a separate realm, or transformed under a documented policy. The choice is a project design decision, not an implemented protection.

## 4. Voice options researched from primary sources

| Option | Integration route | Recommendation for this project |
| --- | --- | --- |
| Discord Social SDK | In-game lobby calls; per-user decoded audio callbacks can feed our spatial mixer | Optional adapter after a small prototype and verification of production access requirements. Keep it separate from the legacy Rich Presence SDK. |
| TeamSpeak | External TS3 client plugin with position exchange, or embedded SDK under its terms | Viable optional provider; distinguish plugin integration from embedding a licensed client/server SDK. |
| Mumble Link | Game writes avatar/camera vectors and context to shared memory; Mumble client handles voice | First OSS-friendly positional experiment with an external client and self-hosted voice server. Channel isolation needs explicit testing. |
| WebRTC with Opus | Dedicated media service or SFU, per-speaker streams, game-side spatial mixing | Longer-term embedded/provider-independent path; larger build, deployment and audio-engine task. |

### Discord: current Social SDK, not a Discord bot

The official reference lists Social SDK **1.10.19337, released 2026-09-01**; its voice-settings snapshot includes mute/deafen, volumes, voice mode and the configured PTT key. Pin a tested release at implementation time. [Discord Social SDK release notes](https://discord.com/developers/docs/social-sdk/release_notes.html)

`StartCallWithAudioCallbacks` provides incoming PCM with a user ID. The callback can alter samples or set `outShouldMuteData` and route them into a separate audio pipeline, avoiding double playback. This makes game-side attenuation/panning feasible; it does not supply the authoritative position of that user. Map provider IDs to authenticated game entities separately. [Managing voice chat](https://docs.discord.com/developers/discord-social-sdk/development-guides/managing-voice-chat)

Production communications have approval and rate-limit requirements. The reviewed guide lists development lobby create/join and update limits of 100 requests per two hours **per application**, with separate limits for other operations. Increased production access requires account linking, Rich Presence/joins, invites, a unified friends list, functioning flows, review materials and age-appropriate protections. Do not use lobby metadata updates for frame-by-frame coordinates or assume a bare voice-only integration qualifies. [Communication features and production access](https://docs.discord.com/developers/discord-social-sdk/core-concepts/communication-features)

The C++ setup requires an application and downloaded vendor SDK/runtime. It is a service dependency that should remain optional in source builds. A Discord bot or an ordinary server voice channel does not automatically gain game-relative positions. [Official C++ integration guide](https://docs.discord.com/developers/discord-social-sdk/getting-started/using-c%2B%2B)

### TeamSpeak: two different integration choices

The official **TS3 client plugin** API exposes listener position/orientation, per-client 3D positions, distance settings and individual volume controls. A plugin can consume a small authenticated game-to-plugin position feed while the external TeamSpeak client owns voice capture and transport. This is different from bundling an embedded SDK. [Official TS3 plugin API header](https://github.com/teamspeak/ts3client-pluginsdk/blob/master/include/ts3_functions.h)

TeamSpeak currently lists a C/C++ SDK and a separate client plugin SDK in its downloads. Business/SDK integration is offered with vendor pricing and terms; do not infer an unrestricted redistributable OSS voice stack from the availability of headers. Confirm supported client/API versions and redistribution requirements before choosing the adapter. [SDK downloads](https://teamspeak.com/en/downloads?product=sdk), [Business/SDK licensing information](https://teamspeak.com/en/features/licensing/)

### Mumble: native Link support and an important isolation detail

Mumble Link accepts avatar and camera position/orientation, session context and identity through shared memory. Positions are meters; front/up are orthogonal unit vectors in its documented coordinate system. Native Link avoids scraping game memory. Its example code is published for free reuse. [Official Link integration](https://www.mumble.info/documentation/developer/positional-audio/link-plugin/)

**A different Mumble context is not a mute boundary.** The documentation says mismatched contexts remove positional metadata and voice is then heard normally. Separate dungeon instances therefore need server channels/permissions or explicit routing, not only a different context string. Also test users whose positional plugin is disabled. [Mumble positional audio behavior](https://www.mumble.info/documentation/user/positional-audio/)

### WebRTC/Opus: ownership with more engineering

Opus supplies a BSD-licensed reference codec and royalty-free terms as documented by its maintainers. A codec alone does not implement capture, transport, authentication, echo cancellation, a jitter buffer or spatial playback. [Opus licensing](https://opus-codec.org/license/)

WebRTC offers media transport, but signaling must be supplied separately and connectivity involves ICE/STUN/TURN. A native C++ game still needs a supported implementation and an audio bridge; browser examples are not drop-in SDL code. [WebRTC peer connections and signaling](https://webrtc.org/getting-started/peer-connections)

For a larger hub, our proposed topology is a media relay/SFU with recipient interest sets, rather than a connection to every other visitor. Keep individual speakers as separate streams so each listener can pan and attenuate them. Use an established secure media stack rather than inventing crypto over raw UDP. [WebRTC security architecture, RFC 8827](https://datatracker.ietf.org/doc/html/rfc8827), [Opus over RTP, RFC 7587](https://datatracker.ietf.org/doc/html/rfc7587)

## 5. Provider-independent positional contract

The proposed game-side voice service consumes a read-only world snapshot, independent of the rendering camera and independent of gameplay turn transport:

```text
VoiceEntityState:
  protocolVersion, sessionId, realmId, instanceId, levelId
  stableEntityId, authoritativeTick, sampleTime
  footPosition: (x, z) in game world units
  mouthHeight: physical height in world units
  facing: normalized physical forward vector
  membershipEpoch, voiceEnabled, localMuteState
```

These are proposed fields, not structs or packets present in the engine. The same dungeon floor number in different instances must not match. For legacy co-op, scope by session + dungeon/set-level identity; for a hub, add shard/instance identity. Voice-provider user IDs must be bound to stable game identities rather than names or a reusable four-player slot.

Use the actor's logical footpoint and movement interpolation, with a documented world-unit-to-meter scale. Original art does not establish a real-world meter scale. Never use native projected pixel coordinates or the avatar hull's inferred depth as network positions. Speaker position is near the actor's mouth. Listener position remains near the local avatar; an orbit camera hundreds of units away must not make nearby players inaudible. Camera yaw can orient the listener consistently with the view, without using camera zoom or orbit radius for distance.

Proposed initial attenuation: full volume inside `nearRadius`, smooth fade to zero by `farRadius`; reject speakers outside the permitted world membership first. Radii and coordinate conversion are tunable experiment parameters. Add hysteresis to recipient subscription changes and interpolate timestamped positions to avoid pops. Wall occlusion is a later feature based on gameplay geometry; a cosmetic 3D mesh must not decide who belongs to a voice room.

Audio capture/playback and transport run outside the simulation thread. Audio callbacks read immutable snapshots through a bounded queue; they do not mutate `Players`, execute game commands, wait on networking, or build geometry. Voice packets must not enter the reliable gameplay turn queue. A provider disconnect, mute toggle, or microphone failure must never stall movement or combat.

### Required player controls and boundaries

- Voice disabled until the player opts in; no microphone capture before the choice.
- PTT as the initial default, with a configurable key that does not conflict with game input; optional voice activation only after explicit selection.
- Local mute/deafen, per-speaker mute and volume, block behavior, input/output device selection and speaking indicators.
- Separate party, proximity and optional community channels with clear scope; joining a town does not silently join a public microphone session.
- End capture and remove subscriptions on leave/disconnect; revoke old membership before completing an instance transfer. A stale/unknown entity is silent.
- No raw audio recording or retention by default. Audio diagnostics require a separate explicit choice. Do not put provider tokens in logs or commit secrets into the repository.

A client-side distance fade improves presentation, but a modified client can bypass it. If hearing another instance is a boundary the project intends to enforce, the service must avoid forwarding those streams, rather than relying on a volume value of zero.

## 6. Staged experiments and acceptance gates

Every stage below is **unimplemented**. Numbers are proposed test targets, not performance claims or a release schedule.

| Stage | Small deliverable | Evidence required before proceeding |
| --- | --- | --- |
| N0: restore the existing network build | Optional network build using TCP first; keep `MAX_PLRS=4` | Two then four separate clients, 2D/3D toggling and orbiting independently, join/leave/rejoin, level travel, item pickup, portal and save/reload checks. Same simulation outcomes with either renderer; packet/turn logging and a controlled latency/loss exercise. |
| V0: external positional voice | Optional Mumble Link adapter for one four-player session | Explicit opt-in/PTT/mute, correct direction after orbit, no distance change on zoom, disconnect cleanup, and two identical floor numbers in different sessions with no cross-room audio. Test plugin-off users too. |
| N1: unattended party host | One headless legacy worker with isolated save/config state | Document real participant capacity including any idle host slot; clean shutdown/restart, resync after late join, host loss/recovery, and a repeatable soak test. No claim of authoritative economy. |
| N2: shared social town | Separate hub entity replication; target 16 simulated visitors before real clients | Stable IDs, bounded queues/bandwidth, movement interpolation, reconnects, and no use of extra hub visitors as legacy combat players. Publish CPU/memory/bandwidth, latency percentiles, hardware and scenario. Try 32 only after the 16-client gate passes. |
| N3: instance allocation and transfer | Two concurrent four-player dungeon workers connected to a hub | Same floor numbers remain isolated; seed/quest/item/portal state does not leak; transfer/reconnect cannot duplicate membership or inventory. Decide and label legacy trust versus authoritative progression. |
| V1: embedded voice provider | One provider adapter, with an optional build flag | Independent per-speaker spatial playback, no double audio, failed authentication/service recovery, device hotplug and performance tests. Discord production approval or TeamSpeak SDK terms verified if selected. |
| N4: authoritative public progression | Server-owned character revisions and combat/inventory outcomes | Versioned protocol, replayable input/event tests, server validation, transactional persistence, migration policy and load/abuse testing. Choose capacity only from measured results. |

An eight-player dungeon test may be a separate research branch after N0; it must include a complete player-ID/mask/protocol/save audit and should not delay a clean separation of hub entities. A town with 32 visitors plus many four-player workers is architecturally different from a 32-player dungeon.

## 7. Contributor boundaries

Keep these experiments independent from procedural 3D reconstruction. A renderer or mesh contribution should not change multiplayer simulation, character saves or collision to make a visual test pass. Network tests must exercise a 2D client alongside a 3D client where protocol compatibility is intended.

Suggested modules are a world-state observer, provider-neutral voice interface, optional provider adapters, hub protocol/service, and a dungeon-worker supervisor. Keep vendor runtimes, credentials, private MPQs, native sprite exports and generated diagnostics out of source commits. Record third-party notices and build requirements for any adapter that is actually implemented. The default build must remain usable without a voice account or provider SDK.

No server was deployed, no microphone was accessed, no voice SDK was installed, and no player capacity increase was implemented during this research. The current smoke tests are rendering/geometry diagnostics; they are not multiplayer or voice acceptance evidence.
