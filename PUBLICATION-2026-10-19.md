# Development snapshot — 19 October 2026

This branch publishes the current local development files, documentation, authoring tools, generated concept studies and the separately frozen R15 Source checkpoint. It is a preservation snapshot, not a claim that all work in progress has passed the release gates.

- `checkpoints/r15/Source/`: public source portion of the tested R15 local executable; the manifest records the full closure and explicitly lists two withheld embedded data headers. See its qualification summary.
- Root `Source/`, CMake, editor and tools: current work in progress, retained without overwriting the selected R15 source.
- Concept art remains candidate art; it is not a completed model, accepted runtime asset or complete distributable art pack.

The pre-existing remote `main` history and the local working branch were left intact. This branch is based on the local development parent; it must be integrated deliberately rather than force-pushed over newer remote work. No public binary release or website deployment is implied.

Excluded under AGENTS.md and ASSET-APPROVAL.md: original MPQ/extracted game data, saves/profiles, private Meshy or other models/textures, owner-supplied private music, credentials, local build outputs and raw browser/runtime diagnostic dumps. All excluded material stays on the local machine.
