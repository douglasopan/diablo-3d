# Nomes de itens no mundo 3D

**Instalado em 10/10/2026, 00:50:47 (Brasília)** pelo mesmo **Iniciar-Tristram.cmd**, SHA-256 `1fe70560e8eb50509ddb440deccba7d1afd0f7ae6dcb6027b2ae06d3379018d6`, 6.734.336 bytes. Os três aliases são idênticos; o [guia de execução](PROJECT-EXECUTION.md) registra a entrega e seus limites.

O pedido nasceu de um nome de item, como CAPE, que aparecia no desenho nativo e desaparecia no mundo 3D. A implementação leva a mesma função de destaque do DevilutionX para a projeção 3D: conserva nome, quantidade de ouro, cores, opções e ID do item. A seleção pelo texto segue os comandos nativos de interação; não cria um segundo inventário ou uma regra de coleta.

## Como usar

Segure **Item highlighting**, por padrão Alt esquerdo, para mostrar os nomes dos itens no chão. **Show Item Labels**, nas opções de gameplay, mantém os nomes visíveis. **Toggle item highlighting**, por padrão Ctrl direito, alterna essa preferência. As ações são remapeáveis: o perfil do jogador pode usar outras teclas.

O comportamento continua sendo o nativo: a ação temporária inverte a preferência permanente enquanto estiver pressionada.

| Preferência permanente | Ação temporária | Legendas fora da loja |
| --- | --- | --- |
| Desligada | Solta | Ocultas |
| Desligada | Pressionada | Visíveis |
| Ligada | Solta | Visíveis |
| Ligada | Pressionada | Ocultas temporariamente |

Com o cursor livre, passe sobre a legenda para selecionar o item e use o clique normal do jogo. O destaque azul acompanha a caixa final do texto, inclusive depois do ajuste de sobreposição entre nomes. O destaque pode mostrar um item encoberto por uma parede, como no comportamento nativo; isso não elimina a parede, altera o caminho até o item ou permite coletá-lo à distância.

Em primeira pessoa com o mouse capturado, o texto não recebe seleção pelo cursor. O retículo continua selecionando o mundo 3D por seu caminho próprio. As legendas podem permanecer visíveis, mas não fornecem um alvo através da parede ao retículo. Após liberar o mouse e publicar um quadro atual, a seleção pelo texto volta a usar o cursor livre. Os controles de captura seguem [TRISTRAM-FIRST-PERSON-INPUT.md](TRISTRAM-FIRST-PERSON-INPUT.md).

## Texto e bloqueios de interação

O texto é produzido pelo mesmo helper do caminho nativo: `Item::getName()` conserva o nome traduzido e o estado de identificação; ouro usa a quantidade formatada e a expressão localizada. Fonte, margens, cores de `Item::getTextColor()`, fundo semitransparente e destaque da seleção continuam nativos.

Um item só recebe âncora se estiver ativo, tiver sprite, posição e região de seleção válidas e corresponder à ocupação `dItem` daquela célula. A animação de queda deve ter terminado, com a exceção nativa da Magic Rock. A âncora precisa estar no viewport; no piloto da Catedral, sua célula também precisa pertencer ao frame corrente. Isso limita a apresentação, sem mudar o alcance da coleta.

Lojas ocultam as legendas. Menu ativo, pausa, morte, ação em andamento, personagem invencível, item carregado no cursor, identificação, seleção de magia, arraste da câmera e áreas ocupadas pelo HUD ou pelos painéis impedem a seleção projetada. Um texto visível não contorna esses bloqueios.

## Frame atual e seleção nativa

`town_view` publica âncoras de posição e ID junto de um quadro 3D completo. A âncora é projetada acima do apoio do item e conserva a possibilidade de destaque através de objetos. Esse caminho percorre a lista de itens; não varre o framebuffer, decodifica sprites ou consome o RNG da simulação.

`DrawItemNameLabels` aplica às âncoras o ajuste nativo de sobreposição. Como as coordenadas já pertencem ao viewport lógico, o caminho 3D não aplica outra vez o zoom do desenho nativo. O desenho nativo mantém seus offsets CLX, altura do tile e zoom próprios.

`SelectProjectedItemLabelAt` usa a caixa final, exige o identificador do frame atual e revalida lista ativa, ID, posição, `dItem`, seleção e animação do item. Conserva a ordem nativa de desempate. Ao selecionar uma legenda, o cursor limpa outros estados de hover e atualiza `pcursitem` e a célula nativa. Uma legenda válida pode receber clique mesmo sobre a área de céu; pathfinding, colisão e comando de interação continuam pertencendo ao jogo.

F4, retorno ao modo nativo, fallback, resize, mudança de câmera ou quadro recusado invalidam a seleção antiga. Uma caixa de uma publicação anterior não pode selecionar um item removido ou deslocado. A revalidação do item também vale quando a imagem ainda não foi redesenhada.

## Validação e entrega

Os resultados abaixo foram reexecutados na revisão final R4. Resultados anteriores continuam históricos. O core não testa captura FPP; o runner FPP usa serviços físicos SDL simulados. Seu companheiro de legendas faz parte do total, sem somar os mesmos checks novamente.

| Registro final | Estado da entrega |
| --- | --- |
| Build e fontes R4 | **PASS** — recibo `compiled-final-r4.json`, SHA `6d6e3ee178b5a97ef27b43023a354ea04a5505407e53cc52d9cbff9705cc09dd` |
| Core de legendas | **PASS** — 253 checks, 14 quadros de mundo, três hardware e seis PNGs; recibo `a725c1b8a299213eabfdf60fdb3a976a9252d3208deca84657e055080b490814` |
| FPP e companheiro de legendas | **PASS** — 3.851 checks totais, incluindo 47 do companheiro; três quadros CPU e um gesto de captura simulado; recibo do companheiro `1ee841caedec233e06639090d770fdaedfcea6d39eec4175cfd69d5e6f97b979` |
| Paredes e iluminação | **PASS no domínio conhecido** — 12 quadros/seis GPU; RAW35/36 e suporte 269. Operadores estritos permanecem `UNSUPPORTED` por ausência de 255 nas quatro colunas próprias; unidade sintética separada. Envelope `f68b6405d7bf26664c2bd64454ef723ecc24ebdce87431d0285f6ebe882a4e2a` |
| Instalação no launcher habitual | **CONCLUÍDA** — 10/10/2026, 00:50:47 BRT; três executáveis e somente dois campos do recibo derivado; 639 arquivos protegidos por bytes/tamanho/data. Recibo `0d710e4b476eb72766651d7c367f9faa54c212806c99e1ef9c6d8c3bdb7010ae` |
| QA física | **PENDENTE** — alternância, cursor capturado/livre, clique/coleta, painéis e queda de item |

Serviços de mouse simulados não provam captura física ou Alt+Tab. Seleção por legenda e preservação de witness não provam coleta na partida. Fixtures offscreen não demonstram HUD completo, gameplay integral ou FPS sustentado. A atualização das legendas não conclui a arte da Catedral nem os marcos G2/G3/G4; a fila permanece no [guia de execução](PROJECT-EXECUTION.md).

Recibos, perfis, saves e pixels proprietários dos oracles ficam privados. A associação entre candidato, fontes, objetos e executáveis de teste está registrada por hash; os gates executaram fixtures do mesmo grafo, sem alegar execução integral da partida. Recibo privado de instalação: `diagnostics/item-labels-20261009-r1/install-preparation/installed-20261010T035047Z-34cf23dbd66a424f9da9f49b1e82e2c2/receipt.json`.
