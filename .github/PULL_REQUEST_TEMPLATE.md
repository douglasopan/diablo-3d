## Problem and resulting behavior

Describe the concrete defect or feature and what changes for the player or contributor. Link the issue and name the affected object/subsystem.

## Implementation and provenance

Explain the approach, whole-object bounds and important tradeoffs. For assets, include author, license, source, tool/version, transformations, scale/origin and material/UV information. Keep game-data extracts and credentials out of this PR.

## Validation

State the build/commit, data edition and fixture. Report checks actually run and their result; mark untested cases explicitly.

- Original backend and forced-mesh native-angle comparison:
- Relevant 360° views, picking, occlusion and actor motion:
- Closed/outward finite geometry, depth, ground contact and deterministic reload:
- Preserved collision, simulation state and game seed:
- Performance or lighting/shadow evidence when relevant:

Home pixel equality validates original-backend reuse; it does not prove mesh fidelity. Current shadow decals are compatibility artwork, not dynamic 3D shadows. The supplied `NONET` build does not demonstrate multiplayer or voice support.

## Remaining differences and follow-up

List visual differences, unknown sides, dependencies and material risks that remain. Attach only evidence you may publish.

## Checklist

- [ ] The change is scoped and its validation is reproducible.
- [ ] Original gameplay rules and RNG behavior are preserved unless the issue explicitly changes them.
- [ ] I have preserved existing license/copyright notices and documented new asset provenance.
- [ ] No game archives, extracted proprietary artwork, saves, credentials or protected secret blobs are included.
- [ ] Implemented behavior and planned work are described separately.

Português: explique o problema, a mudança, a origem dos recursos e os testes realizados. Separe a imagem original das malhas 3D e declare as limitações.
