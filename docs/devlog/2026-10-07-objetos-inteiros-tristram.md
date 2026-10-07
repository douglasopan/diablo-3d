---
title: "Reconstruir objetos inteiros em Tristram"
date: 2026-10-07
description: "A v4 agrupou construções, árvores e pedras a partir do cenário nativo e ampliou a auditoria dos volumes, com limites visuais explícitos."
slug: objetos-inteiros-tristram
image: /assets/captures/v4-town.webp
image_alt: "Centro de Tristram na rodada v4-refined-final-gog: arquitetura, árvore, poço e herói em uma captura da v4 refinada."
category: Geometria
order: 4
---

Tristram é desenhada com fragmentos que, juntos, formam casas, árvores e pedras. Transformar cada fragmento em uma coluna independente mantém alguma cobertura do mapa, mas pode desmontar o objeto quando a câmera gira. A v4 passou a reunir esses componentes em volumes completos, usando padrões verificados da cena nativa.

Esta retrospectiva foi publicada em 7 de outubro de 2026. Os diagnósticos locais `v4-final-gog` e `v4-final-shareware` registram a etapa anterior à animação nova e às revisões de Meshy e iluminação. O resultado foi incluído no [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5).

A imagem de abertura e o giro deste artigo vêm da rodada posterior `v4-refined-final-gog`, identificada como v4 refinada. Elas ilustram o estado usado nas revisões locais seguintes. Os resultados da auditoria inicial abaixo continuam vinculados às rodadas `v4-final`, sem atribuí-los automaticamente a cada captura posterior.

## Arquitetura com frente, costas e base

Casas, taverna, oficina de Griswold, cabana de Adria, poço, exterior da Catedral e entrada das Catacumbas têm malhas contínuas. Superfícies externas, internas e inferiores fecham seus volumes. A arte frontal é aplicada nas faces correspondentes, respeitando profundidade e oclusão; as superfícies reveladas pela rotação recebem materiais próprios.

Isso conserva uma referência visual reconhecível sem repetir porta ou janela por todas as paredes. Ainda há formas e materiais a refinar. O jogo original oferece uma vista pintada, e a reconstrução das costas precisa assumir uma interpretação coerente dessa composição.

O [catálogo publicado em seguida](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md) organiza o snapshot em 14 grupos arquitetônicos, reunidos em 12 objetos completos e nove famílias reutilizáveis. Esses números descrevem a organização do renderer; não significam que todos os modelos tenham aprovação artística.

## Árvores e pedras pertencem a famílias

A grade nativa completa de 112 por 112 células revelou 93 árvores em seis famílias. O agrupamento combina os fragmentos do cenário com imagens especiais e inclui árvores pequenas desenhadas apenas por peças MIN. Troncos, galhos e folhagem recebem espessura e volumes fechados.

As pedras são identificadas por seis padrões exatos de peças. A rodada completa reúne 501 grupos e 1.607 células de origem. Desses grupos, 19 são preenchimentos ocultos dentro da arquitetura. Eles continuam disponíveis para auditoria, mas não precisam reaparecer atravessando uma parede fechada. As rochas externas são mantidas.

Uma pedra de quatro fragmentos passa a ser tratada como um objeto, em vez de quatro caixas. Contagens antigas de 419 pedras pertenciam a uma região menor; a diferença de total corresponde à ampliação do snapshot. Instâncias no mapa também não são pedidos individuais de modelagem: uma família reutilizável pode atender várias delas.

![Centro de Tristram girado 90 graus na rodada v4-refined-final-gog.](/assets/captures/v4-rotated.webp)

*Giro de 90 graus da v4 refinada. A vista mostra a relação entre volumes que a perspectiva nativa sobrepõe. As faces ocultas continuam sujeitas à revisão.*

## Auditar fechamento e preservar a partida

Os testes conferiram triângulos finitos, profundidade, orientação das arestas, fechamento, reconstrução determinística e contato com o chão. A rodada final anterior auditou 93 árvores e 501 grupos de pedras, produziu 40 vistas de giro e verificou 12.794 raios independentes da cabana e do poço.

Esses raios não encontraram faces ausentes nas fixtures testadas. Isso é evidência mecânica de cobertura, com escopo definido. A silhueta de uma árvore, a proporção de uma casa ou a profundidade de uma pedra ainda podem estar erradas mesmo quando suas superfícies estão fechadas.

Objetos sólidos ainda sem agrupamento identificado usam relevo fechado por fragmento. Ruínas, cercas e decorações compostas precisam de identificação e revisão como conjuntos. O [estado registrado da v4](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md) mantém essa lacuna explícita. Completar esses objetos preservando colisão, caminhos e gatilhos é parte da prioridade de terminar Tristram antes de avançar para o primeiro nível procedural da Catedral.
