# Runtime baseline and asset review

`assets/runtime-baseline.json` selects the same local review baseline for the normal launcher, Nítido/Suave/Amplo presets and `-QualityReview`. It reuses catalog ID `tristram.arch.cabin`, east-long layout. Selection is **not acceptance**: the current status is `review`, `accepted: false`; full 360-degree fidelity, openings/interior and roof matching still require review. The registry's acceptance criteria remain in [ASSET-CATALOG.md](ASSET-CATALOG.md).

The manifest contains metadata and exact SHA-256 values only. The private model path is relative to the workspace: `models/meshy/cabin-east-v2-multiview/runtime/cabin-east.d3d`. No generated model, texture, game extract or save belongs in Git. A later candidate requires an explicit manifest revision and review; the launcher never selects files by modification date or “latest” filename.

O usuário aprovou continuar a partir deste exterior, da tonalidade e da direção de iluminação quente. Esse escopo parcial fica registrado separadamente: base, interior, aberturas, telhado e revisão em 360° continuam pendentes. A abertura frontal foi medida no modelo importado; a traseira é uma reconstrução inferida e solicitada pelo usuário para revisão, não uma medida comprovada da referência original.

## O que muda nos atalhos

Todos os modos instalam a mesma cabana e iluminação selecionadas quando as fontes locais estão disponíveis. Os perfis continuam separados e um perfil existente conserva seus saves. Um perfil novo recebe somente a cópia inicial de configuração e saves da edição correta (`single_*.sv` para Diablo completo; `spawn_*.sv` para shareware).

O launcher verifica o hash da fonte antes de modificar o perfil, verifica a cópia e confere o destino. Um modelo ou arquivo de iluminação divergente no perfil é preservado e a preparação para com uma mensagem; não é substituído silenciosamente. Separe conscientemente esse override antes de trocar a seleção. `-MeshyReview` exige as fontes locais selecionadas. A ausência de fontes no uso comum permite um override já instalado com o hash correto; sem ele, o launcher anuncia o fallback procedural e a ausência do interior Meshy. A iluminação ausente usa os valores internos do renderer com aviso explícito.

`-PrepareOnly` prepara os arquivos e o recibo sem iniciar o jogo. A baseline será usada na próxima inicialização ou recarga da cena; a preparação não troca uma cena já cacheada em uma partida aberta. Não feche instâncias do usuário para validar. Exemplos:

```powershell
.\Iniciar-Tristram.ps1 -Presentation Nitido -PrepareOnly
.\Iniciar-Tristram.ps1 -QualityReview -PrepareOnly
.\Iniciar-Tristram.ps1 -MeshyReview -PrepareOnly
```

## Local receipt and code-generated openings

Each prepared profile receives `runtime-baseline-receipt.json`, inside the ignored profile directory. It records the selected ID/revision/status, actual model/executable/lighting hashes, manifest hash, requested presentation and the opening pipeline ID. It contains no save data, player identity, credentials or log contents.

Pipeline `cabin-east-openings-fire-v2` declares the expected executable adjunct: one measured front opening, an inferred rear opening under review, the room, timber floor and two candle sources. These changes do **not** require a newer `.d3d` file. The original selected mesh/UV source stays unchanged. This declaration is not proof that an arbitrary executable implements that pipeline. Consequently, a model hash alone cannot prove that the interior is present; retain the executable hash and compare actual forced-geometry/window/orbit evidence. Home's original backend is not mesh approval.

The receipt is a reproducibility record of the launcher's prepared files, not an automatic artistic approval or proof of arbitrary third-party mod precedence. Audited hashes identify the known local v4 and quality candidates; the receipt alone cannot validate compatibility of another executable. It does not validate every triangle; the engine's bounded importer and the geometry/lighting smoke tests remain responsible for runtime validation.

## Verification

Run `tools/test_runtime_baseline.ps1` for isolated synthetic fixtures: matching sources, absent/invalid sources, divergent overrides, profile/save isolation, palette-independent asset selection and `PrepareOnly` without launching an executable. No paid generation, source model regeneration or game build is involved.

Validation on 2026-10-07: 20/20 synthetic cases passed in Windows PowerShell 5.1. Nine local profiles were prepared with the identical selected model and lighting hashes, then prepared again after the normal executable was relinked. The seven pre-existing saves retained their content hashes, sizes and modification timestamps; the repeat also preserved the two initial comparison-profile copies. The normal profile's INI remained byte-identical. No game was launched by these checks, and no visual acceptance was inferred.
