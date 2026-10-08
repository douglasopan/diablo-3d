---
title: "Godot entra na revisão da arquitetura de Tristram"
date: 2026-10-08
description: "Um editor externo para inspecionar arquitetura, salvar a cena e exportar modelos estáticos ao DevilutionX, com validação, perfil de revisão e limites explícitos."
slug: editor-godot-arquitetura
image: /assets/captures/cabin-fire-angles.webp
image_alt: "Comparação histórica da cabana em quatro ângulos, publicada antes desta entrega. Esta imagem não mostra o editor Godot nem a correção da janela da porta."
category: Ferramentas
order: 12
status: published
---

O projeto passou a contar com um editor externo no Godot para inspecionar e trabalhar na arquitetura estática de Tristram. A cena pode ser salva e reaberta, e modelos compatíveis podem seguir por uma exportação explícita até o loader C++ do jogo. A entrega está no [commit e17b77f03](https://github.com/douglasopan/diablo-3d/commit/e17b77f0370df87732e227b7fa479d9b56c44280).

O jogo continua no DevilutionX, com sua simulação, colisões e saves. O editor auxilia a revisão dos objetos; portar Diablo 1 inteiro para Godot continua sendo uma avaliação futura. A imagem acima é histórica e serve somente como contexto da cabana preservada. Nenhuma nova captura local, textura ou modelo foi acrescentado ao site.

## A cena que o jogo realmente monta

O snapshot leva ao editor os triângulos efetivamente montados pelo jogo, incluindo os recortes e o interior adicionados pelo executável. A cena real validada contém **14 grupos de arquitetura**, **11.361 triângulos de arquitetura** e uma referência de chão com **12.544 células agrupadas em 503 malhas**. Essas contagens descrevem a cena preparada, sem representar novos modelos aprovados.

Há modos separados para o resultado editado, o snapshot herdado e o GLB fonte com os materiais PBR que o Godot consegue apresentar. O modelo fonte pode ser comparado à geometria montada, mas importar seu GLB não reconstrói nele os adjuntos de interior, luz e fogo do jogo.

Cada instância conserva identidade, variante, revisão e hashes dos bytes reais. Orbitar, deslocar e aproximar a câmera ajuda a conferir as faces. O corte para interior revela a sala sem remover geometria da cena salva ou da exportação. Salvar e reabrir preserva o trabalho; reconstruir a partir do snapshot é uma ação separada, com cópia da cena anteriormente salva.

## Chão e colisão como referências fixas

O chão permite conferir o encaixe das construções, e a sobreposição de colisão mostra as células nativas bloqueadas. As duas camadas ficam bloqueadas e fora da exportação de arquitetura.

Mover, girar ou escalar uma malha no editor não desloca colisões, portas, NPCs ou gatilhos da partida. Uma unidade da cena corresponde a uma célula nativa. As ferramentas de câmera e proporção da prévia ajudam a inspecionar o conjunto, mas não substituem a comparação na perspectiva original do jogo e em 360°.

## Da edição ao perfil de revisão

A importação de um GLB como referência fica separada da escolha de substituir uma instância no jogo. O responsável precisa marcar a substituição e informar uma revisão própria antes de exportar. O primeiro formato aceita malhas estáticas e materiais básicos opacos, com cor base e textura albedo quando presente; recursos incompatíveis causam erro em vez de desaparecerem silenciosamente.

O pacote é conferido pelo loader C++ antes de ser aplicado ao **perfil-godot-review**, com backup, recibo e hashes. Antes de abrir a partida, o iniciador também confere o executável, o manifesto e os modelos contra esse recibo. O perfil habitual permanece preservado. Salvar, exportar, aplicar e aprovar artisticamente continuam sendo decisões distintas.

A integração foi demonstrada com um substituto sintético de 12 triângulos no vínculo do poço, em um workspace descartável. Essa caixa é um teste da ponte, sem constituir uma nova edição artística do poço. O [guia do editor](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/GODOT-EDITOR.md) descreve o fluxo e seus pré-requisitos.

## A cabana com luz e fogo permanece protegida

O formato inicial ainda não transporta o comportamento de luz e fogo da cabana leste. Ela pode ser inspecionada, mas sua substituição está bloqueada nessa versão para preservar os recursos existentes. Ver materiais PBR na referência também não significa que todos esses materiais possam ser exportados para o jogo.

Esta entrega inclui uma correção delimitada no DevilutionX: a pequena janela já existente na porta da cabana passou a atravessar a parede interna que a bloqueava. O arquivo D3D selecionado, as divisões da porta e a colisão foram preservados; a porta continua fechada. A pipeline de aberturas e fogo avançou para v3, sem gerar outro modelo. Essa correção técnica ainda faz parte da revisão visual do conjunto.

## O que foi validado

A rodada de 8 de outubro de 2026 usou Godot 4.7.2, a cena real preparada e fixtures sintéticas para testar o contrato de exportação. Os resultados publicados são:

| Verificação | Resultado |
| --- | --- |
| Exportador | 185 verificações aprovadas com fixtures sintéticas |
| Ciclo real no Godot | 21 verificações, incluindo salvar, reabrir e exportar |
| Aplicação Python e loader C++ | 15 verificações em workspace descartável |
| Iniciadores no PowerShell 5.1 | 8 fixtures com lançamentos interceptados, sem abrir partidas |
| Regressão C++ completa | Sem falhas; 140.704 ms para a execução da suíte |

Foram conferidos hashes, limites, identidade, atlas, recibos e recusas de entradas incompatíveis, além da preservação das configurações e saves de revisão. Os executáveis locais normal e de qualidade receberam os mesmos bytes do candidato validado, com o perfil habitual intacto. A duração da regressão é o tempo de execução dos testes, sem constituir uma medição de desempenho por quadro.

## O próximo passo continua sendo a revisão dos objetos

O [contrato da ponte](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/GODOT-BRIDGE.md) limita esta versão à arquitetura estática e aos materiais básicos. Luzes autoradas, animação, edição de colisão e terreno e uma migração completa do jogo continuam fora da entrega. Os testes técnicos não encerram Tristram nem aprovam integralmente a arte.

A próxima ação é revisar a cabana com o responsável pelo projeto, registrar defeitos em 360° e evoluir o formato de interiores e luzes antes de permitir substituir essa baseline. O [guia de execução da revisão](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/PROJECT-EXECUTION.md) conserva essa sequência. O objetivo permanece reconstruir todos os níveis de Diablo 1 em 3D, começando por Tristram.
