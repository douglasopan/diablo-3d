---
title: "Meshy e sombras: uma geração começa a revisão"
date: 2026-10-07
description: "A cabana leste recebeu geração multi-view e importação opcional; construções passaram a projetar sombras, com falhas e limites documentados."
slug: meshy-sombras-revisao
image: /assets/captures/meshy-review.webp
image_alt: "Cabana Meshy em Tristram na rodada intermediária v4-lighting-meshy-gog, anterior à calibração final da luz exterior."
category: Ferramentas
order: 7
---

A cabana leste é a primeira fixture do pipeline Meshy. Ela oferece uma composição identificável, com telhado, porta lateral e janela redonda, que pode ser comparada na perspectiva nativa. O propósito do experimento é obter um candidato inspecionável e verificar o que a geração preserva ou altera.

Esta etapa foi publicada em 7 de outubro de 2026, às 18:08 no horário de São Paulo, no [commit 2218b439b](https://github.com/douglasopan/diablo-3d/commit/2218b439bea8ab8b38c186283928ac6409c96163). O candidato não foi adotado como substituição padrão da cabana.

A imagem de abertura vem de `v4-lighting-meshy-gog`, uma rodada intermediária do trabalho posterior de iluminação. Ela registra o modelo no jogo antes da calibração exterior final de [f673fc709](https://github.com/douglasopan/diablo-3d/commit/f673fc7090f21afc2f09a4fff3bd44a028943315), e não a captura exata do primeiro commit de importação.

## Da primeira imagem às múltiplas vistas

O primeiro teste usou a composição original da cabana, sem árvores nem personagens. Consumiu 30 créditos e produziu 2.760 triângulos, além de uma malha mestre preservada. Recuperou a forma geral, mas alterou janela, acabamento e telhado. Esse candidato permaneceu fora do jogo, disponível para inspeção local.

O segundo experimento começou pela geração de vistas complementares. Essa etapa consumiu nove créditos. A requisição 3D seguinte colocou a imagem original primeiro, seguida das vistas geradas da traseira e do frontão, e consumiu 35 créditos. O resultado tem 5.783 triângulos, texturas 4K, mapas PBR e uma malha mestre.

As imagens das costas são interpretações geradas: Diablo não fornece uma vista original dessa parte da construção. Elas devem aparecer como referências de geração, nunca como capturas do jogo. O [workflow documentado](https://github.com/douglasopan/diablo-3d/blob/2218b439bea8ab8b38c186283928ac6409c96163/docs/MESHY-WORKFLOW.md) registra esse papel e os custos observados, num total de 44 créditos para o segundo experimento.

## Carregar o candidato sem substituir a revisão

O modelo ganhou importação real opcional no perfil separado de comparação. O iniciador normal conserva a cabana reconstruída localmente. Girar ligeiramente a câmera sai da rota nativa de Home e permite observar o modelo importado.

A inspeção e conversão preservam geometria, materiais e UVs, com orientação e escala registradas. O carregador verifica tamanhos, valores finitos e coordenadas de textura; um arquivo ausente ou inválido conserva a alternativa procedural. Um carregamento válido ainda exige conferir localização, silhueta, portas, oclusão e topologia.

O candidato duplica uma janela escura nos dois frontões, altera cumeeira e vidro e inclui placas de chão. A inspeção também encontrou arestas de borda e não manifold. Esses problemas impedem tratar a geração como um objeto final aprovado. Os mapas de normal, rugosidade e metalicidade estão disponíveis localmente, mas ainda não são aplicados pelo shader de software.

## Sombras produzidas pela arquitetura

O mesmo commit introduziu um mapa de profundidade direcional de 512 por 512. Telhados, paredes e construções importadas bloqueiam a luz. Chão e superfícies desenhadas recebem a sombra, com filtragem de borda; o mapa fica em cache durante o giro da câmera.

Quatro peças de sombra pintada das cabanas receberam máscaras delimitadas. A substituição usa grama de peças doadoras apenas nos pixels selecionados e cobertos pelas duas peças. Pixels fora da máscara e a opacidade original permanecem preservados. Mapa e colisão continuam nativos.

Esse sistema cobre construções. Árvores, pedras e personagens ainda não lançam sombras por ele, e os atores mantêm as sombras de compatibilidade. Remover as demais sombras pintadas, incluir novos emissores e acompanhar objetos móveis são trabalhos adicionais, descritos no [estado dessa etapa](https://github.com/douglasopan/diablo-3d/blob/2218b439bea8ab8b38c186283928ac6409c96163/docs/TRISTRAM-STATUS.pt-BR.md).

A próxima revisão calibra a luz exterior com modelo, câmera e textura fixos. Essa separação permite identificar o que é erro de iluminação e o que continua sendo diferença do recurso gerado.
