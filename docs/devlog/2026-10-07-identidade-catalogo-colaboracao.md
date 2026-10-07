---
title: "Uma identidade D3D e um catálogo para colaborar"
date: 2026-10-07
description: "A animação fornecida pelo autor e o catálogo de Tristram organizam a identidade do projeto e a colaboração em famílias de objetos."
slug: identidade-catalogo-colaboracao
image: /assets/banner.webp
image_alt: "Banner oficial Diablo 3D fornecido para a identidade visual do projeto."
category: Comunidade
order: 6
---

O projeto precisa tornar visíveis tanto seus avanços quanto as formas de participar. Em 7 de outubro de 2026, dois commits ampliaram essa base: um catálogo para coordenar objetos de Tristram e a animação D3D fornecida pelo autor para a entrada, o menu principal e a pausa.

O [commit 80c291fcb](https://github.com/douglasopan/diablo-3d/commit/80c291fcb3bead7741722d8f78c84a0fc25e1a94), publicado às 16:05, adicionou catálogo, registro e verificação da estrutura. O [commit 8f031fa3b](https://github.com/douglasopan/diablo-3d/commit/8f031fa3b8ebbe713ddf80a6383ed8a289347232), às 16:06, integrou a animação. Os horários são de São Paulo. Estas são datas de publicação verificadas no histórico Git.

## A arte entregue ganha espaço no jogo

A animação recebida contém 240 quadros, a 30 quadros por segundo, com ciclo de oito segundos. O importador adapta tamanho, paleta e cobertura ao renderer de software sem redesenhar a arte. Cada tela usa um recurso próprio, adequado à área disponível.

O ciclo é calculado em 8.000 milissegundos. Essa escolha evita acumular o erro que surgiria ao arredondar cada intervalo de 30 fps para 33 milissegundos. A pausa também usa a paleta ativa do jogo, em vez de presumir que sua paleta seja a mesma do menu principal.

Os arquivos fornecidos e as adaptações têm proveniência registrada. O [documento da animação](https://github.com/douglasopan/diablo-3d/blob/8f031fa3b8ebbe713ddf80a6383ed8a289347232/docs/ANIMATED-LOGO.md) distingue esse material independente do experimento anterior que reconstruía uma alternativa a partir da arte nativa, mantida localmente. O banner completo identifica as páginas do projeto; o símbolo D3D identifica ícones e avatares.

Os diagnósticos da animação e dos recursos instalados passaram. A rodada registrada não realizou uma revisão manual completa da interface aberta. A mudança também não altera a geometria da cidade, cuja auditoria anterior permanece separada.

## Reservar uma família, construir um objeto

O catálogo oferece IDs estáveis para construções, árvores, pedras, habitantes e tarefas de identificação. Uma árvore pode ter várias instâncias no mapa. Uma casa pode incluir componentes e variantes. Esses casos devem ser coordenados como famílias e objetos completos, evitando pedidos concorrentes para cada fragmento ou direção da câmera.

O fluxo começa pela consulta ao [catálogo](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md), registro e issues existentes. O colaborador abre uma reserva para um ID e espera a confirmação do mantenedor antes de modelar. A confirmação define escopo e responsável; a revisão registra evidência e revisão aprovada.

Os estados distinguem trabalho necessário, protótipo, revisão e aceitação. No snapshot documentado, há **zero modelos manualmente autorados aceitos**. Uma representação procedural existente continua sendo um protótipo, e uma fonte identificada não prova fidelidade artística.

## Evidência e origem acompanham a contribuição

Um objeto precisa conservar composição, escala, portas, janelas, chão e posição. A comparação deve usar malhas forçadas na perspectiva original, seguida de giros pequenos e uma volta completa. Home usa o backend original e não serve sozinho para aprovar a reconstrução.

A proposta também informa autoria, licença e origem dos recursos. Arquivos do jogo, extrações, saves, modelos derivados que redistribuam essa arte e credenciais ficam fora do repositório. Capturas contextuais autorizadas de desenvolvimento são apresentadas neste devlog com sua etapa identificada.

O código preserva a [Sustainable Use License](https://github.com/douglasopan/diablo-3d/blob/8f031fa3b8ebbe713ddf80a6383ed8a289347232/LICENSE.md), com distribuição gratuita e não comercial. D3D é um projeto independente de fãs, sem afiliação com a Blizzard. O [Discord oficial](https://discord.gg/4YxQ7s69S) e as [issues](https://github.com/douglasopan/diablo-3d/issues) são os pontos de coordenação. A prioridade compartilhada é completar Tristram antes do primeiro nível procedural da Catedral.
