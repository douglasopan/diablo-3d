---
title: "Personagens com profundidade, identidade em revisão"
date: 2026-10-07
description: "Guerreiro, habitantes e vacas ganharam volume na v4. As fontes disponíveis, as inferências e a cobertura dos testes continuam explícitas."
slug: personagens-com-profundidade
image: /assets/captures/v4-actors.webp
image_alt: "Comparação do herói na v4 refinada: backend original e malha forçada nos giros de menos cinco, zero e mais cinco graus."
category: Personagens
order: 5
---

Dar profundidade a um personagem exige preservar sua identidade, seu contato com o chão e sua posição na partida. Cabeça, roupa, equipamento e silhueta precisam continuar reconhecíveis enquanto a câmera muda. A quantidade de vistas disponíveis também muda o que a reconstrução pode observar e o que precisa inferir.

Este artigo de 7 de outubro de 2026 registra a etapa de personagens da v4, publicada no [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5). A fixture principal usa o guerreiro. A presença de um caminho compartilhado no código para outras classes não equivale à revisão de todas as suas aparências e animações.

A comparação de abertura vem de `v4-refined-final-gog`, uma rodada posterior identificada como v4 refinada. Ela mostra a referência original e a malha forçada em giros próximos ao ângulo nativo. Essa origem é distinta da auditoria inicial descrita neste registro.

## Oito vistas de um mesmo corpo

O guerreiro dispõe de oito direções da animação original. A reconstrução combina essas observações para formar um corpo com profundidade e aplicar a arte da direção correspondente nas faces. As vacas também fornecem oito vistas e seguem esse caminho.

O diagnóstico usa o frame e a animação reais carregados na fixture e confere a disponibilidade das oito direções. A malha precisa ter profundidade, triângulos válidos, componentes fechados e orientação consistente. A reconstrução deve ser determinística: repetir a mesma entrada e direção precisa produzir o mesmo resultado.

As oito imagens continuam pertencendo a um personagem. Elas não representam oito modelos para colaboradores, nem comprovam que todas as combinações de armadura, arma, ação e frame foram verificadas. Essa cobertura precisa de uma matriz de aparências e animações por classe, como explica o [catálogo de personagens](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md).

## Quando só existe uma vista

Os demais habitantes da cidade têm uma única vista original. Seus corpos recebem cabeça, tronco e membros arredondados, ou roupa contínua, ajustados ao contorno daquela imagem. A profundidade e as costas são inferidas. O resultado precisa de revisão de anatomia, materiais e identidade em toda a volta.

A cena registrada usa 11 habitantes: oito moradores nomeados e três vacas, compartilhando nove famílias. O morador ferido e habitantes condicionais de Hellfire não estavam nessa fixture. A existência de enums ou caminhos de código para essas presenças não estabelece uma validação visual que não foi feita.

No fallback de uma vista, as vacas preservam o relevo de seu contorno próprio. Receber um corpo genérico de anatomia humana seria um erro de interpretação da fonte. Reconhecer o tipo real do personagem faz parte do mesmo cuidado usado para agrupar árvores e pedras.

## Pés, sombras e posição original

O corpo é encostado no chão por uma translação rígida que conserva a projeção frontal. Os testes conferem a altura mínima e a orientação das texturas depois desse ajuste. A posição visual do pé não é uma justificativa para mover o ator na simulação.

A sombra pintada no chão fica separada da geometria do corpo. A máscara retira a região preta externa inferior identificada como sombra, preservando detalhes escuros fechados e superiores. Os pixels originais permanecem intactos; a separação ocorre no tratamento da imagem em execução.

Essa sombra de compatibilidade ainda não é uma sombra dinâmica produzida por uma luz da cena. Mesmo depois da introdução das sombras da arquitetura, personagens, árvores e pedras ainda não participam daquele sistema. O [estado posterior do protótipo](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/TRISTRAM-STATUS.pt-BR.md) conserva essa limitação.

## Seleção e oclusão também contam

O guerreiro foi selecionável nos dez ângulos testados em espaço aberto. Perto da cabana, a construção continua podendo ocultá-lo. Seleção, contato com o chão e oclusão precisam acompanhar a geometria sem mudar inventário, caminhada ou diálogo.

Modelos e personagens seguem em revisão. O avanço desta etapa é tornar essa revisão possível em volume e em vários ângulos, com evidência suficiente para apontar o que falta corrigir.
