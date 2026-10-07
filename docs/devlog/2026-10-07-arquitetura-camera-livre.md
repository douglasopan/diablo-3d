---
title: "Da projeção às construções completas"
date: 2026-10-07
description: "A etapa v2 reuniu arquitetura, câmera em 360 graus e caminhada suave, mantendo o mapa e as interações originais de Tristram."
slug: arquitetura-camera-livre
image: /assets/captures/v2-center.webp
image_alt: "Captura contextual da cidade de Tristram no diagnóstico local v2 do D3D."
category: Geometria
order: 2
---

Depois de estabelecer a alternância de visão, o trabalho passou a dar estrutura às construções. Um cenário isométrico é composto para uma direção específica. Ao permitir que a câmera circule, o renderer precisa explicar o que existe atrás de um telhado, nas laterais de uma parede e nas partes que a imagem original nunca mostrou.

Esta publicação de 7 de outubro de 2026 reúne a etapa local v2. Seu diagnóstico está preservado em `diagnostics/v2-gog`, com registro às 06:19 desse dia. As versões locais não receberam commits individuais: seu resultado foi consolidado no [primeiro commit público D3D](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5). As capturas devem ser lidas como registros daquele processo, anteriores aos refinamentos posteriores da v4.

## Construções ligadas ao mapa real

A v2 já verificava a existência de malhas arquitetônicas, coordenadas finitas e triângulos com área válida. A geometria precisava permanecer na região do objeto e referenciar as peças corretas do cenário nativo. O cache da cena também era reconstruído para conferir que a mesma entrada produzisse a mesma geometria.

Esses critérios ajudam a evitar construções colocadas apenas por aparência na tela. A localização de uma casa pertence ao mapa da partida; mudar a câmera não deve mudar sua posição no mundo. O teste conferiu ainda que a montagem e as consultas da cena preservavam mapa e estado dos habitantes.

Uma atenção específica foi dada à entrada da Catedral. Os pontos de acesso continuaram disponíveis após a substituição visual. É uma restrição que permanece no projeto: paredes e telhados devem respeitar portas, passagens e interações existentes. A representação completa de um objeto precisa conservar as aberturas usadas pelo jogo.

## Uma câmera que o jogador pode controlar

A câmera ganhou uma órbita completa, ajuste de inclinação, distância e enquadramento. Os testes passaram por oito direções da volta, além dos limites de zoom e de inclinação. Uma rotação de 360 graus deveria retornar à projeção de chão inicial, sem acumular uma diferença no enquadramento.

Arrastar com o botão do meio permite girar e inclinar; a roda aproxima ou afasta; Shift com o botão do meio desloca o enquadramento. O início do movimento só é aceito dentro da área do cenário. Alternar o modo cancela um arrasto em andamento, e Home restaura a câmera. Essas regras permitem controlar o mundo enquanto menus e outras janelas conservam suas funções.

![Câmera baixa no diagnóstico local v2.](/assets/captures/v2-low.webp)

*A inclinação baixa é útil para encontrar superfícies e sobreposições que um enquadramento alto esconde. Ela mostra o estado da v2, sem incorporar correções posteriores.*

## Acompanhar uma caminhada contínua

A câmera acompanha o herói, mas a caminhada nativa tem posições de origem e destino entre células. Se o enquadramento usar apenas a posição inteira do mapa, ele pode saltar ao completar cada passo. O diagnóstico da v2 conferiu a interpolação entre essas posições sem alterar o estado da caminhada.

Na fixture testada, o deslocamento visual durante o passo foi de 17 pixels, com diferença de um pixel no instante da conclusão. Esse resultado pertence àquele enquadramento; ele não é uma medida universal de todas as animações. O importante é que a câmera avance junto do movimento, enquanto o jogo continua decidindo onde o personagem pode andar.

## A geometria ainda precisa de revisão visual

Triângulos válidos, câmera estável e passagem preservada são verificações necessárias. A fidelidade de um telhado, uma porta ou uma janela exige comparação com a composição original e inspeção dos lados reconstruídos. O [guia de contribuição](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/CONTRIBUTING.md) descreve essa revisão de objeto inteiro.

A etapa seguinte aprofundou justamente essa comparação: separou os limites físicos das paredes da área visual ocupada pela arte e tornou explícita a diferença entre o backend original e o desenho das malhas.
