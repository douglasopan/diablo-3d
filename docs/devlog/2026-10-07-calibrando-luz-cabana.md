---
title: "Calibrando a luz da cabana na perspectiva original"
date: 2026-10-07
description: "Comparações reais de original, antes e depois orientaram a iluminação exterior da cabana Meshy antes da conversão à paleta do jogo."
slug: calibrando-luz-cabana
image: /assets/captures/lighting-comparison.webp
image_alt: "Comparação de capturas reais da cabana: backend original, modelo Meshy antes da calibração e modelo com a luz exterior calibrada."
category: Luz
order: 8
---

A cabana importada precisava receber luz de maneira previsível. Algumas paredes estavam claras demais, e grandes manchas triangulares apareciam no telhado. Antes de continuar o interior, a comparação fixou câmera, posição do ator, geometria, UVs e textura. A alteração isolada foi o tratamento da iluminação.

A calibração exterior foi publicada em 7 de outubro de 2026, às 18:57 no horário de São Paulo, no [commit f673fc709](https://github.com/douglasopan/diablo-3d/commit/f673fc7090f21afc2f09a4fff3bd44a028943315). As imagens deste artigo vêm das rodadas reais de diagnóstico. Referências artificiais da interface do site e vistas geradas para o Meshy têm outro papel e não são apresentadas como resultados do jogo.

## Aplicar a luz antes da paleta

A textura de cor base importada é decodificada de sRGB, recebe iluminação em RGB linear e volta para sRGB antes da escolha da cor mais próxima na paleta de Diablo. Isso conserva o uso da paleta original enquanto separa a cor do material da intensidade de luz que ele recebe.

O perfil compartilhado define ambiente, luz direcional e direção do sol. A luz direta e o mapa de sombras usam a mesma direção no mundo. O ambiente continua contribuindo nas regiões bloqueadas pela construção. Girar a câmera não muda a posição da fonte.

Os materiais importados são desenhados dos dois lados. A normal de uma face visível pelo verso passou a receber a orientação apropriada para essa iluminação. A correção retirou as grandes manchas triangulares sem modificar os vértices, as UVs ou a textura do modelo.

A arte nativa já contém iluminação pintada e conserva o tratamento de compatibilidade. Portanto, esta calibração governa os recursos importados de cor base. Ela não remove a iluminação embutida de todos os materiais do cenário, como explica a [referência de luz](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/TRISTRAM-LIGHTING.md).

## Comparar telhado e paredes separadamente

A comparação usa os mesmos recortes de material no backend original, no candidato anterior e na versão calibrada. As médias RGB codificadas ajudam a interpretar a mudança, mas não são uma nota de fidelidade da geometria.

| Região | Original | Antes | Calibrado |
| --- | --- | --- | --- |
| Telhado | 37,30 / 29,86 / 28,84 | 35,23 / 28,64 / 27,98 | 37,76 / 29,98 / 30,47 |
| Parede inferior do frontão | 7,04 / 6,26 / 8,29 | 38,51 / 34,48 / 40,65 | 7,22 / 5,90 / 9,31 |
| Parede inferior da porta | 15,88 / 14,20 / 18,18 | 25,57 / 22,81 / 26,71 | 13,52 / 11,31 / 15,24 |

O frontão se aproximou do tom escuro original, a lateral perdeu o excesso de claridade e o telhado conservou seu brilho. A textura Meshy ainda tem menos contraste, e a parede sob o beiral da porta permanece mais escura. Essas diferenças continuam sendo questões de material e geometria a revisar.

![Capturas de giros próximos ao ângulo original na revisão da iluminação.](/assets/captures/lighting-orbits.webp)

*Os giros de menos cinco e mais cinco graus ajudam a revelar problemas perto da perspectiva de comparação.*

![Capturas da cabana por outros ângulos durante a revisão da iluminação.](/assets/captures/lighting-angles.webp)

*As costas e laterais foram inspecionadas em rotação. Sem uma vista nativa dessas superfícies, a revisão não pode afirmar uma correspondência de cor com um original inexistente.*

## Evidência e próximo trabalho

Os testes de iluminação e de cena passaram com dados completos do GOG, shareware e perfil Meshy separado. A rodada final usa o perfil distribuído, sem substituição local das configurações. Essa validação não encerra a revisão artística nem os defeitos topológicos do candidato.

Home continua usando o backend original; a calibração usa malhas forçadas no mesmo enquadramento para medir a mudança real. A janela amarela, a luz interna e o piso de madeira são a etapa seguinte e **não estão ativos neste commit**. Avanços posteriores precisam de evidência própria antes de aparecer como concluídos.

O [roadmap](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/ROADMAP.md) mantém dia/noite, horizonte e neblina como planos posteriores ao fechamento de Tristram. A escolha entre expansão caminhável e horizonte decorativo continua aberta. Esta etapa entrega uma referência exterior comum para continuar a revisão de modelos e materiais.
