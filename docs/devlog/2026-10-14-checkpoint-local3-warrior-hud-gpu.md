---
title: "Checkpoint local3: Warrior 3D, HUD e versão preservada"
date: 2026-10-14
description: "Fechamos o ponto atual do Diablo3D, com binários e fonte para download, Warrior TP/FP na montagem local e HUD R4. Desempenho e distribuição dos runtimes seguem em trabalho."
slug: checkpoint-local3-warrior-hud-gpu
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "Warrior 3D com espada e escudo em Tristram, junto ao painel de personagem e HUD; captura automatizada do checkpoint local3."
category: Protótipo
order: 26
status: published
---

## Fechamos este ponto para continuar com segurança

Preservamos a **local3, de 14 de outubro de 2026**, antes de continuar as próximas correções. O executável, a fonte correspondente e seus hashes identificam o que foi entregue. A cena e os recursos privados da montagem de teste também ficam congelados localmente; eles não vão para o download público.

O objetivo continua sendo **todo Diablo 1 em 3D**, com primeira e terceira pessoa. Esta versão cobre Tristram e o piloto do primeiro andar normal da Catedral. Ela não conclui a campanha, a arte ou o desempenho.

## O Warrior já aparece no jogo em 3D

Na montagem local qualificada, o **Warrior com armadura leve** usa corpo 3D em terceira pessoa e olhos, braços e mãos do **mesmo rig e da mesma pose** em primeira pessoa. Não criamos outro relógio de animação para a câmera: a lógica nativa continua comandando as ações.

A Short Sword e o Buckler usam seus modelos existentes. Sem item equipado, não fica uma arma residual. Equipamentos sem attachment podem usar o sprite correspondente do inventário; essa alternativa em GPU ainda precisa de qualificação própria. Classes, armaduras, ações e níveis fora da cobertura conservam a apresentação nativa. Pegadas, enquadramento e acabamento continuam sujeitos à revisão.

![Braços, espada e escudo vistos a partir do rig do Warrior no teste automatizado da local3.](/assets/captures/local3-warrior-fp.webp)

Esta imagem vem do **teste automatizado do renderizador**, não de uma gravação de gameplay com mouse físico. Ela mostra o recurso na montagem privada com runtimes selados; não promete que esses recursos acompanhem o ZIP técnico público.

## O HUD acompanha movimento e redimensionamento

O HUD R4 reúne correções de tamanho, conteúdo e áreas clicáveis dos painéis, além de foco, sobreposição, fechamento pelo X, mapa e relógio. Os testes cobriram fechar e reabrir, limites de tamanho e a composição com o mundo.

Nos contextos admitidos, **Ctrl + botão esquerdo** move um módulo e **Ctrl + Shift + botão esquerdo** redimensiona. **Ctrl + botão direito** restaura o módulo; com Shift, restaura todos. Arrastar itens e abrir modais interrompe a edição do HUD. A transparência atua no fundo, preservando texto e itens.

## Tristram fixa, Catedral e controles

A montagem local carrega uma **cena persistente selada** e quatro corredores cardinais testados em staging. Isso não substitui automaticamente a cidade que Douglas está editando no Godot. Salvar ou exportar no editor também não instala uma nova cena numa partida aberta.

**K** alterna primeira e terceira pessoa; **Home** volta à terceira na sessão; **F4** alterna a apresentação 3D e a original. Os atalhos admitem remapeamento e são bloqueados nos modais apropriados. O teste de terceira pessoa percorreu comandos SDL, pacotes e lógica nativa: caminhada pelo clique e interação E com uma porta próxima. Não equivale a testar todas as interações da campanha.

## O que foi verificado

Os gates da fonte final registraram:

| Recorte | Resultado |
| --- | --- |
| Warrior CPU | 2.301 verificações e 49 imagens |
| Comandos de terceira pessoa | 158 verificações em cinco casos |
| HUD | 29.263 verificações e 154 imagens |
| Diagnósticos locais | 8.272 verificações em seis casos |
| Radeon RX 570 | 33.393 verificações, cinco readbacks e um Present oculto real |

Os testes incluem Town/Catedral1, equipamento, pose, braços, portas e preservação de profundidade e seleção. Usam fixtures e capturas automatizadas da fonte final. Não certificam mouse e teclado físicos, multiplayer, campanha completa ou aceite artístico. O texto antigo de câmera visível em algumas capturas é histórico; valem os controles descritos acima.

## A lentidão continua sendo nossa próxima prioridade

Não vamos apresentar esta publicação como uma solução de desempenho. Uma medição estacionária oculta, em **1280 × 720, RX 570, AA desligado e VSync ligado**, registrou médias de **214,85 ms em Tristram** e **176,02 ms na Catedral**. Os primeiros frames também têm custo elevado.

São tempos do renderizador, não FPS medidos de gameplay. O custo de gravação da cena na CPU foi identificado como gargalo importante; as correções seguintes ainda não integram este checkpoint. Os encerramentos anteriores continuam sem causa comprovada. Preservar esta versão permite comparar o próximo resultado com uma base identificada.

## Download: o que está público e o que continua local

[**Baixar o checkpoint técnico local3 e a fonte correspondente**](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-checkpoint.local3).

O ZIP de **4.303.106 bytes** preserva o programa e os scripts da candidata. **Não é um instalador completo.** Não inclui DIABDAT.MPQ, saves, packs, os mapas/modelos privados nem os assets R17. Seu launcher exige esses runtimes selados e assets compatíveis: **DIABDAT.MPQ sozinho não basta**. As instruções e os SHA256 acompanham a release.

Quem já possui a montagem local deve usar um perfil novo pelo preparo explícito, preservando saves e configurações anteriores. Para instalação pública convencional, a [R17 anterior](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-early-access.r17) continua disponível separadamente. Ela mantém heróis em sprites e não deve ser confundida com o novo Warrior da montagem local3.

Fonte do checkpoint: `24080e8900d695f9c88f9d3ad417917499d8103f`. Executável: `1.6.0-dev-Release-24080e8`, SHA256 `0f4d9290c0589cbee3eb6c22791f373d4a1b67dd618b97e3bfb5a86637f9bd6c`. Use o arquivo de fonte correspondente anexado à release; o arquivo automático do GitHub acompanha a revisão do site, não a fonte privada integrada.

## Publicamos este marco e continuamos

Vamos continuar a correção pontual de desempenho e a revisão de câmera e interface sobre essa base. Entorno procedural, baú central adicional, novos efeitos e integração Panvia permanecem separados desta entrega.

Douglas Pan dirige o projeto. Codex participa do desenvolvimento de código, textos e traduções; recursos gerados por IA são identificados e continuam exigindo revisão. Preservamos licença, autoria e créditos de DevilutionX, Blizzard e colaboradores. A publicação é gratuita e não comercial; dados originais do jogo não são distribuídos.
