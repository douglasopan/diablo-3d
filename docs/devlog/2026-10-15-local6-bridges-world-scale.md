---
title: "Local6: pontes, casas do Godot e mundo maior"
date: 2026-10-15
description: "Pontes corrigidas, sete casas salvas no Godot e mais 35% de escala em relação à local5."
slug: local6-bridges-world-scale
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "Referência histórica local3 do Warrior; não é uma captura da local6."
category: Protótipo
order: 30
status: published
---

A local 6 inclui Tristram e Warrior em primeira e terceira pessoa. Os quadrados do mundo estão mais 35% maiores que na local 5, chegando a 1,62× a escala original em relação ao personagem. Corpo, equipamentos e câmeras usam a mesma conversão física. As três pontes agora usam um tablado horizontal com a cobertura original, em vez de um relevo genérico elevado. A taverna mantém a restauração das peças nativas 382/385/386 da local 5.

Foram importados os ajustes das sete casas salvos no Godot, incluindo as escalas autoradas de Gillian e da casa sudeste. O deslocamento do Ground do editor foi tratado como registro: o piso continua na grade nativa e as arquiteturas mantêm a posição relativa ao piso salva no editor. Não é uma ampliação do mapa lógico: caminhos, colisão nativa, coordenadas e formato dos saves são preservados.

Baixe o ZIP completo, extraia em uma pasta nova e execute **Iniciar-Diablo3D.cmd**. Precisa apenas de **DIABDAT.MPQ da sua cópia original retail compatível**. Não exige Godot, Python, compilador ou pastas privadas. K alterna primeira/terceira pessoa; Home restaura terceira; F4 alterna 3D/clássico. Perfil separado `%LOCALAPPDATA%/Diablo3D-local6-grid162/profile`, Speed 32.

Testes: renderizador nativo CPU com NEW real, Warrior TP/FP, proporções/câmeras, três pontes, reprodução/restauração visual da taverna no modo clássico e quatro estados do poço.65.816 asserts incluem 65.536 comparações de texels, não 65 mil casos de gameplay. Instalação limpa MPQ-only,19 arquivos exatos, reuso/preservação, recusa de cena alterada, CRC e hashes do ZIP passaram.

Novos GPU/FPS e gameplay físico, colisão de edifícios reposicionados e 18 efeitos Godot permanecem sem qualificação. A investigação de RefreshLiveFrameRefused na Catedral continua. Esta é uma prévia de desenvolvimento. O pacote não inclui MPQ, extrações de imagens nativas, saves ou PDB. Créditos/licenças originais mantidos; música personalizada: Douglas Pan.

[Download / GitHub Release](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-preview.local6-tristram.grid162)
