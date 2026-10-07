---
title: "Uma janela de luz e um chão sem a sombra antiga"
date: 2026-10-07
description: "O perfil opcional da cabana Meshy recebeu interior físico e luz amarela; oito peças de terreno foram revisadas para retirar sombras pintadas."
slug: interior-cabana-sombras-terreno
image: /assets/captures/cabin-interior-final.webp
image_alt: "Comparação da cabana: referência original, modelo antes e modelo depois do interior iluminado e da limpeza das sombras pintadas no terreno."
category: Luz
order: 9
status: published
---

A janela da cabana leste agora permite ver luz amarela vindo de um cômodo físico. O terreno próximo também recebeu uma revisão para retirar fragmentos da sombra antiga pintada, deixando a sombra calculada pela geometria ocupar esse papel.

O trabalho foi publicado em 7 de outubro de 2026, às 19:33 no horário de São Paulo, no [commit 63e5e749](https://github.com/douglasopan/diablo-3d/commit/63e5e749d73c213aecf4b77b400aa04acc5a0598). O interior pertence ao perfil opcional de revisão Meshy. As capturas finais comparam esse estado à calibração exterior anterior; a primeira tentativa de iluminação tem resultados próprios, separados abaixo.

## Um cômodo por trás da abertura

O interior tem paredes de pedra, teto, piso de 12 tábuas sobre uma base fechada e uma pequena lâmpada volumétrica. Um túnel de pedra e divisórias de madeira acompanham a profundidade da janela. A parede interna traseira fecha a abertura duplicada que o modelo gerado apresentava.

A abertura foi medida no modelo importado e recebe um polígono de 20 lados que cabe dentro dela. A iluminação usa esse mesmo limite, bloqueando raios nos cantos de pedra. Isso corrige um vazamento que uma abertura retangular permitia. A lâmpada emite luz própria e ilumina piso e paredes com uma fonte pontual quente, em RGB linear.

O arquivo Meshy, os triângulos exteriores, as UVs e a textura de origem permanecem intactos. Nenhuma nova geração paga foi necessária. A porta continua fechada, e a colisão nativa impede a entrada do jogador. A [referência de iluminação](https://github.com/douglasopan/diablo-3d/blob/63e5e749d73c213aecf4b77b400aa04acc5a0598/docs/TRISTRAM-LIGHTING.md) documenta esse escopo.

## O primeiro teste e a rodada final

A comparação liga e desliga a fonte pontual e sua emissão no renderer real, mantendo câmera e cena iguais. Os números pertencem a essa fixture:

| Rodada | Intensidade | Pixels alterados | Pixels aquecidos | Mudanças fora da cabana |
| --- | ---: | ---: | ---: | ---: |
| Primeiro teste local | 2,5 | 117 | 66 | 0 |
| Diagnóstico final | 6,0 | 116 | 116 | 0 |

O ajuste de intensidade veio acompanhado da correção dos limites da abertura. A rodada final verifica também a parede traseira opaca e o raio que antes atravessava o canto de pedra.

![Recorte da janela na comparação real entre lâmpada desligada e ligada.](/assets/captures/cabin-lamp-final.webp)

*O teste mostra a mudança local da iluminação. O detalhe das tábuas não é legível através da pequena janela na escala normal; esta imagem não oferece uma vista completa do cômodo.*

## Limpar a sombra certa

A limpeza passou de quatro para oito peças auditadas do chão das cabanas. Máscaras fixas delimitam os pixels que recebem terreno limpo de peças doadoras. A leitura independente verificou as oito peças e 80 controles em GOG, shareware e no perfil Meshy, sem divergências fora das máscaras.

Pixels transparentes, preto opaco, cobertura original, mapa e colisão são preservados. A transição escura entre terra e grama junto ao poço foi inspecionada e mantida: apagar qualquer região escura também apagaria detalhes legítimos do terreno.

![Terreno exterior: grama com sombra pintada antes e terreno limpo com sombra geométrica depois.](/assets/captures/cabin-floor-final.webp)

*Antes, fragmentos da sombra pintada permaneciam junto às paredes. Depois, o terreno recebe grama limpa e conserva a sombra calculada pela geometria. Este é o chão externo da cabana; o piso interno de madeira é outro componente.*

## Conferir o giro e conservar a referência

As rodadas finais passaram nos testes de iluminação e cena com dados GOG, shareware e perfil Meshy separado. Cada uma produziu 50 vistas e conferiu 24 enquadramentos de Home iguais ao backend original. Home valida esse retorno, enquanto as imagens abaixo usam a geometria ativa.

![Comparação antes e depois nos giros de menos cinco, zero e mais cinco graus.](/assets/captures/cabin-orbits-final.webp)

![Comparação da cabana antes e depois em quatro ângulos principais.](/assets/captures/cabin-angles-final.webp)

A janela importada ainda difere da referência em posição e material. Topologia, texturas e outros interiores precisam de revisão; os testes não aprovam a fidelidade completa do modelo. Árvores, pedras e personagens ainda não lançam sombras pelo mapa da arquitetura. Dia/noite, horizonte e neblina permanecem futuros no [roadmap](https://github.com/douglasopan/diablo-3d/blob/63e5e749d73c213aecf4b77b400aa04acc5a0598/docs/ROADMAP.md), com Tristram como prioridade.

## Revisão seguinte: fontes de fogo

A direção posterior pediu fontes de fogo com múltiplos emissores e oscilação sutil, além de uma abertura física e visível na janela traseira. A revisão com duas velas foi publicada em `cdeaaab0d` e está documentada em [Duas velas, duas janelas e o mesmo cômodo](/devlog/velas-janelas-cabana/).

As capturas e os testes acima continuam documentando o emissor histórico de `63e5e749` e o fechamento traseiro daquela etapa. As evidências da nova fonte e da janela aberta pertencem ao registro seguinte.
