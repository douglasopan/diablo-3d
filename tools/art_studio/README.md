# Preparação de arte — candidatos offline

Ferramentas utilizáveis em Python 3.10+, Pillow e NumPy. Nenhuma instalação no jogo, seleção de revisão, chamada paga ou alteração de master. A integração nos formatos/slots nativos continua com os importadores e gates existentes.

## Recorte, transparência e registro

`studio.py sprites <frames...> --size 256x256 --pivot X,Y --output <nova-pasta>`.

Use frames com o mesmo canvas e pivô de autoria. A ferramenta usa bounds/escala comuns para toda a sequência: não recentra cada pose nem muda o tamanho do personagem por frame. `--pivot` deve vir do contrato de autoria; ausência é apenas enquadramento pelo conteúdo, não calibração nativa. RGBA existente é preservado. Para fundo sólido explícito, use `--background 255,255,255`; flood fill remove apenas o fundo conectado às bordas e preserva detalhes brancos fechados. Rever casos em que roupa clara toca o fundo, cabelo semitransparente ou sombra faz parte do master. `--pixel-art` usa nearest; o padrão HD usa reamostragem premultiplicada. Saída: frames, receipt com hashes/registro e registration-preview.png. O pivô raster não substitui o pivô CLX nativo.

## Texturas

`studio.py texture <master.png> --output <nova-pasta>` produz prévia 3×3 e medidas de descontinuidade das bordas versus variação interna. Essas medidas ajudam a detectar emendas, mas não aprovam estilo ou repetição visual. Não aplica blur/seamless automático ao master. Próxima produção mantém identidade única da Catedral: alvenaria, dimensão real, pedra, paleta, desgaste e escala consistentes; revisão sob luz diurna/entardecer/noturna e CPU/GPU precede seleção. Normal/roughness/PBR são masters separados; o renderer atual não os consome plenamente.

## NPCs MakeHuman

`npc_body.py --age .4 --muscle .2 --weight .3 --feminine 0 --output <nova-pasta>` gera OBJ candidato com morphs de dados CC0, mantendo topology/índices e fontes de rig/weights. É uma receita limitada de autoria, não equivalente a todos os sliders oficiais. Rig de origem tem 163 ossos; nosso ActorModel admite até96. Reconstrução dos anchors no corpo morphed, rig reduzido/retarget, roupas/cabelo/skin, animação e export D3DACTR1 ainda são necessários. Zero clips incluídos. Não altera o Warrior selecionado.

## Warrior

`warrior_retarget_probe.py --source <donor.glb> --target <warrior-reparado.glb> --clip-index 0 --output <nova-pasta>` reutiliza o retarget por delta de bind já existente e exige correspondência exata de todos os ossos-alvo. Preserva mesh, textura, IBMs e nós; gera movimento real derivado do donor e receipt. Não estabelece licença de redistribuição do donor nem sincroniza eventos nativos. Source planar hips é removido explicitamente apenas no derivado; não desloca player. Contato de chão, sentido do golpe, mãos/arma, loop e duração devem passar em toda a sequência. Saída técnica/rejeitada não entra no gameplay.

## Proveniência e checks

Universal Modder `sprite.py/common.py` MIT, revisão em `vendor/upstream.json`; adaptação pequena `trim_result=False` evita perder registro. Licença original incluída. Não instalamos plugin/hooks/fal/ComfyUI.

MakeHuman core data e licença em `assets/character-sources/makehuman-r1/provenance.json`, pin e hashes verificados pelo gerador; código AGPL da aplicação não foi importado. Assets comunitários adicionais exigem licença própria.

`python -m unittest discover -s tools/art_studio -p 'test_*.py'` cobre deslocamento relativo/registro, detalhes interiores, preservação de master, overwrite recusado, emenda de textura e morph sem alterar topology. Não qualifica runtime, arte ou animações.
# Weapon socket diagnostic

`weapon_socket_reference.py BODY.glb CALIBRATION.json --output reference.txt`
samples the calibrated socket at 1,025 phases using the independent GLB evaluator.
It verifies the body hash and refuses an existing output. The C++
`weapon_socket_smoke.cpp` takes `BODY.d3dactor reference.txt settings.txt`, where
settings contain translation XYZ, quaternion XYZW and uniform scale. It compares
the actual native loader, pose evaluator and attachment helper against that
reference. This proves matrix agreement only; finger closure, weapon identity,
equipment coverage and runtime integration require separate validation.

`weapon_grip_candidate.py BODY.glb WEAPON.glb CALIBRATION.json --output new-dir`
authors a diagnostic finger-only grip using one bounded flexion hinge per
phalanx and the calibrated handle axis. Preserves wrist, nonfinger movement,
mesh and master; emits a derived GLB, front/side close-ups and fit residuals.
Solver convergence is not a contact or anatomy gate. Review per-finger reach,
thumb opposition and mesh intersections before any runtime selection.

A compatible clip can reuse `--grip-recipe receipt.json --grip-reference original-body.glb`;
the reference hash and exact skeleton, mesh and material layout must agree.
`grip_surface_audit.py BODY.glb WEAPON.glb CALIBRATION.json --handle-min=X,Y,Z --handle-max=X,Y,Z --output new.json`
measures distal weighted skin against actual handle faces selected by source-space
triangle centroids. Distances are unsigned: proximity does not prove that the
skin stays outside the handle. Source grip position must match the actual handle,
not the guard or an estimated wrist midpoint. Two analytic tests cover interior
face projection, edge/vertex distance, degenerates and transformed scale.

`guard_idle_candidate.py source-guard.glb --output new-dir` authors a subtle
upper-body breathing loop from the source clip's initial guarded pose. The
source skeleton, mesh, material and binary prefix remain intact; only five
upper-body rotational tracks vary. It reports real mesh motion, fixed nonupper
local transforms, ground clearance and loop seam. This is a new authored 3D
animation candidate, not gameplay coverage, distribution approval or native
timing integration. The native pose smoke also reports maximum mesh motion so
a repeated still cannot be mistaken for a tested animation.

`guard_hit_candidate.py source-guard.glb --output new-dir` authors a five-key
upper-body recoil/recovery candidate while preserving pelvis, legs and grip.
Native combat remains responsible for damage and action timing. Review the
peak at phase .15, not only the midpoint; the weapon preview accepts
`--preview-phases 0,.15,.6,1` to expose short action peaks. The native pose
evaluator checks the exported geometry independently; neither check selects
the model or replaces missing block/cast/death/equipment coverage.

`guard_cast_candidate.py source-guard.glb --kind fire|lightning|magic --output new-dir`
authors a left-hand windup/release/recovery gesture with bounded arm/elbow IK
and a palm basis inferred from the actual rig. The right-hand grip, legs and
root remain unchanged. The authored release plateau covers phase 13/19, which
must be mapped from native emission frames by the player binding helper.
The helper's emission anchor emits no spell and rejects attack/emission marker
combinations, loops and mismatched actions. Three authored clips do not prove
runtime graphic selection or gameplay; inspect each variant and equipment set.

`guard_block_candidate.py source-guard.glb --output new-dir` authors a reactive
left-arm brace followed by guard recovery. Its first pose is already raised to
fit the native two-frame block context. It has no shield mesh or left-hand grip
approval and cannot qualify the starter Warrior's sword-and-shield equipment.

`merge_actor_clips.py --clip idle=idle.glb --clip walk=walk.glb ... --output new-dir`
bundles clips while keeping one static actor payload. It compares resolved
geometry, weights, inverse binds, rig, materials and embedded image bytes,
copies only animation views/accessors, and checks source/bundle world matrices
at all source keys plus regular samples. Explicit role names are labels, not
evidence of full gameplay coverage. `actor_bundle_smoke.cpp` independently loads
the native bundle and evaluates each clip; incompatible equipment or textures
are still admission failures.

`guard_walk_entry_candidate.py guard.glb walk.glb --output new-dir` authors a
one-shot entry before the original walk loop, with bounded foot rotations.
It is not a general runtime crossfade and must not restart on each native tile
step. `--refine-from rejected-entry.glb` inserts sampled contact keys only where
the first quarter still violates the safety margin. Dense interpolation checks
must pass; endpoint/keys alone cannot approve floor contact. No simulation
movement, model scale or static placement offset is altered.

`guard_walk_exit_candidate.py guard.glb walk.glb --output new-dir` authors a
one-shot exit from the terminal walk pose to the exact guard. Dense contact
checks and bounded foot rotations preserve rig lengths, scales, fingers and
static asset bytes. It does not handle an arbitrary mid-walk interruption.

`actor_player_geometry_smoke.cpp` tests native actor geometry preparation:
captured render fraction, uniform asset fit, native yaw and footpoint including
Y, with owned posed vertices/indices for later projected triangle submission.
The geometry module lives in the isolated Source checkout. This test does not
qualify material/palette, renderer, picking, player activation or gameplay.

`warrior_admission_smoke.cpp` tests the isolated `warrior_actor_admission.hpp`
policy. Diagnostic rows cannot suppress sprites; the global gameplay matrix
is not an admission policy for a narrower playable pilot. Caller-owned review
receipts, package verification and lifecycle still need a production owner.

`starter_shield_candidate.py` authors an original geometric buckler from the
existing concept, with front/back/rim/boss/grip and uniform palm-basis socket.
Candidate views and dense equipment-floor probes expose defects; open fingers
and a shield intersecting the floor during death are not a qualified loadout.

`shield_grip_candidate.py` derives a left-hand grasp with 15 bounded finger
rotations and a source/rig/socket checked reusable recipe. Its signed sampled
skin-vs-grip containment is stronger than unsigned distal proximity, but does
not prove all triangle intersections or anatomy. Varying source finger motion
requires explicit `--allow-finger-motion-clamp`, recorded in the receipt.

`death_shield_contact_candidate.py` corrects a specified shield/socket's ground
contact using only three left-arm/hand rotations. A new socket requires a new
proof; compose the finger recipe and qualify actual final skin separately.

`actor_player_frame_reference.py --output new-dir` and
`actor_player_frame_smoke.cpp` compare body-plus-rigid-gear preparation with
independent GLB socket matrices and actual source mesh parts. No material
identity is collapsed. Normals use inverse transpose; a complete frame failure
preserves every prior member. This checks geometry, not the native dispatcher,
rendering, textures/palette, gameplay admission or item art identity.

`native_spell_emission_marker_smoke.cpp` is generated from the entire exact
native `DoSpell` body and current capture statement for an isolated side-effect
stub fixture. The current marker is `_pSFNum`, not `_pSFNum-1`; attack still uses
`_pAFNum-1`. Cross-check with the actual Player capture regression before any
host integration; the extracted fixture is not the whole application.

