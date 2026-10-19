# Auditoria dos heróis — 12/10/2026

Escopo local de diagnóstico para Warrior → Rogue → Sorcerer. Nenhum Source, build, instalação, seleção global ou NPC foi alterado. O JSON contém hashes, tabelas e metadados completos; esta auditoria não é um pack gerado.

**Regra humana vigente, 12/10/2026:** gameplay usa **somente sprites para todos os heróis e NPCs, inclusive Warrior e Ogden**, até rig e animações corretos serem validados. Esta auditoria registra o requisito; aplicação na Source/build/instalação pertence ao principal. Masters e revisões anteriores de aparência permanecem preservados; skin, aceitação de aparência estática ou stills novos não autorizam gameplay por mesh animada.

## Reaproveitamento e reservas

O Warrior Tripo já foi produzido na operação `e18a8313-d802-44b6-80bd-b9620409a1ea`, encerrada em 40 créditos históricos (mesh trial 0, textura 20, rig 20). Os recibos antigos de input/ownership são snapshots anteriores; não autorizam nem exigem repetir a geração. O export 2K bloqueado no navegador foi resolvido pelo derivado local existente.

| Fonte preservada | SHA-256 | Conteúdo |
| --- | --- | --- |
| `warrior-light-unarmed-r3-tripo-master-4k.glb` | `bb8c85b9186ea4a687d440762139becbcd1b8ec7e731a5b1f1e9004b981d0da4` | 11,039 triângulos; 0 joints; 0 clips |
| `warrior-light-unarmed-r3-tripo-rigged-4k.glb` | `61565b41d367e04449fdb073105acef0ca4d1a652047cdf8ab994f2fd40c01e9` | 11,039 triângulos; 52 joints; 0 clips |
| `warrior-light-unarmed-r3-tripo-bind-repaired-2k.glb` | `52a097b68e1d9a52f83d7e6443dc7eb6694e9193a3ebc5b7ab17950342b07473` | 11,039 triângulos; 52 joints; 0 clips |
| `warrior-light-unarmed-r3-tripo-bind-basis-repaired-2k.glb` | `1c09cc5e55fbc1f6de5d5db94adae2cbb52a2842e5d1b300b3913f98a4d28507` | 11,039 triângulos; 52 joints; 0 clips |
| `warrior-first-person-forearms-hands-CANDIDATE-r1.glb` | `20e09e2ec2128d960383f506339bb0b6998fb336a037efecefe9b94ba95beecd` | 1,749 triângulos; 52 joints; 0 clips |

O rig bruto tem nodes sem TRS e rest pose defeituosa. O r1 corrigia cancelamento algébrico, mas foi explicitamente não selecionado por base anatômica divergente. O r2, SHA `1c09cc5e55fbc1f6de5d5db94adae2cbb52a2842e5d1b300b3913f98a4d28507`, é a fonte do handoff técnico existente; base B(x,y,z)=(y,z+0,499755859375,x) inferida e validada, sem aprovação artística/integral automática. Os 13 terminais sem IBM têm posições autorais não comprovadas. Preserve os masters 4K e use o derivado 2K identificado. Nenhuma dessas cinco fontes contém animação.

Conceito r3 selecionado historicamente: `D:\Diablo 1 3D\diagnostics\characters-rigs\tripo-warrior-input-20261008\warrior-light-unarmed-a-pose-r3.png`, SHA `1c37f7587debc732aed67e086b9531fb7ed9b9e8a97304163d3c80f6f1a3c9fd`. R1/R2 foram preservados. Um conceito A-pose não é idle, walk ou ataque.

Antes do authoring neste lote local, os registros auditados não identificavam GLB/rig/clips próprios de Rogue/Sorcerer. Agora a frente raiz produziu masters raster PNG próprios em `D:/Diablo 1 3D/assets/own-art/heroes/local-r1/masters`; o JSON lista hashes e metadados atuais. Rogue directions-r1 e Sorcerer directions-r2 são seleções de authoring **candidate-stills**, sem seleção no registry. O manifesto da raiz é `docs/own-art-pack/heroes-local-r1/manifest.json`. A meta reportada pela raiz é 24 direções estáticas (Warrior8/Rogue8/Sorcerer8); direções estáticas e frames derivados não são animação. Não foram identificados GLB/rig/ações compatíveis nativas de Rogue/Sorcerer nesta auditoria. Referências privadas originais de corpo permanecem separadas e nenhum de seus pixels é incorporado. Ausência continua limitada aos nomes/recibos consultados, sem inventário remoto ou arquivos não identificados; gastos novos Meshy/Tripo não estão autorizados.

## Contrato nativo

Direções de índice 0 a 7: **S, SW, W, NW, N, NE, E, SE** (`Source/engine/direction.hpp`). Nove enums gráficos geram onze slots de recursos, pois Stand/Walk têm Town e dungeon separados. O JSON lista ataques de todas as nove classes gráficas de armas, larguras, suffixes e guards.

| Classe | Idle dungeon/town | Walk dungeon/town | Hit | Block | Death light | Spell / release SF |
| --- | --- | --- | --- | --- | --- | --- |
| warrior | 10 / 20 | 8 / 8 | 6 | 2 | 20 | 20 / 14 |
| rogue | 8 / 20 | 8 / 8 | 7 | 4 | 20 | 16 / 12 |
| sorcerer | 8 / 20 | 8 / 8 | 8 | 6 | 20 | 12 / 8 |

Pivô nativo: `CalculateSpriteTileCenterX(width)=(width-64)/2`, deslocamento de desenho `{-centerX,0}` mais `GetOffsetForWalking`. CLX usa posição no canto inferior esquerdo relativamente à origem nativa do tile. Em atlas RGBA independente, documentar explicitamente a conversão para o footpoint `(canvasWidth/2,canvasHeight-1)`; não confundir o corte opaco com a altura lógica ou recentrar cada pose. Dimensões lógicas dependem do recurso/frame; larguras nativas estão no JSON. Fonte Sorcerer usa path **sorceror**.

Tick padrão é 20/s; perfil habitual é Speed32. Idle usa 4 ticks/frame, walk/attack/spell/hit 1, block 3, death 2 antes de segurar a pose. Essas durações são extensões nominais, não vida fixa do modo. Impacto/flecha=A−1; magia=SF sem −1. Não criar relógio, RNG, dano, som, colisão ou root motion visual independentes. Warrior com arco usa idle dungeon8; morte medium/heavy15; Death força arma unarmed sem apagar armor. Block depende de `_pBlockFlag`, magia usa **lm/fm/qm** e troca sword/mace por unarmed em Town. Run não é um enum gráfico nativo. Attack/Hit/Block em Town exigem a closure específica da Source isolada do principal.

## Lacunas do inventário r1

O r1 traz **320 clips / 7,040 frames de referência**, somente Warrior light com sword/sword_shield/unarmed/unarmed_shield, dez rótulos em oito direções. Não cobre Rogue/Sorcerer, medium/heavy, bow/axe/mace/staff. Contagens de stand20/town_stand40/walk24/hit12/block12/cast24/death24 divergem dos slots nativos; somente os ataques16 dessas quatro variantes coincidem. O rótulo cast não prova os três recursos de magia e run não cria modo nativo. Cells/pivôs do r1 permanecem metadados de referência, sem pixels lidos/copied e sem validar a matriz nativa. Não preencher clips ausentes duplicando a A-pose.

## Closure de integração do principal

1. **heroes + cloud pack** — Confirm cloud reservations/jobs and class/variant IDs; reuse Warrior r2/master/concept hashes before new own raster authoring.
2. **heroes** — Produce actual distinct native-compatible motion or explicitly missing clips. Provide named clips and all 8 direction renders, pivots/crop offsets/alpha/frames/hash/provenance. Concept sheets and repeated poses never count as motion.
3. **heroes + item attachments** — Cover light/medium/heavy and nine weapon graphics; preserve armor on forced-unarmed death, per-class frames, shield guard and three spell slots. Separate body masters, attachments and derivatives.
4. **principal** — Enforce the latest human rule: gameplay ONLY SPRITES for every hero/NPC, including Warrior/Ogden, until correct rig plus animations are validated. Preserve model masters and prior appearance reviews. Freeze exact Source, normal EXE, pack, asset manifest and isolated Speed32 profile; record policy-applied state separately from this audit. Resolve native CL2 logical frame sizes/anchors from metadata, map resampling to active native frame/fraction, and avoid simulation/RNG writes.
5. **principal** — Bind authored pack through the existing loader/container, validate RGB/alpha/TRN/cache reset and source absence, then check native state/action timing, NEW/save/load/Town-Dungeon, facing and equipment changes; no Python/converter at player install.
6. **principal + human reviewer** — Inspect native-angle and 360 art, exact temporal poses, feet/ground, view direction, close/low camera and attachments. Keep generated/validated/integrated/accepted separate; only principal changes global registry selection/install.

Hellfire permanece condicional: Monk/Bard/Barbarian não estão produzidos nesta entrega; fallback de Bard/Rogue e Barbarian/Warrior não comprova a identidade/timing de cada classe.

Verificação: 25 checks de metadados/hash, zero falhas. Não foi executado jogo, render de movimento, GPU ou aceitação artística. Nenhuma chamada a provedor, geração, retry ou upload.
