# Model animation contract — planned integration

**Status: design proposal; skeletal animation is not implemented.** This document
defines how a future rigged actor would follow the existing game. It does not
change combat, movement, networking, saves, or the current asset format.

Resumo em português: podemos sincronizar um modelo com esqueleto às animações
originais usando o estado e o tempo do próprio Diablo. O rig deve apenas desenhar
a pose; impactos, projéteis, colisões e posição continuam controlados pelo jogo.
Os modelos importados atuais são estáticos. Ainda precisamos de esqueleto,
pesos, clips e um caminho de renderização com skinning para personagens rigados.

## What exists today

- [D3DMESH1](../Source/engine/render/town_model_import.hpp) contains triangles,
  positions, UVs and one RGB texture. It has no bones, skin weights, joint
  hierarchy, animation tracks or clip metadata. Its town override changes visual
  geometry while retaining native map/collision metadata.
- [DrawPlayerVolume and DrawDirectionalActorVolume](../Source/engine/render/town_view.cpp)
  read the player's current graphic, direction and
  `AnimInfo.getFrameToUseForRendering()`. For eight compatible sprite directions,
  they reconstruct and cache a physical hull of that particular native pose.
  This is frame-based reconstruction, not deformation of one persistent rig.
  Missing/incompatible directions fall back to the current sprite volume.
- [BuildTownActorVisualHull](../Source/engine/render/town_actor.hpp) describes the
  native directional projection and grounding transform. No skeleton can be
  recovered from that public mesh contract alone.
- The imported cabin is a static town asset. It does not establish a rigged
  player or monster pipeline. Town rendering is the current integration scope;
  dungeon monsters require a separate integration and acceptance pass.

## The game owns time and events

Use [AnimationInfo](../Source/engine/animationinfo.h) as the source of timing:
`currentFrame`, `numberOfFrames`, `ticksPerFrame`,
`tickCounterOfCurrentFrame`, petrification and the existing render accessors.
Frames in this API are zero-based. The default gameplay rate is 20 ticks per
second, but it is configurable; an authored clip must not assume a fixed rate.
See [options.cpp](../Source/options.cpp) and [multi.cpp](../Source/multi.cpp).

The logical frame and the displayed frame are intentionally distinct.
[animationinfo.cpp](../Source/engine/animationinfo.cpp) redistributes displayed
frames for skipped startup frames, faster attacks, repeated attacks and input
previews. `getFrameToUseForRendering()` can still show the tail of the previous
action while simulation has started a repeated action. A rig adapter must not
replace that behavior with a second wall-clock timer.

For an initial integration, a native-frame-to-clip-time table can sample a key
pose at the existing displayed frame. Smooth interpolation is a subsequent
render-only extension: expose continuous displayed-frame position from the
same `AnimationInfo` calculation, including its clamp, previous-action tail,
preview and freeze rules. Do not estimate all combat phases with
`currentFrame / (numberOfFrames - 1)`. `getAnimationProgress()` is already used
for walking but is not a substitute for every displayed-frame distribution
case. No new continuous accessor is implemented by this document.

An adapter snapshot should identify actor kind/type, class/equipment variant,
visual graphic/state, facing, displayed frame, logical frame, frame count,
frame delay, freeze/reverse/terminal state and authoritative world footpoint.
The renderer may read this snapshot; it must not advance `AnimationInfo`.

## Clip mapping and event anchors

Player state mapping comes from [Player::getGraphic](../Source/player.cpp).
Walking northwards, southwards and sideways all select `Walk`; melee and ranged
attacks share `Attack`. Casting selects `Fire`, `Lightning` or `Magic` by spell
type. Standing, blocking, hit recovery and death have their own graphics.

| Action | Existing logical marker to preserve | Visual acceptance |
| --- | --- | --- |
| Player melee | `DoAttack`: swing sound at `_pAFNum - 2`; hit resolution at `_pAFNum - 1` | Weapon contact pose coincides with the existing hit tick, including faster/repeated attacks. |
| Player bow | `DoRangeAttack`: primary projectile at `_pAFNum - 1`; multiple-arrow effect at `_pAFNum + 1` | Draw/release pose agrees with each existing projectile tick. |
| Player spell | `DoSpell`: cast at `_pSFNum` | Casting gesture agrees with that marker; do not subtract one by analogy with melee. |
| Player walk | `DoWalk` and `GetOffsetForWalking` | Feet/root follow the existing subtile displacement and tile transition. |
| Player block/hit | `DoBlock`/`DoGotHit`: state ends at the native last frame | Do not prolong recovery to finish an authored clip. |
| Player death | `DoDeath` and `ProcessPlayers`: terminal frame is held and controls dead-player state | End pose remains held; no automatic loop or idle transition. |
| Monster normal melee/ranged | `MonsterAttack`/`MonsterRangedAttack`: `animFrameNum - 1` | Contact/release coincides with that species' existing event. |
| Monster special | Special attack handlers use `animFrameNumSpecial - 1`; additional species-specific conditions exist | A mapping supports multiple anchors and held frames, rather than one universal hit fraction. |
| Monster death/fade/petrification | `MonsterDeath`, fade handlers, `ProcessMonsters`, `getVisualMonsterMode()` | Hold/reverse/freeze the pose as the native state requires. |

Player handlers are in [player.cpp](../Source/player.cpp); monster handlers are
in [monster.cpp](../Source/monster.cpp). Magma and storm monsters have additional
attack frames; the Mega special attack can hold its action frame. These are
examples, not an exhaustive event inventory. Diablo's death also drives the
ending sequence, so a generic animation-end callback must not replace it.

The rig's event annotations are calibration markers only. They do **not** emit
damage, missiles, footsteps, inventory changes or network messages. Existing
handlers remain the only authority. If smooth clip sampling is introduced,
time remapping must preserve each action anchor and interrupted segment.

## Class, equipment and species variants

Read effective runtime values after `SetPlrAnims` and animation loading. The
[class tables](../assets/txtdata/classes) vary by class, town/dungeon and weapon.
For example, the base tables contain:

| Class | Sword frames / configured action frame | Bow frames / configured action frame | Cast frames / configured action frame | Hit-recovery frames |
| --- | --- | --- | --- | --- |
| Warrior | 16 / 9 | 16 / 11 | 20 / 14 | 6 |
| Rogue | 18 / 10 | 12 / 7 | 16 / 12 | 7 |
| Sorcerer | 16 / 12 | 20 / 16 | 12 / 8 | 8 |

These configured action values are interpreted by the handlers in the table
above; they are not all direct zero-based hit indices. Hellfire classes,
equipment changes and compatibility adjustments must use their own effective
values. `AnimationInfo::changeAnimationData` can alter an in-progress animation
after gear changes; retain its frame validity behavior.

Monster [graphics and modes](../Source/monster.h) include Stand, Walk, Attack,
GotHit, Death and optional Special. Frame counts/rates come from
[monstdat.tsv](../assets/txtdata/monsters/monstdat.tsv). A source can have a
directional sheet or a single sprite list; do not require eight views for every
monster. Unique recoloring and special AI behavior do not imply a new skeleton.

## World placement and rig reuse

The game owns facing and the root path. Preserve the eight-direction order in
[direction.hpp](../Source/engine/direction.hpp). Root translation must use
native actor placement and [GetOffsetForWalking](../Source/engine/render/scrollrt.cpp).
For the current town transform, a screen offset `(dx,dy)` converts to
`worldX += (2*dy + dx)/64`, `worldZ += (2*dy - dx)/64`; see `PlayerPosition`
in [town_view.cpp](../Source/engine/render/town_view.cpp). Do not apply authored
root motion on top of that displacement. A clip may animate hips/limbs around
the authoritative root, but it cannot move occupied tiles or collision bounds.

Ground the bind pose to a declared foot origin and unit scale. Preserve foot
contact throughout walking, while allowing lifted feet and death poses. Camera
orbit changes the view, not actor facing. Selection continues to identify the
native actor, and attached weapon visuals do not extend gameplay reach.

A shared humanoid rig is a useful authored contract when joint hierarchy,
rest-pose orientation, attachment names and units agree. New meshes still need
valid skin weights and may need retarget calibration for proportions. A static
OBJ or unskinned GLB does not acquire a usable rig merely by reusing a clip.
Quadrupeds, winged creatures and bosses with different anatomy need compatible
rig families or separate rigs. Automatic rigging, if later selected, must be
validated for the actual model; it is not a capability of D3DMESH1.

A future asset format must explicitly carry joint hierarchy/inverse bind
matrices, weighted vertices, clip channels, rig identity and state/variant
mapping. Keep this separate from the existing static format rather than
silently interpreting static triangle data as skeletal animation.

## Acceptance before enabling a rig

1. Compare the same actor/variant/direction at native start, impact/release,
   recovery and terminal frames. First confirm the original camera; also inspect
   90/180/270 degrees and slight turns.
2. Replay movement in all eight directions, town running and state transitions.
   Check authoritative footpoint continuity, ground contact, camera follow and
   unchanged occupied tiles. A root-motion clip must not create double movement.
3. Compare native logical event ticks for attacks, spell release, multiple
   projectiles, hit interruption, death and species-specific held/reverse states.
   Repeat under speed modifiers and at different configured tick rates.
4. Test rapid repeats, action cancellation, input previews, gear changes,
   petrification, pause and a loaded save. Camera movement must not change pose
   time or simulation state.
5. Verify finite skin transforms, valid joint indices/weights, reasonable
   deformation, stable selection and equipment attachments. Unsupported or
   invalid assets retain the native visual fallback.
6. Separate source-resolution improvement from enlargement. New reference art
   is accepted only after comparing decoded frames/opacity and provenance;
   bigger screenshots do not establish higher-detail sprites or a valid rig.

This contract specifies future acceptance. Current static-model and town hull
checks do not prove that a future skeletal implementation satisfies it.
