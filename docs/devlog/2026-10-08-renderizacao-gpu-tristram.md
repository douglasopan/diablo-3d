---
title: "Tristram ganha renderização na GPU"
date: 2026-10-08
description: "O piloto Direct3D 11 no Windows reduz o tempo de desenho do mundo, com opção durante a partida e retorno à CPU. O resultado medido ainda não é o FPS final."
slug: renderizacao-gpu-tristram
image: /assets/captures/v4-town.webp
image_alt: "Captura histórica de Tristram na etapa v4, usada como contexto. Esta imagem não é uma comparação entre CPU e GPU."
category: Ferramentas
order: 11
status: published
---

O protótipo de Tristram passou a ter um renderizador opcional na placa de vídeo. O piloto usa Direct3D 11 no Windows, dentro do DevilutionX, e foi publicado no [commit c8329403e](https://github.com/douglasopan/diablo-3d/commit/c8329403e6b69ff3c95f97adeff38cb4a8c1e8a2). O objetivo imediato é reduzir o custo de desenhar a mesma cena, preservando a partida e a cabana com sua iluminação atual.

A captura acima pertence à etapa v4 já publicada. As evidências desta entrega são os resultados descritos na [documentação versionada do renderizador GPU](https://github.com/douglasopan/diablo-3d/blob/c8329403e6b69ff3c95f97adeff38cb4a8c1e8a2/docs/GPU-RENDERER.md); nenhuma nova captura local foi acrescentada ao site.

## A mesma partida, outro caminho para desenhar

A GPU recebe a geometria, os materiais, as luzes, as sombras e os identificadores preparados pelo protótipo. Produz cor, profundidade e seleção, que voltam aos buffers usados pela interface existente. A simulação, o mapa, as colisões e os saves continuam no DevilutionX. A cabana e sua luz foram preservadas; essa mudança não regenera modelos nem representa aprovação artística final.

O caminho de CPU continua disponível. Se o backend GPU falhar ou ultrapassar seu orçamento, o quadro inteiro é refeito na CPU. Isso evita misturar uma imagem nova com profundidade ou seleção de outro quadro. Home continua usando o renderizador original, e os níveis procedurais ainda não receberam a reconstrução 3D.

## Opções durante a partida

No Windows, abra **Esc → Options → Video Options → 3D GPU Rendering**. **3D Edge Smoothing** controla a suavização separadamente. As escolhas persistem no perfil e valem no próximo desenho; desligar e ligar GPU permite tentar novamente após uma falha.

Fora da partida, as opções ficam em **Settings → Graphics**. O padrão público da GPU é desligado. A preparação do perfil local do responsável ativou somente GPU e preservou as outras preferências. O indicador do protótipo distingue GPU ativa, CPU selecionada e retorno automático à CPU.

## Menos tempo no desenho do mundo

A rodada controlada usou Windows, Radeon RX 570 e Intel Core i7-14700, com a mesma cabana, iluminação e fogo congelado. Cada número é a mediana de cinco chamadas após aquecimento por backend.

| Condição | CPU | GPU |
| --- | ---: | ---: |
| 640×352, 1×, geometria forçada na câmera original | 24,8 ms | 8,1 ms |
| 960×540, 2× por eixo | 191,8 ms | 40,9 ms |
| 1920×1080, 1×, zoom nativo ligado | 154,0 ms | 29,6 ms |

Em Full HD, o desenho do mundo levou cerca de **5,2 vezes menos tempo**. A medição inclui a leitura de volta da GPU e a redução quando aplicável, mas exclui a interface e a apresentação SDL. **Isso não é uma medição do FPS final da partida.** Os tempos variam entre rodadas e não garantem o mesmo ganho em outro computador.

## Seleção e recuperação também fazem parte do teste

Passaram seis cenários reais com diferentes câmeras e densidades, testes sintéticos da GPU, retorno CPU → GPU → CPU, recuperação após falha e 25 testes do iniciador. Desligar GPU restaurou exatamente a referência CPU; Home manteve o caminho original.

A comparação encontrou 108 diferenças de seleção em bases de terreno coplanares e uma diferença de subamostra na borda de uma árvore. Todas receberam explicação geométrica independente. Não restaram divergências sem explicação, mas isso não significa que os dois rasterizadores produzam pixels idênticos em todas as bordas.

## O que ainda limita este piloto

A ponte ainda lê os resultados da GPU de forma síncrona, e o mapa estático de sombras continua sendo construído na CPU. Outras plataformas continuam usando CPU. O orçamento atual também pode fazer uma área grande retornar à CPU; este incremento não promete suporte GPU a 4K.

Menus HD, apresentação direta na GPU e renderização 3D dos níveis procedurais permanecem pendentes. A próxima ação é observar o desempenho na partida habitual e retomar a revisão completa da cabana, sem gerar outro modelo automaticamente. O [guia de execução desta revisão](https://github.com/douglasopan/diablo-3d/blob/c8329403e6b69ff3c95f97adeff38cb4a8c1e8a2/docs/PROJECT-EXECUTION.md) registra essa sequência. Tristram continua sendo o primeiro marco do objetivo de reconstruir todo Diablo 1 em 3D.
