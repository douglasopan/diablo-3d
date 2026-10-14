---
title: "Projeto no GitHub e download completo da local3"
date: 2026-10-14
description: "Código, ferramentas, editor e recursos próprios publicados. A nova montagem Windows inclui os arquivos de execução e pede apenas DIABDAT.MPQ da sua cópia original."
slug: local3-full-projeto-github
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "Captura automatizada histórica do Warrior e HUD na montagem privada local3; não é captura desta montagem pública."
category: Protótipo
order: 27
status: published
---

## Um download com os arquivos de execução

O primeiro ZIP da local3 era um checkpoint técnico e dependia de arquivos da montagem local. Preservamos esse registro e publicamos **local3 FULL**, com executável, bibliotecas, recursos públicos R17, os três packets próprios do Warrior e Clock.

[Baixar o pacote Windows completo](/download/). Extraia numa pasta e abra **Iniciar-Diablo3D.cmd**. Selecione **DIABDAT.MPQ da sua cópia original do Diablo**. A escolha fica salva; não precisa de Python, compilador, arquivos privados ou pack HD externo.

Esta é uma prévia: o jogo inteiro em 3D ainda está em desenvolvimento. Warrior Light com Short Sword/Buckler tem suporte em Tristram e Catedral 1, com primeira/terceira pessoa e HUD R4. K alterna as câmeras; Home restaura terceira pessoa; F4 alterna 3D/nativo.

## O projeto também foi publicado

[O repositório no GitHub](https://github.com/douglasopan/diablo-3d) contém o núcleo congelado da local3, ferramentas de autoria, editor Godot, documentação, conceitos e recursos próprios. [A branch do trabalho em andamento](https://github.com/douglasopan/diablo-3d/tree/snapshot/workspace-20261014) preserva as alterações locais ainda abertas; não corresponde ao executável qualificado. Os arquivos grandes de distribuição ficam nos anexos das releases.

## O que esta publicação verificou

Hashes de todos os arquivos, integridade do ZIP, tags de autoria Douglas Pan nas gravações próprias e preparo de perfil limpo passaram. O EXE é o mesmo local3; nenhuma otimização nova foi aplicada. Perfil e saves ficam separados em `%LOCALAPPDATA%/Diablo3D-local3-full/profile`.

A cena cardinal privada de staging não está neste download. Tristram usa os dados da cópia original do jogador. Os gates anteriores da montagem privada não qualificam automaticamente esta nova combinação de recursos: **teste manual desta montagem pública e desempenho permanecem pendentes**. A imagem deste artigo é uma captura automatizada histórica da montagem privada local3.

Não publicamos MPQ, saves, credenciais ou extrações proprietárias. O desenvolvimento continua a partir do checkpoint preservado, na fila existente.
