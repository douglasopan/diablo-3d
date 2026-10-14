---
title: "Tristram completa da local3, com apenas o MPQ original"
date: 2026-10-14
description: "A nova distribuição prepara automaticamente a mesma cena selecionada de Tristram a partir do DIABDAT.MPQ, com Warrior em primeira e terceira pessoa. Instruções também em inglês no GitHub."
slug: tristram-completa-mpq-warrior
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "Captura automatizada histórica da local3: Warrior 3D e HUD na cena selecionada de Tristram; não é nova captura de instalação pública."
category: Protótipo
order: 28
status: published
---

## A mesma Tristram, no download

O pacote FULL3 incluía Warrior e arquivos de execução, mas deixava de fora a cena persistente de Tristram da montagem local. Isso não atendia ao pedido de publicar o que já temos pronto.

O snapshot continha imagens originais do Diablo embutidas. O launcher antigo apenas copiava esses arquivos privados. Agora o pacote inclui um componente interno que recupera os materiais a partir do **DIABDAT.MPQ do jogador**, sem distribuir essas imagens originais.

[Baixe a build corrigida](/download/), extraia o ZIP inteiro e abra **Iniciar-Diablo3D.cmd**. Selecione o MPQ original e aguarde a primeira preparação. Não precisa de Python, compilador, conversor externo ou outro download.

## O que preservamos

A preparação reproduziu **os 19 arquivos da cena selecionada, byte a byte**, conferidos pelos mesmos hashes do snapshot cardinal R2. Geometria, posições, UVs, materiais, identidades e edições permanecem iguais. A validação do loader real confirmou 14 arquiteturas, 25.743 objetos fixos, 1.349 texturas e quatro rotas, sem substituir a cena por um fallback.

O Warrior Light com Short Sword e Buckler continua em primeira e terceira pessoa em Tristram e Catedral 1. K alterna as câmeras; Home restaura terceira pessoa; F4 alterna 3D/nativo. HUD R4, executável local3 e Speed32 permanecem na mesma revisão. Não aplicamos otimizações de desempenho nesta publicação.

## Instalação verificada e instruções em inglês

Testamos o ZIP extraído, a primeira preparação, o reuso do perfil, a preservação de configurações e dados persistentes e a recusa de cena alterada sem sobrescrevê-la. O perfil fica separado em `%LOCALAPPDATA%/Diablo3D-local3-Tristram/profile`. A compatibilidade do MPQ é conferida: dados que não reproduzem a base selecionada são recusados.

[As instruções no GitHub também estão em inglês](https://github.com/douglasopan/diablo-3d/blob/main/docs/TRISTRAM-PUBLIC-PACKAGE.en.md), junto das notas bilingues da release e do LEIA-ME incluído. Fonte e dependências correspondentes são oferecidas para colaboradores.

Nenhuma janela, input físico, partida ou save foi criado por esses testes. Eles comprovam recuperação exata da cena e carregamento nativo; desempenho e teste manual de gameplay continuam pendentes. A imagem do artigo é uma captura automatizada histórica da local3, com a mesma revisão de cena e Warrior, não uma nova captura desta instalação. O objetivo permanece o Diablo 1 inteiro em 3D, ainda em desenvolvimento.
