---
title: "Diablo 1 em primeira pessoa: nova prévia da Catedral"
date: 2026-10-12
description: "Diablo3D ganhou uma prévia local com caixão animado, câmeras de primeira e terceira pessoa e ajustes no HUD. Download e testes disponíveis na Panvia."
slug: primeira-pessoa-previa-nativa-catedral
image: /assets/banner.webp
image_alt: "Banner de identidade do Diablo3D; ilustração, não captura desta prévia."
category: Protótipo
order: 25
status: published
---

## Uma prévia local, com versão identificada

Em **12 de outubro de 2026**, entregamos a prévia local **R3 / 4fe0f467cd56**. Diablo3D busca reconstruir **todo Diablo 1 em 3D**, com primeira e terceira pessoa. A validação atual cobre Tristram e o primeiro andar normal da Catedral, não a campanha inteira.

Ela sucede a prévia nativa **d33d3aa9f3fc** e a revisão da logo/botão **42883d5f7f19**. Heróis e NPCs conservam sprites nativos. Este registro documenta a prévia local R3; consulte a Panvia para o download disponível atualmente.

## O caixão agora abre no jogo

O modelo fornecido por Douglas tem **corpo com 1.362 triângulos e tampa com 461**, sem redução da malha. Ocupa dois quadrados, com escala uniforme.

A tampa desliza conforme os estados nativos e **permanece parada quando aberta**. Interação, conteúdo e colisão continuam nativos. As texturas mantêm a resolução; a Catedral aplica sua paleta e iluminação, sem reproduzir todos os materiais PBR do GLB.

## Câmeras e HUD

Em terceira pessoa, paredes que obstruem a câmera são ocultadas visualmente, preservando a colisão do herói. Primeira pessoa continua disponível. A validação cobriu 16 ângulos da Catedral; isso não aprova todas as salas ou posições possíveis.

Inventário e mapa permitem arraste, e suas posições são preservadas ao reabrir. O inventário também admite ampliação, com o clique ajustado ao conteúdo. Isso não significa redimensionamento e sobreposição livres de todas as janelas.

A logo própria mantém animação e transparência suave sobre o fundo padrão. O botão e o texto de subida de nível abrem o painel do personagem, sem gastar pontos automaticamente. Os **nomes nativos dos itens** continuam visíveis no 3D conforme iluminação e linha de visão; são continuidade do recurso anterior, não um novo sistema de loot.

## Controles desta prévia

- **WASD ou setas:** movimento relativo à câmera.
- **Mouse e K:** olhar e alternar a câmera; **Home** restaura terceira pessoa.
- **I:** inventário; **Tab:** mapa; **Q:** missões; **B:** livro de magias.
- **E:** interação na masmorra. A coleta de um item próximo foi comprovada; portas e gatilhos ainda exigem revisão específica.
- **Botão esquerdo:** ataque parado; segurar respeita a cadência nativa e soltar interrompe. **Botão direito:** magia selecionada, com repetição e consumo nativos.
- **F4:** apresentação nativa ou 3D; **Esc:** menu.

Esses atalhos pertencem à versão identificada acima. O esquema alternativo de M/Q/J ainda não é seu padrão.

## O que os testes comprovam

Foram conferidos **231 arquivos** e passaram **111.564 verificações delimitadas**, com perfil sintético. Os testes incluem NEW, save/load, reentrada, GPU, UI e input no recorte Tristram/Catedral 1. O caixão foi desenhado nos estados fechado, intermediário e aberto, repetindo o aberto para verificar a permanência da tampa.

Isso não equivale a uma sessão completa com mouse físico, aprovação visual de toda a arte ou FPS sustentado. O teste amplo anterior encontrou um defeito no retorno do andar 2 ao 1; **os andares posteriores continuam sem qualificação**. Novos requisitos de teto e arcos também permanecem pendentes.

## Arte, IA e créditos

Douglas dirige o projeto. Usamos **IA no Codex para gerar e revisar código e textos**; conceitos, assets e experimentos também usam geração por IA, identificada quando aplicável. Revisão e testes não significam conclusão ou ausência de defeitos.

A arte própria continua em produção. H6 foi uma experiência HD **parcial e privada**; o pack DamnationX não entra no pacote público. Dados e sprites originais permanecem separados, e o gerador de masmorras é **estudo**, não recurso entregue.

Preservamos os créditos de [DevilutionX](https://github.com/diasurgical/devilutionX), Blizzard e autores originais. A adaptação de materiais referencia o mod independente [Diablo First Person de plopsDOTio](https://forge.plops.io/t/diablo-first-person-devilutionx-mod-for-windows); método e modificações estão [documentados no repositório](https://github.com/douglasopan/diablo-3d/blob/main/docs/DUNGEON-REFERENCE-ADAPTATION.md). São projetos distintos. A licença e os avisos herdados continuam aplicáveis.

## Download e testes na Panvia

[**Download e testes na Panvia**](https://panvia.online/jogos/thepan-art/diablo-3d/).

A página da Panvia reúne o download atual, a versão e as instruções de instalação. Nos testes, informe versão, passos e uma captura do defeito.
