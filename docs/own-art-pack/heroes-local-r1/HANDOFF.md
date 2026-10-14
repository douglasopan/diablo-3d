# Heróis próprios — entrega local R1

12/10/2026, America/Sao_Paulo. Frente local autorizada pelo principal, somente HERÓIS. Entrega de produção/revisão, não um pack pronto para gameplay ou distribuição. Entrada de distribuição: Panvia; launcher próprio cancelado. Source, build, instalação, registry global e NPCs continuam com seus responsáveis.

O Warrior foi retomado pelo hash da revisão existente: master 4K, rig 4K histórico e derivado 2K com base de bind corrigida. O master 4K e o conceito r3 estão preservados. Não houve novo gasto Meshy/Tripo. Foram gerados por imagegen os masters de identidade de Rogue/Sorcerer e pranchas de oito direções; Sorcerer r1 tinha duas orientações erradas, registradas e corrigidas em r2. A revisão rejeitada permanece identificável.

São **24 stills RGBA**: oito bind views do Warrior renderizadas da geometria/textura real, oito vistas de Rogue e oito de Sorcerer. Warrior: canvas 512², pivô raster [256,448], câmera ortográfica 30°, footpoint de autoria [0,0,0]. Rogue/Sorcerer: 512×640, pivô raster [256,608], escala uniforme por classe. As vistas raster têm câmera baixa/yaw aproximado, sobretudo NE do Sorcerer; ainda exigem câmera e pivô nativo calibrados. Imagens estáticas, inclusive A-pose, não são clips.

O QA verificou alpha sobre cinza claro/checker, reabriu os PNGs, conferiu hashes e contornos. O RGB marrom que aparece no viewer das imagens geradas está oculto em pixels transparentes; não é fundo visível no composite. Masters têm alpha 0..254 e fringe medido; foram preservados. As derivações de resize têm seus próprios bytes/hashes. Nenhum pixel DamnationX ou extração nativa foi usado na geração/render.

## Animação: prova real, rejeitada

A fonte própria de caminhada de Griswold já existente foi investigada para retarget local, sem nova chamada paga. O candidato `warrior/candidate-firstwalk1` contém movimento real derivado dessa fonte: não é uma repetição da bind pose nem deslocamento de um PNG. Há correspondência explícita de 22 ossos, coluna mapeada semanticamente, transformações de bind medidas e proporções conservadas. Dedos extra do Warrior permanecem em bind. Os 64 renders de diagnóstico têm oito fases em oito direções, sem equivalência temporal nativa aprovada. A escala uniforme final de 360 pixels/unidade evita o corte de uma direção observado na tentativa de 400; pivô, footpoint e geometria permanecem fixos. Os renders preliminares ficam preservados localmente, separados da revisão final no ZIP.

A prova estrutural não o torna correto: **o pé atravessa o chão em 0,031217 unidade da fonte** na revisão independente com 288 amostras, cerca de 3,1% da altura do mesh, excedendo a tolerância 0,002 do preflight. O teste inicial de oito fases encontrava somente 0,01723; não era o pior ponto do clip. A escala da instância nativa ainda não foi calibrada, portanto esses valores não são medidas em centímetros do jogo. O candidato e seus renders de diagnóstico permanecem rejeitados, com fonte, método, tempos, hashes, erros e limitações nos recibos. Não foi adicionado deslocamento oculto para mascarar o defeito. A fonte de Griswold tem foot sliding/contato e ciclo próprios; o resampling não comprova o passo do Warrior nem equivalência da duração ao dispatcher nativo.

**Gameplay permanece SOMENTE SPRITES para todos os heróis e NPCs, incluindo Warrior/Ogden, até rig e animações corretas validados.** Esta entrega não aplicou a política no runtime nem selecionou qualquer modelo. Há zero clips nativos validados, zero heróis integrados desta produção e zero heróis completos para distribuição.

## Contrato e próximos passos do mesmo incremento

O `contract-audit.json` deriva direções, recursos e tempos das tabelas/código nativos. Direções: S, SW, W, NW, N, NE, E, SE. O inventário r1 tem só Warrior light, 320 clips/7.040 frames de referência; suas contagens não devem virar delays nativos. Onze slots de recurso por aparência, tiers de armadura, nove classes gráficas de arma, guards de escudo/magia e exceções estão na auditoria. `sorceror` é o path nativo do Sorcerer.

O pivô CLX `(width−64)/2` e a origem inferior do tile são diferentes do footpoint raster dos candidatos. Impacto de ataque é A−1; emissão de magia é SF; nenhuma animação deve emitir dano, áudio ou RNG. Speed32 habitual é distinto do padrão20. A simulação e o frame/fração ativos continuam autoritativos.

Corrigir o contato/retarget no master derivado e validar loops/pés/mãos em oito direções, com revisão artística; depois authoring dos estados ainda ausentes (idle dungeon/town, walk town/dungeon, ataques, hit, block, três magias, death) e equipamentos/todos tiers. Rogue/Sorcerer ainda precisam de geometria/rig/clips ou uma produção raster temporal validada. Hellfire segue condicional. Não preencher cobertura com repetição de stills.

O principal recebe os candidatos para calibração dos slots/pivôs, container/loader e gates nativos de NEW/save/load/Town↔Dungeon/combate/equipamento; somente ele promove registry, compila e instala. Este handoff não cria outro plano do jogo.

## Arquivos e reprodução

`manifest.json` lista todos os bytes, origem, dimensões, pivôs, status e hashes; `SHA256SUMS.txt` confere os arquivos do ZIP. O recibo local `diagnostics/own-art-heroes-local-20261012-r1/delivery-receipt.json` contém SHA do ZIP e a verificação de CRC/bytes. Os prompts finais e o edit direcionado do Sorcerer estão neste diretório. Modo usado: ferramenta integrada imagegen, cinco chamadas; não CLI/API fallback.

O ZIP contém `art/` com fontes/candidatos, `metadata/` com estes diagnósticos/scripts e `tools/inspect_glb.py` com sua licença herdada. Os scripts exigem Python/NumPy/Pillow apenas para autoria/QA local; isso não vira dependência do jogador. Num pacote extraído, use `hero_pipeline.py --repo-root <raiz extraída> ...` para o reader em `tools/`. Auditoria de Source/tabelas e reprodução do retarget precisam do checkout/fontes locais identificados nos recibos. Não executar stage com caminhos de origem ausentes nem tratar testes técnicos como aceite artístico.
