---
title: "Duas câmeras de gameplay e continuidade da Catedral"
date: 2026-10-09
updated: 2026-10-10
description: "Câmeras e continuidade da Catedral, com atualização local de 10 de outubro: grades, iluminação e nomes nativos dos itens no mundo 3D. Gameplay e arte continuam em revisão."
slug: cameras-continuidade-catedral
image: /assets/banner.webp
image_alt: "Banner de identidade do projeto Diablo 3D; ilustração, não captura desta entrega."
category: Protótipo
order: 24
status: published
---

## Instalado na entrada habitual

A atualização de **9 de outubro, 21:16 (Brasília)** está instalada pelo mesmo launcher. O [commit 7e4a6c3b](https://github.com/douglasopan/diablo-3d/commit/7e4a6c3b3796a98f371b6a4817ad259c1bf91467) reúne as duas vistas de gameplay e a correção de uma causa reproduzida de retorno da Catedral ao renderer 2D. Modelos selecionados, simulação, colisão, saves e configurações foram preservados.

**K e o menu** oferecem terceira e primeira pessoa. A roda aproxima até a primeira pessoa e afasta de volta; **Home restaura terceira pessoa na sessão**, enquanto **F4 alterna a apresentação nativa e 3D**. A arquitetura local de Tristram que contém ou obstrui o olho da câmera fica temporariamente oculta para o jogador local e reaparece ao sair. A altura comum de 1,7 unidade é provisória: calibração por classe, árvores, objetos de cenário e Catedral ainda não entram nessa ocultação inicial.

## A causa corrigida na Catedral

O orçamento das transparências cobrava uma tela inteira por camada, embora a GPU já copiasse regiões. A admissão agora soma as caixas realmente emitidas, mantendo os limites de **2.048 camadas e 512 MiB**.

Em oito casos equivalentes na Radeon RX 570, a versão anterior publicou seis quadros na GPU e recusou dois; a correção publicou os oito. O maior caso chegou a **408 camadas e 19.556.601 bytes**, sem rasterização CPU ou WARP. Cor, profundidade e seleção foram preservadas nas saídas comuns comparadas. Esse teste técnico usa cenário parcialmente inicializado e AA efetivo 1×; não é benchmark de gameplay nem comprova todas as situações de retorno ao 2D.

O piloto continua restrito a nove regiões próximas no primeiro andar normal. Revisão física da continuidade, atualização dos quadros opacos, composição das paredes e arte final seguem pendentes. Os demais andares e níveis de missão permanecem nativos.

## Atualização de 10 de outubro: grades e nomes de itens

O [commit 29097acd](https://github.com/douglasopan/diablo-3d/commit/29097acdc9c30a9662006f469ab11c38a7dc5ae6) foi instalado em **10 de outubro, 00:50:47 (Brasília)** pelo mesmo **Iniciar-Tristram.cmd**. Modelos selecionados, configurações e saves foram preservados. Esta é uma instalação local; a distribuição de uma build pública continua em preparação.

Os nomes e a seleção nativos dos itens agora aparecem no mundo 3D. A ação **Item highlighting**, por padrão Alt esquerdo, pode ser remapeada. Ela e a preferência **Show Item Labels** conservam o comportamento do jogo. Com o cursor livre, a seleção pelo texto usa os comandos nativos. Em primeira pessoa com mouse capturado, o texto não recebe seleção pelo cursor; o retículo segue seu próprio caminho. Uma legenda visível não permite coletar um item à distância ou atravessar uma parede.

As grades conhecidas RAW35/36 usam suas colunas próprias e a combinação nativa de escrita opaca e transparência. A correção remove paredes visuais falsas nos suportes de luz L1LIGHT, mantendo objetos, colisão e comandos. Pilares e arcos recebem material de alvenaria; a iluminação nativa é preparada em cache. Grades sem luz podem continuar legitimamente pretas. Alturas, faixas repetidas, materiais doadores, portas e a arte geral ainda são aproximações.

O gate de legendas passou **253 verificações em 14 quadros**, incluindo três na GPU. Paredes e iluminação passaram em **12 quadros**, seis na RX 570. A verificação de primeira pessoa usa três quadros CPU e serviços SDL simulados, sem comprovar input físico. A suíte estrita de operadores permanece **UNSUPPORTED** porque o índice de paleta 255 não existe nas quatro colunas próprias examinadas; o gate do domínio conhecido passou. A unidade sintética separada cobre os 256 valores, sem fabricar esse caso na cena real. Esses resultados não concluem gameplay, coleta na partida, arte final ou FPS sustentado.

## Próximas etapas e contexto do vídeo

O [devlog em vídeo](/devlog/devlog-em-video/) registra uma etapa anterior. Ele não demonstra esta nova instalação. Esta publicação não acrescenta uma captura de gameplay nem anuncia FPS sustentado.

A prioridade continua sendo concluir e estabilizar **todo Diablo 1 em 3D**. O [roadmap](/roadmap/) coloca o multiplayer futuro, dependente de mundo persistente e renovação de recursos, antes da expansão da superfície. O build atual permanece offline.
