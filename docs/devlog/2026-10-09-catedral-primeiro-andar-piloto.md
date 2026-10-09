---
title: "Catedral: o primeiro andar recebe um piloto 3D"
date: 2026-10-09
description: "O piloto técnico instalado desenha nove regiões próximas no primeiro andar normal da Catedral. Geometria simples, recuperação da GPU e dados nativos preservados; arte e revisão na partida continuam pendentes."
slug: catedral-primeiro-andar-piloto
image: /assets/captures/cathedral-pilot-gpu-world.png
image_alt: "Quadro técnico offscreen do piloto da Catedral, com piso e paredes em cores simples e atores nativos em sprites; não é uma captura da sessão de gameplay instalada."
category: Geometria
order: 21
status: published
---

A reconstrução começa a avançar de Tristram para as masmorras. O **primeiro andar normal da Catedral** recebeu um piloto técnico que monta pisos e paredes a partir do mapa vivo da mesma partida. O recorte cobre **nove regiões próximas do jogador**; os demais andares e níveis de missão continuam usando a apresentação original.

A entrega do [commit c5e8cb7f](https://github.com/douglasopan/diablo-3d/commit/c5e8cb7fdcf79ceb6ed368b77bfa2ae55446a91e) foi instalada em **9 de outubro de 2026, às 15:54, horário de Brasília**, pelo mesmo **Iniciar-Tristram.cmd**. É um piloto técnico acessível pelos hooks existentes de entrada normal e de alternância com **F4**. Sua instalação não equivale a uma validação de entrada, movimento ou combate com dispositivos físicos.

## Geometria do mapa, com arte provisória

O piloto usa o mapa e a semente nativos para montar o recorte próximo. O jogo continua responsável pela simulação, colisões e comandos; o incremento é visual. Pisos e paredes têm geometria e **cores técnicas simples**, com suporte à paleta e à transparência no caminho GPU. Texturas finais, composição artística e cobertura completa ainda precisam de trabalho.

As imagens abaixo vêm do gate técnico anterior à instalação: inicialização parcial, cenário parado e atores nativos preparados em sprites. Elas ilustram o piloto e a referência de recuperação, **sem constituir uma sessão de gameplay, aprovação artística ou comparação de fidelidade na mesma câmera**. A quantidade de nove regiões vem do contrato do piloto, não de uma contagem possível nesse quadro.

<figure><a href="/assets/captures/cathedral-pilot-gpu-world.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-gpu-world.png" alt="Piloto técnico da Catedral em quadro GPU offscreen: piso quadriculado, paredes planas translúcidas, porta em cor provisória e atores nativos em sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Piloto — quadro técnico offscreen após recuperação imediata da GPU. Pisos e paredes geométricos, cores simples e atores ainda em sprites; arte final e gameplay permanecem pendentes.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-pilot-native-reference.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-native-reference.png" alt="Referência nativa do estado de teste da Catedral, com cenário original e atores preparados, usada para conferir o retorno integral ao renderer original." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Referência — apresentação nativa do estado de teste, usada no controle de recuperação. Captura offscreen sem HUD ou sessão física de gameplay; não é uma comparação artística antes/depois.</figcaption></figure>

Os dois PNGs foram publicados **sem conversão, recorte ou retoque**, mantendo seus bytes e a resolução original de **640×480**. Sete saídas do gate formam três grupos distintos; estas duas fontes evitam publicar cópias redundantes. O quadro da referência não representa cada falha intermediária do teste.

## Recuperação da GPU e preservação da partida

O gate final usou uma **Radeon RX 570 real, sem WARP**, com **157 verificações do harness, 188 verificações contextuais e 93 de alocação**. Os grupos têm escopos distintos; não são uma contagem de testes de gameplay. O gate aceitou nove valores de missão sob contexto inativo e recusou os mesmos nove sob contexto ativo, além de registrar 42 recusas de entradas inválidas pelos consumidores. Isso mantém o recorte fora dos níveis de missão que ele não suporta.

Um quadro percorreu o caminho real de desenho do mundo na GPU. Em **quatro falhas injetadas**, a fixture conferiu o retorno integral ao viewport nativo e a recuperação imediata na GPU, em três casos aquecidos e um caso frio. O quadro parcial é descartado antes de publicar imagem e seleção. Essas verificações sustentam a recuperação nos casos exercitados; não são uma avaliação prolongada de desempenho.

O witness de **290.243 bytes** ficou igual antes e depois, com mapa, SOL, RNG e lista de transparência preservados. Ogden e as seleções de Tristram continuam na instalação. Os **48 arquivos originais do perfil** conservaram conteúdo, tamanho e data; somente o recibo derivado do launcher foi renovado. Os três executáveis receberam os mesmos bytes, com backups, sem encerrar processos ou gerar novo custo.

O executável instalado tem **6.525.440 bytes**, SHA-256 `87a441706d378d3583346023e53218c936304897900b6f610ad576e9830e21e5`. O [estado versionado da integração](https://github.com/douglasopan/diablo-3d/blob/c5e8cb7fdcf79ceb6ed368b77bfa2ae55446a91e/docs/PROJECT-EXECUTION.md) distingue esta instalação dos protótipos privados anteriores.

## O próximo passo acontece na partida

Agora é preciso conferir a entrada física, o movimento, o combate e as transições, medir o orçamento de desempenho e desenvolver a arte do piloto. Os quadros offscreen não demonstram AI, HUD, save/load durante a partida, FPS sustentado ou qualidade artística final. A aprovação parcial da aparência de Ogden não aprova a arte da Catedral; o Guerreiro 3D continua em sua frente privada, sem ser anunciado por esta entrega.

O objetivo permanece **Diablo 1 inteiro em 3D**. Tristram continua em revisão, o primeiro andar normal ganhou este recorte técnico e os demais andares, Catacumbas, Cavernas e Inferno ainda exigem implementação própria. Veja o [roadmap](/roadmap/) e acompanhe a [comparação anterior da taverna](/devlog/taverna-sem-residuos/) para manter cada etapa ligada à sua evidência.
