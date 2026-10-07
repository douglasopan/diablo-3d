# Physical openings and fire lighting

This is the authoring and review standard for buildings throughout Diablo 1, with Tristram as the first validation fixture. An opening must exist in geometry and light visibility, rather than being simulated by a dark or glowing patch on an opaque wall. Separate openings confirmed by the original reference from artistic inferences explicitly approved for the 3D design. Both need physical geometry and light boundaries; neither changes the original map's walkability.

**Resumo em português:** o padrão vale para Diablo 1 inteiro, começando por Tristram. Medir e registrar aberturas de todas as faces; distinguir referências originais confirmadas de desenhos inferidos e explicitamente aprovados. Recortar a geometria e usar a mesma forma para a luz. Velas, candelabros e fogueiras usam emissores físicos com oscilação discreta, sem alterar o acaso ou a colisão do jogo. Validar frente, costas, laterais e telhado em 360°.

## Record the complete building before modeling

Measure the original reference at a known camera pose and scale. Keep references, source archives and extracted art local unless a specific contextual publication has been authorized. Record the object origin, world X/height/Z axes, physical bounds, floor, wall thickness, roof profile and door/window positions. Distinguish painted trim, glass and deep shadow from an actual opening.

Create a face inventory for front, rear, both sides, roof slopes and underside. For each door, window or gap, record:

- Its stable identifier, face, plane or surface basis, center, width/height or radius, shape, orientation and depth through the wall.
- Its classification: confirmed by an original reference, or an explicitly approved artistic inference. Record the measured evidence or the approved design reference and approval context accordingly.
- Whether it is open, closed, glazed, shuttered or partially obstructed in the represented state.
- Its lintel, sill, frame, shutters, glass, bars or wooden divisions, and the interior surface behind it.
- Its visual and light-transmission behavior, separately from native collision and interaction.

An absent rear reference does not establish that the original building had a rear window. An explicitly approved inferred design may add that window, as requested for the cabin, but must be labeled as an artistic decision rather than a verified historical reconstruction. Keep other unverified surfaces closed until their design is approved. Correct or reject generated duplicate doors or windows unless they are explicitly accepted as part of that design; preserve native collision in every case. Roof seams and gaps likewise need either source evidence or an approved inferred design, followed by the same physical and lighting review.

## Match the hole, lining and light boundary

Cut the opening through the relevant outer wall and inner shell. Preserve the surrounding masonry or timber. Add a real wall-depth lining, sill and frame; leave clear panes or spaces where a ray can actually reach the interior. Opaque wooden divisions must remain geometry rather than an all-purpose transparency mask.

Use the same aperture shape and orientation for geometry and point-light visibility. The current pure lighting helper supports bounded opaque axis-aligned room shells with several openings. `TownLightAperture` declares an X, Height or Z plane, the plane coordinate and its in-plane bounds. The axes are:

| Plane | u coordinate | v coordinate |
|---|---|---|
| X | world Z | height |
| Height | world X | world Z |
| Z | world X | height |

`polygonSides=0` is a rectangle. Values 3 through 32 describe a regular polygon inscribed in the ellipse of those bounds, with the first vertex toward +u. The calibrated cabin window uses 20 sides, matching its physical cut. A rectangular bounding box around a round window is insufficient: it can transmit light through opaque corner masonry.

Light crossing a room needs an explicit opening at every entry and exit, including the rear face. Rays touching two wall faces at a corner must satisfy both. A blocked rear window must have a complete opaque shell behind it. An open roof gap needs a matching physical light boundary; the current axis-aligned shell API does not establish support for arbitrary tilted roof apertures. Extend the occlusion representation before claiming those cases work.

Avoid hidden surrogate holes, glowing opaque disks and duplicated exterior photos on interior surfaces. Frames, floors, walls and roof undersides need real depth and opaque materials. Keep imported master geometry unchanged when testing a separate runtime adjunct, and report every clipped or added surface.

## Use physical fire emitters

Represent candles, candelabra and campfires as authored physical sources. Store each emitter's stable identity, world position, linear RGB color, radius and intensity. Multiple candles may have separate identities and light contributions; the renderer still needs a bounded active-light selection because the current sampler processes at most eight point lights per call.

`TownFireLightAtTime(source, identity, elapsedSeconds)` produces a reproducible intensity variation for one emitter. The default is at most ±6%, with a maximum configurable variation of ±15%. Identity gives sources different phases; position, radius and source color remain fixed. Evaluate it once per emitter per rendered frame, outside pixel loops. Use render time supplied by the game, never simulation RNG. Freeze the same explicit time for comparison captures.

Warmth belongs in the source's **linear RGB color**. Point-light RGB contributions are added before multiplying the unlit material base color, then encoded and mapped to the game palette. Keep visible flame emission separate from diffuse illumination. Do not warm the texture itself and then apply the same tint again in the shader. The flicker utility modulates illumination; it does not provide a flame mesh, flame animation or shadows from every interior prop.

Use clean base-color materials without painted floor shadows or a new light baked into the image. Preserve full-resolution material masters and provenance locally. Imported models, references and generated textures must follow the repository's [asset workflow](MESHY-WORKFLOW.md) and [contribution rules](CONTRIBUTING.md); credentials and proprietary art are not authoring deliverables.

## Review the complete object

1. Compare the forced mesh at the original angle with the real original backend. Report material, shape and visibility differences independently; Home's native backend match is not mesh approval.
2. Review a full orbit, close angles, roof views and a camera outside every opening. Check rear walls, frames, lining, floor contact, silhouettes and occlusion of actors.
3. Freeze time and compare fire sources enabled/disabled at the same camera and material settings. Verify warm interior receivers, visible source geometry, distance attenuation, and no illumination through opaque walls or polygon corners.
4. Test light visibility through front and rear openings, through a whole room, and through supported roof gaps, whether reference-confirmed or explicitly approved inferences. Also test blocked panes, shutters, wall corners and roof regions without gaps. Tilted roof gaps require the occlusion support described above before they can pass this review.
5. Audit finite triangles, useful depth, intended shell closure, normals, material sides, UVs and aperture bounds. A topology check does not replace the visual and light-visibility review.
6. Verify picking, actor occlusion, reload behavior and unchanged native map, triggers, pathing and collision. A visible open doorway can still be blocked by the original simulation; document that distinction.
7. Record active light count, frame cost, cache memory and preparation cost. Test several fire emitters and camera poses so performance or visibility errors cannot be hidden by one favorable capture.

The first completed fixture should establish a reusable building contract. Apply it to other buildings from their own references and approved design decisions; do not copy the cabin's aperture positions or silently add openings to make another model appear lively.
