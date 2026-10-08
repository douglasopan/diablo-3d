---
title: "Jogabilidade: trocar de página preserva o HUD"
date: 2026-10-08
description: "A página longa de Jogabilidade invadia os globos e impedia a composição do HUD HD. O menu agora respeita a área visível da interface, com a correção instalada no iniciador habitual."
slug: hud-paginas-jogabilidade
image: /assets/captures/hud-hd-offscreen-fullhd.webp
image_alt: "Composição técnica offscreen histórica do HUD em Full HD, publicada antes da correção das páginas de Jogabilidade; não mostra esta entrega nem comprova a troca de páginas 1→2→1."
category: Ferramentas
order: 19
status: published
---

Trocar uma página de configurações deveria mudar a lista de opções, preservando a interface da partida. Em **Jogabilidade**, porém, alternar **1→2→1** podia mudar a aparência da faixa inferior: a página longa interferia nos globos do HUD HD, enquanto a página menor podia conservar a arte.

A causa foi reproduzida, e a correção do [commit 1e28d7b5a](https://github.com/douglasopan/diablo-3d/commit/1e28d7b5a5cc90f4c40d9d2dfb59ee61fd71e8b1) já chegou ao mesmo **Iniciar-Tristram.cmd**, em **8 de outubro de 2026, às 19:51, horário de Brasília**. O desenho do menu e sua paginação agora usam o espaço realmente ocupado pelo HUD.

A imagem de abertura é uma **composição técnica offscreen histórica**, já publicada no [registro de itens e HUD](/devlog/itens-e-novo-hud/). Ela precede esta correção: **não é uma captura desta entrega nem demonstra a sequência 1→2→1**. Nenhuma nova captura foi acrescentada à publicação.

## Por que a página longa afetava os globos

O menu ainda calculava sua altura disponível a partir do painel nativo de **128 pixels**. Esse limite não acompanhava a área visível dos globos e botões escalados do HUD HD. Com mais opções na primeira página, o painel do menu avançava sobre essa área.

Ao detectar a sobreposição, uma proteção do compositor recusava o desenho da arte HD naquele quadro, retornando o estado técnico `Inactive`. A página menor não precisava ocupar o mesmo espaço e podia deixar o HUD aparecer. A reprodução anterior à correção confirmou esse cruzamento em Full HD.

Isso explica por que a troca de página parecia alterar o HUD mesmo sem mudar uma preferência da interface. A [documentação desta revisão](https://github.com/douglasopan/diablo-3d/blob/1e28d7b5a5cc90f4c40d9d2dfb59ee61fd71e8b1/docs/INGAME-SETTINGS.md#convivência-com-o-hud-hd--8-de-outubro) registra a causa e a correção.

## O menu acompanha a área visível

O desenho e a quantidade de entradas por página passam a compartilhar o mesmo limite, calculado com a moldura, os globos e os botões visíveis. No multiplayer, Chat e Friendly também entram nessa conta. Os layouts autorados são respeitados; sem HUD ativo, permanece o limite legado.

A consequência visível é simples: uma página pode ter menos linhas para deixar os globos livres, **preservando o tamanho da fonte e o acesso a todas as opções**. No layout padrão em solo, o teste confirmou **14 entradas por página em 1920×1080**, diante das 18 do cálculo histórico, e **9 em 640×480**. Multiplayer e personalizações de layout podem produzir capacidades diferentes.

A mudança não redesenha a arte nem desloca as áreas de clique do HUD. Desenho e clique continuam usando os mesmos retângulos do menu; callbacks, comandos e rotinas de entrada foram preservados. O ajuste reúne a geometria e a paginação em uma regra comum.

## Comparar antes, depois e a volta ao jogo

A comparação usou as versões anterior e corrigida com o **mesmo diagnóstico**, mundo original pela CPU e compositor SDL em software. Foram **36 quadros por versão**, com **623 verificações estruturais em cada execução**.

O percurso repetiu as páginas 1→2→1, acionou o rodapé por Enter e mouse, voltou à partida, abriu e fechou personagem e inventário e mudou a resolução lógica de **1920→640→1920**. Todos os quadros elegíveis da versão corrigida apresentaram a arte HD sem sobreposição ou falha de desenho.

Os **13 retângulos do HUD permaneceram iguais** entre as versões. Em **14 retornos ao estado anterior**, a comparação da região inferior encontrou **zero pixels diferentes**. Isso sustenta a continuidade visual nesses retornos; não representa uma comparação de todas as combinações possíveis de painéis.

O build Release/NONET x64 passou, assim como as regressões:

| Verificação | Resultado |
| --- | ---: |
| Configurações e navegação | 59.916 verificações aprovadas. |
| Caminho produtivo de entrada | 2.777 verificações aprovadas. |
| HUD | 946 verificações aprovadas. |

Amostras próprias em Full HD e resolução compacta foram inspecionadas visualmente, sem novo bloqueador técnico. O redimensionamento e a composição foram offscreen; não houve teste de uma janela física.

## Instalado no iniciador habitual

O principal integrou, compilou e instalou a correção. Os **três aliases** receberam o mesmo executável, com backup e **nenhum processo encerrado**. Os **44 arquivos do perfil** conservaram bytes, tamanho e data; a preparação posterior do iniciador mudou somente o recibo esperado de runtime.

Modelos, texturas, luzes, músicas, saves e preferências habituais foram preservados. A entrega também conservou os [controles de câmera, WASD e roda](/devlog/controles-primeira-pessoa/), repetindo as verificações produtivas de entrada. O [estado operacional dessa revisão](https://github.com/douglasopan/diablo-3d/blob/1e28d7b5a5cc90f4c40d9d2dfb59ee61fd71e8b1/docs/PROJECT-EXECUTION.md) mantém a fila existente de revisão visual e desempenho.

## O que conferir na partida

A próxima conferência é abrir **Esc → Configurações → Jogabilidade**, percorrer as páginas, abrir e fechar os demais painéis e retornar ao jogo pelo iniciador habitual. Combinações adicionais de layout e sessões prolongadas continuam para avaliação.

As imagens enviadas pelo autor não ficaram acessíveis nesta rodada e não foram comparadas. A reprodução técnica própria sustenta a correção do caso exercitado, enquanto a **confirmação do defeito na partida e a aprovação artística pelo usuário permanecem pendentes**.

Não houve execução em GPU física, teste de entrada ou foco físicos, nem medição nova de FPS. Esta entrega corrige a convivência entre menu e HUD; as melhorias de desempenho anteriores continuam registradas em seus próprios artigos.
