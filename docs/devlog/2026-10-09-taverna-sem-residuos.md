---
title: "Taverna: removendo os resíduos do cenário antigo"
date: 2026-10-09
description: "A correção instalada retira uma fachada antiga e uma pedra que se sobrepunham à taverna importada, preservando o modelo e a navegação. Veja a comparação técnica CPU antes/depois."
slug: taverna-sem-residuos
image: /assets/captures/tavern-cleanup-after.webp
image_alt: "Prévia técnica CPU offscreen da taverna após a correção dos resíduos nativos, na órbita zero; não é uma captura da janela do jogo nem prova de aprovação artística."
category: Geometria
order: 20
status: published
---

Uma parede isolada e uma pedra diante da taverna chamaram atenção durante a revisão de Tristram. A investigação localizou elementos do cenário nativo antigo que continuavam sendo desenhados junto à construção importada. A correção retira esses resíduos quando o modelo correspondente está carregado, **preservando a arte da taverna**.

O ajuste do [commit feb9ca77e](https://github.com/douglasopan/diablo-3d/commit/feb9ca77e73e66c2cc209af51ca9bb9b3504e6c1) já está instalado no mesmo **Iniciar-Tristram.cmd**, desde **9 de outubro de 2026, às 00:44, horário de Brasília**. É uma correção delimitada de composição do cenário; a cidade continua em revisão.

## Por que os fragmentos apareciam

A taverna importada já fornecia a alvenaria da construção, mas a substituição visual não cobria toda a pintura da fachada antiga. Uma pedra de preenchimento nativa também permanecia diante dela, fora do limite conservador usado para ocultar elementos substituídos pela arquitetura.

O ajuste deixa de desenhar **20 células da fachada antiga e um grupo específico de pedra, formado por quatro células**. A identificação da pedra exige a combinação exata de posição, peças nativas e vínculo com a taverna importada. Isso evita estender a remoção às pedras vizinhas. Sem esse modelo carregado, o caminho nativo de recuperação é preservado.

O grupo de pedra tem **1.760 triângulos**, que passam a ficar ocultos nesse caso, sem alterar sua geometria. Essa contagem descreve a limpeza visual; não é uma medição de ganho de desempenho. O modelo, seu master, o ajuste de escala e posicionamento, a luz e as texturas permanecem iguais. A colisão nativa e os atores também foram preservados.

## Antes e depois na mesma câmera

As duas imagens abaixo vêm da reprodução própria do responsável pela integração, com **a mesma câmera e a mesma cena de entrada**. São prévias **CPU offscreen**, produzidas sem capturar uma janela de gameplay ou executar a comparação na GPU. A imagem de abertura corresponde ao resultado depois da correção.

<figure><a href="/assets/captures/tavern-cleanup-before.webp" data-lightbox="true"><img src="/assets/captures/tavern-cleanup-before.webp" alt="Antes: prévia CPU offscreen da taverna na órbita zero, com resíduos da fachada nativa e a pedra de preenchimento ainda desenhados junto ao modelo importado." width="960" height="720" loading="lazy" decoding="async"></a><figcaption>Antes — reprodução CPU offscreen, órbita zero. A fachada antiga e a pedra de preenchimento ainda se sobrepõem à composição da taverna.</figcaption></figure>

<figure><a href="/assets/captures/tavern-cleanup-after.webp" data-lightbox="true"><img src="/assets/captures/tavern-cleanup-after.webp" alt="Depois: a mesma câmera CPU offscreen da taverna, com os resíduos nativos suprimidos e o modelo, luz, texturas e atores preservados." width="960" height="720" loading="lazy" decoding="async"></a><figcaption>Depois — a mesma câmera CPU offscreen, com os resíduos identificados suprimidos. Não é uma captura da janela do jogo nem uma nova aprovação artística.</figcaption></figure>

As imagens foram publicadas inteiras, em WebP lossless, mantendo **960×720 e todos os pixels** das duas fontes autorizadas. Nenhuma delas foi recortada, retocada ou gerada novamente.

## O que a comparação confirmou

A reprodução antes/depois passou **113 verificações por versão**, com **54 comparações e oito órbitas**. Os arquivos nativos de mapa e de arquitetura comparados permaneceram byte a byte idênticos. Isso sustenta a retirada dos resíduos no conjunto exercitado, preservando a arquitetura importada e o comportamento de recuperação previsto.

A compilação Release x64 passou antes da instalação. Os três aliases receberam o mesmo executável, com backup, e **nenhum processo foi encerrado**. Os **44 arquivos do perfil** conservaram bytes, tamanhos e datas durante a instalação; a preparação posterior do iniciador alterou somente seu recibo esperado de runtime.

A validação é finita: uma falha ampla histórica relacionada à área ocupada pela Catedral continua separada e **não foi resolvida por este ajuste da taverna**. O resultado não significa que toda a suíte do cenário tenha passado. Não houve nova execução na GPU, medição de FPS, teste de entrada física ou aceitação artística integral.

## A revisão de Tristram continua

A próxima conferência é observar a taverna na partida habitual e continuar a revisão de orientação, proporções, materiais e encaixes dos objetos. A correção não regenera o modelo nem encerra a montagem da cidade, o HUD ou as ferramentas do editor.

O objetivo permanece **todo Diablo 1 em 3D**. As frentes de Map Builder e do primeiro nível procedural têm protótipos privados em revisão; o addon correspondente continua desativado por verificações de identidade pendentes, e não há um novo nível procedural 3D instalado ou jogável. Consulte o [roadmap](/roadmap/) para distinguir a entrega atual dos próximos marcos.
