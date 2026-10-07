---
title: "Uma mesma partida, duas maneiras de ver Tristram"
date: 2026-10-07
description: "Retrospectiva do primeiro diagnóstico local do D3D: alternar a visão, girar a câmera e preservar a partida original de Diablo."
slug: duas-visoes-mesma-partida
image: /assets/captures/v1-center.webp
image_alt: "Captura contextual do primeiro diagnóstico local da visualização de Tristram em 3D."
category: Protótipo
order: 1
---

O primeiro passo do D3D foi colocar outra maneira de desenhar Tristram dentro da mesma partida. A cidade, o personagem e as regras continuam pertencendo ao jogo. A mudança visual precisa acompanhar esse estado, permitir comparação e devolver o jogador à visão original quando ele quiser.

Este artigo é uma retrospectiva publicada em 7 de outubro de 2026. As imagens representam o primeiro conjunto local de diagnósticos, preservado em `diagnostics/gog`. O registro dessa rodada foi gravado às 05:37 do mesmo dia. Esse horário identifica a evidência local; ele não representa um lançamento separado. O código das etapas iniciais foi publicado junto do protótipo v4 no [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5), às 15:29, no horário de São Paulo.

## Uma troca de desenho que preserva o jogo

A tecla F4 alterna a renderização em Tristram sem recarregar o mapa. O primeiro roteiro de testes verificou 24 trocas de modo, conferindo que mapa, jogador e alvo da câmera continuavam iguais. Isso estabeleceu uma condição básica para o projeto: uma mudança na apresentação não deve produzir outra partida ou interferir nas regras existentes.

A separação também delimita o escopo. A visualização 3D se aplica à cidade. Ao entrar na Catedral, o jogo conserva a renderização original. Inventário, movimentação, colisão e diálogos continuam sendo calculados pelo DevilutionX. A proposta é reconstruir Tristram inteira primeiro e amadurecer esse caminho antes de estendê-lo aos níveis procedurais.

O build Windows documentado usa `NONET=ON` e funciona offline. Mais jogadores, um hub compartilhado e voz por proximidade continuam como pesquisa futura. As verificações desta etapa pertencem à partida local; elas não estabelecem uma implementação nova de rede.

O protótipo parte do DevilutionX 1.6.0-dev, no [commit dac104bab](https://github.com/diasurgical/devilutionX/commit/dac104babfb6187415432f428ac2516747ffc154). O histórico anterior a essa base pertence ao engine upstream. A contribuição D3D começa com seu renderer e suas ferramentas; preservar essa distinção também faz parte da retrospectiva.

## Desenhar e selecionar o mesmo cenário

Uma câmera que muda a projeção também muda onde o jogador vê cada objeto. Por isso, o diagnóstico inicial conferiu a seleção de chão caminhável, jogador e habitantes nas suas posições reais. Também rejeitou cliques fora da área do mundo e verificou que a renderização não sobrescrevia as linhas da interface.

Ao girar a câmera, a seleção antiga precisa ser invalidada. O teste confirmou essa atualização e o retorno entre coordenadas do chão e sua projeção na tela. A recarga dos recursos foi exercitada separadamente: liberar e reconstruir o cache deveria produzir novamente a mesma imagem no mesmo estado.

![Vista girada do primeiro diagnóstico local de Tristram.](/assets/captures/v1-rotated.webp)

*A rotação expõe o estado daquele primeiro experimento. Esta captura é evidência de desenvolvimento, não uma imagem promocional de uma cidade concluída.*

## O que esta etapa demonstra

Essa rodada mostra que a nova apresentação podia conviver com a partida nativa, responder à rotação e manter uma seleção coerente. Ela não encerra a reconstrução das casas, árvores, pedras ou personagens. Uma imagem legível de um ângulo ainda pode revelar volumes insuficientes quando a câmera muda.

Também é preciso distinguir a captura automática da experiência em uma sessão aberta. O diagnóstico roda sem janela e verifica contratos concretos do renderer. Caminhar, conversar e usar os controles no jogo continuam sendo parte da revisão. O [estado documentado do protótipo](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md) registra essas limitações.

A etapa seguinte passa a tratar construções como objetos completos e amplia os controles da câmera. A mesma regra acompanha esse avanço: melhorar a geometria preservando a simulação que já funciona.
