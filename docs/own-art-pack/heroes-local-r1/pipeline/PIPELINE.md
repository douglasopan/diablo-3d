# Pipeline local de heróis — candidato r1

Frente exclusiva de heróis do pack próprio, autorizada em 12/10/2026. O principal integra. Nenhuma alteração de Source, CMake, build, instalação, perfil ou registry global foi feita por este pipeline. Preserva NPCs, incluindo Ogden. Nenhuma nova submissão/gasto Meshy ou Tripo é necessária para executar os scripts.

**Regra humana vigente:** gameplay usa somente sprites até rig e animações corretas/completas serem validados para todos os atores, incluindo Warrior e Ogden. Masters, rigs e vistas estáticas aqui são candidatos; não promovem um ator esquelético à partida.

## Fontes e ferramentas comprovadas

Python 3.12.10 com NumPy e Pillow está disponível localmente. Godot 4.7.2 está instalado em `D:/Diablo 1 3D/.tools/godot`; este pipeline não inicia Godot nem usa GPU. Blender não foi encontrado no PATH; módulos `bpy` e `trimesh` não estão instalados no Python consultado. O raster usa CPU e o leitor GLB existente `tools/inspect_glb.py`, cujo SHA fica em cada recibo.

O inventário `production-inventory-r1.json` tem 320 recursos de Warrior e nenhum de Rogue/Sorcerer. É metadado de slots/frames, não biblioteca de arte própria ou licença para copiar pixels. Este pipeline não lê imagens DamnationX, sprites nativos, extrações ou referências raster: todas as vistas abaixo vêm exclusivamente de geometria e texturas embutidas no GLB próprio existente.

Os três arquivos de Warrior auditados são preservados pelo hash completo no recibo. Master 4K: 11.039 triângulos, 13.627 vértices, zero skins/clips. Rig bruto 4K: 52 joints, zero clips, deformação de base defeituosa já identificada. Derivado `tripo-warrior-derivative-20261009/r2/candidate`: 52 joints, zero clips; a correção de base restaura os bounds do master. A textura 2K é derivada e não substitui o master 4K para close-up. **Rig não é animação. Bind não é idle.**

## Reproduzir a inspeção e a preservação

Execute no PowerShell com o Python local:

```powershell
$heroPipeline = 'D:/Diablo 1 3D/devilutionx/docs/own-art-pack/heroes-local-r1/pipeline'
$heroSource = 'D:/Diablo 1 3D/diagnostics/characters-rigs/tripo-warrior-submission-20261009/r1/assets'
$heroRepaired = 'D:/Diablo 1 3D/diagnostics/characters-rigs/tripo-warrior-derivative-20261009/r2/candidate/warrior-light-unarmed-r3-tripo-bind-basis-repaired-2k.glb'
python "$heroPipeline/hero_pipeline.py" inspect "$heroSource/warrior-light-unarmed-r3-tripo-master-4k.glb" "$heroSource/warrior-light-unarmed-r3-tripo-rigged-4k.glb" $heroRepaired --output "$heroPipeline/warrior-local-glb-audit.json"
python "$heroPipeline/stage_warrior_masters.py" --output 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/masters'
```

Preservação recusa fonte com bytes alterados ou dependência externa, usa nomes imutáveis com prefixo SHA-256 e verifica o destino. Não sobrescreve um master existente com bytes diferentes. O master bruto permanece disponível para auditoria; usar o derivado reparado para revisar a bind pose.

## Oito vistas RGBA de bind

```powershell
python "$heroPipeline/hero_pipeline.py" render $heroRepaired --output 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/bind-directions' --bind-stills --footpoint 0,0,0 --size 512,512 --pivot 256,448 --pixels-per-unit 400 --supersample 2
```

A saída recusa diretório não vazio. Para uma nova renderização, escolha uma revisão de saída nova. `bind-directions-framing-r0` guarda a primeira tentativa com pivot Y=480: quatro direções tocaram a borda inferior e ela foi rejeitada. A correção desloca **o mesmo pivô de canvas para todas as vistas**, sem refazer/escalar/deformar o master ou recentrar frames.

Projeção ortográfica, câmera elevada 30°, frente de autoria +Z, escala uniforme 400 pixels/unidade, footpoint fixo no chão `[0,0,0]`, canvas 512². Uma unidade do GLB tem altura aproximada de 1; isso não é uma altura canônica em metros. Ordem nativa de `Source/engine/direction.hpp`: `s, sw, w, nw, n, ne, e, se`, yaws `0,-45,-90,-135,-180,-225,-270,-315`. O campo `--front-yaw` permite correção explícita de eixo para outro master. Esses eixos/projeção devem passar revisão visual e calibração do slot nativo antes de integração.

Cada PNG conserva RGBA, alpha transparente, winding CCW do GLB, depth por triângulo, UV/base-color/vertex-color reais, skin CPU e filtro AA2x. Materiais BLEND, morph targets, Draco e textura comprimida sem decoder são recusados. A luz aproximada é ambiente 0,65 + difusa direcional 0,35 em espaço linear; metallic/roughness/normal/emission e sombras de contato não são avaliados. `--unlit` oferece controle do albedo. Não atribuir diferenças PBR ao master sem comparar esse modo.

O recibo registra SHA dos arquivos e pixels, cobertura alpha, triângulos desenhados/culled, bounding box e contato com as bordas. Um PNG não vazio exige geometria real. Oito vistas da mesma bind pose continuam **oito stills e zero clips**, mesmo com oito hashes diferentes. `bind-directions-preflight-r1` conserva a primeira saída sem corte; a saída final foi refeita após a correção de wrap bilinear nos limites de textura, com o hash do script final no recibo.

## Animações reais quando disponíveis

```powershell
# Exemplo de interface; este comando NÃO funciona nos Warrior atuais sem clips.
python "$heroPipeline/hero_pipeline.py" render 'D:/fonte-propria-com-clip.glb' --clip 'nome-exato-do-clip' --state 'walk' --frames 8 --loop --footpoint 0,0,0 --output 'D:/nova-revisao'
```

Exporta somente um clip TRS real nomeado no arquivo, com pelo menos duas amostras de deformação diferentes. Avalia STEP/LINEAR/CUBICSPLINE e slerp de menor arco; conserva hierarquia, IBMs, até quatro influências normalizadas e transform de skin. Recusa uma pose/keys constantes como animação e recusa chamar bind de idle. Nenhum deslocamento 2D, repetição de imagem ou interpolação visual artificial cria um clip ausente.

Escala/câmera/footpoint/pivô permanecem fixos em todos os frames. Root motion do clip é preservado e medido pelos pixels/poses; não se recentra quadril/pés para esconder foot sliding. Não cancela deslocamento na simulação, dispara dano/projéteis ou cria relógio de gameplay. O integrador mapeia slots/ActionFrame/delays e variantes a partir dos estados ativos nativos, conforme `docs/CHARACTERS-RIGS-ANIMATION.md`; os segundos e a taxa de keys do GLB não determinam esses eventos. `--loop` apenas evita duplicar a amostra do endpoint, sem aprovar a continuidade do loop.

## Verificação e lacuna

`python -m unittest -v test_hero_pipeline` no diretório do pipeline passa cinco controles: skin responde a tradução real do joint; keys estáticas recusadas; bind não pode ser rotulada idle; quaternion de menor arco/ordem nativa; RGBA e winding/pivô da projeção CPU. Fixture autoral, sem pixels proprietários.

`python "$heroPipeline/verify_delivery.py" --render-dir 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/bind-directions'` passou 65 verificações e os cinco controles acima. Verifica novamente hashes dos bytes de fonte/script/reader/PNG/pixels, RGBA, canvas, triângulos, alpha e pivô. Tentativas reais de `--clip idle` e `--clip walk` sobre o Warrior r2 retornaram código 2 com zero frames escritos e nenhum diretório de saída criado. O relatório `pipeline-validation.json` registra versões/caminho/hash do Python, NumPy/Pillow e o Godot instalado mas não iniciado. O contact sheet8direções é artefato rotulado de revisão, não frame nativo.

A inspeção visual técnica do sheet confirmou figura/textura reais em frente, perfis e costas, sem corte de silhueta. Isso não substitui o aceite artístico humano nem qualifica anatomia, armas, foot locking ou animações inexistentes. A seleção de gameplay permanece bloqueada pela regra humana.

Os masters recebidos do Warrior continuam sem clips embutidos. A prova de caminhada descrita abaixo adiciona movimento real somente a um derivado diagnóstico rejeitado; nenhum estado de idle, town idle, walk, attack, hit, block, spell ou death está qualificado para gameplay. Armas, escudos, tiers de armadura e variantes completos também não foram entregues por essas vistas. Rogue/Sorcerer precisam de suas fontes próprias e rig/clips. Hellfire permanece condicional; não gerar Monk/Bard/Barbarian automaticamente. Master/concept/still não satisfaz o gate de animação do pack nem o gate de instalação.

## Retarget real local — prova rejeitada firstwalk1

Autorização específica do principal: reutilizar caminhada real própria do Griswold, sem novo gasto/POST. Fonte preservada: `models/meshy/characters-20261008/griswold-rig-r1/downloads/basic_animations_walking_glb_url.glb`, SHA-256 `6ea36f19d87ecb784f50a11cc6880853c7196e7aab781a64b42255e0600c1dce`. Clip `Armature|walking_man|baselayer`, 32 keys/72 channels, 0,033333335–1,066666722 segundos. Alvo é o r2 reparado, SHA `1c09cc5e55fbc1f6de5d5db94adae2cbb52a2842e5d1b300b3913f98a4d28507`.

```powershell
python "$heroPipeline/retarget_local_probe.py" --source 'D:/Diablo 1 3D/models/meshy/characters-20261008/griswold-rig-r1/downloads/basic_animations_walking_glb_url.glb' --target $heroRepaired --source-clip 'Armature|walking_man|baselayer' --output 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/candidate-firstwalk1'
python "$heroPipeline/render_retarget_diagnostic.py" --retarget-receipt 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/candidate-firstwalk1/retarget-receipt.json' --output 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/candidate-firstwalk1/diagnostic-directions' --pixels-per-unit 360
python "$heroPipeline/annotate_retarget_review.py" --render-receipt 'D:/Diablo 1 3D/assets/own-art/heroes/local-r1/warrior/candidate-firstwalk1/diagnostic-directions/render-receipt.json' --independent-review 'D:/Diablo 1 3D/devilutionx/docs/own-art-pack/heroes-local-r1/retarget-independent-review-r1.json'
python "$heroPipeline/assemble_pipeline_handoff.py"
```

Usa 22 equivalentes semânticos entre os rigs de 24/52 joints. A coluna segue a hierarquia real, não números de nomes: source `Hips→Spine02→Spine01→Spine` corresponde a target `Hips→Spine→Spine1→Spine2`. Bind world é conferida contra inversas dos IBMs. Rotação retarget = `sourceCurrentWorldR × inverse(sourceBindWorldR) × targetBindWorldR`; reconstrói rotação local sob os pais reais. A decomposição polar extrai rotação adequada sem copiar a escala 0,01 do Griswold ao alvo.

Mantém proporções, hierarquia, nodes de bind, IBMs, pesos, materiais/texturas e bytes antigos do buffer do Warrior. Preserva todas as translations/scales locais; a única exceção explícita é a componente Y do quadril, que recebe deslocamento **world** medido do source multiplicado pelo ratio de comprimento médio das pernas ≈0,6077088. XZ do quadril source é descartado na derivação in-place e permanece zero no alvo. Não soma locomoção à simulação. Os 30 joints de dedos sem fonte conservam bind; os terminais herdados não recebem comprimentos inventados.

O GLB derivado `warrior-r2-firstwalk1.glb` tem um clip real com 32 keys nos tempos de fonte deslocados em −0,033333335 s, sem adicionar sinusóides/poses arbitrárias. Oito amostras de fase source produzem oito poses de mesh distintas; reabertura float32 passou a tolerância de 1e−4. **Falhou o contato com chão:** Y mínimo nas oito fases = −0,0172257 unidades GLB do Warrior. Esse alvo tem altura normalizada≈1; a escala métrica/native ainda não foi calibrada, portanto não tratar o número como centímetro canônico. Tolerância técnica foi 0,002 unidades do alvo; nenhuma correção de câmera, pixels, piso ou root oculta a falha. Fonte Griswold já tinha pés de walk/run chegando≈0,10 m abaixo do pivô na auditoria anterior.

Status do clip e do render é `diagnostic-reject`, `gameplayEligible=false`. O principal autorizou 64 frames RGBA de diagnóstico: oito fases reais × oito direções, mesmo canvas/pivô/footpoint das bind stills. A primeira tentativa em400pixels/unidade cortou o pé da fase3/NE na borda inferior e foi rejeitada em `diagnostic-directions-framing-r0`. A projeção completa mede limite368,48pixels/unidade para margem5pixels; a saída final usa **360pixels/unidade uniformemente nas64imagens**, preservando câmera/pivô/footpoint fixos e geometria. Essa redução comum de framing não corrige, esconde ou altera penetração de chão; os bounds world continuam idênticos. Não equivalem a 64 frames nativos validados. Os recibos preservam tempos source, índices dos keys vizinhos, bounds e pior penetração por amostra. A duração source≈1,033333 s não foi disfarçada como os oito slots nativos de walk: nenhum time-warp/speed/game delay foi integrado. Revisar contato de pés, joints anatômicos, loop/phase e resampling declarado é a próxima dependência.

O contact sheet de 64 frames é prova de diagnóstico temporal e volume; não promove o clip. `candidate-firstwalk1-json-failed-r0` preserva somente uma tentativa interrompida por serialização do recibo, sem gate/entrega; não é revisão selecionável.

Revisão independente `retarget-independent-review-r1.json/.md`, produzida sem importar este método, confirmou 14 checks estruturais e movimento real. Em 288 amostras, encontrou extremo mais profundo que as oito fases de render: **Y=−0,0312170449**, target t=0,3430989762 s/fase0,33203125, vertex72, influências RightToeBase98,4585% e RightFoot1,5129%. A coluna/matrizes e o hip in-place coincidem; esse diagnóstico é falha de contato, não repetição de imagem. O recibo de render mantém `worstOf8RenderedPhases` separado de `independentFullClipQA` e registra hashes/links do parecer. A anotação preserva o recibo CPU original em `render-receipt-cpu.json`; nenhuma fonte, key, mesh ou PNG é alterado. O principal autorizou render diagnóstico após conhecer a rejeição; nenhuma revisão é declarada native-compatible por isso.

O manifesto delimitado `pipeline-delivery-manifest.json` contém hashes/status dos arquivos exatos desta entrega. Exclui as tentativas anteriores e não escolhe arquivos pelo timestamp. Contagens: três GLBs masters preservados, oito stills de bind, um clip de retarget real **rejeitado**, 64 frames de movimento diagnóstico, **zero animações nativas validadas**. Master/arte nova da frente Rogue/Sorcerer permanece com o principal e seus responsáveis.
