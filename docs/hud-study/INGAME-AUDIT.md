# Auditoria dos painéis e menus durante a partida

Estudo autorizado pelo pedido de 8 de outubro de 2026: analisar as telas atuais e criar exemplos de cada uma antes de implementar a interface. Este documento registra a leitura do código existente; não altera a fila central nem constitui outro plano de implementação. G1, a cabana de referência, continua sendo a etapa visual do projeto.

Os dados da galeria estão em [data/ingame.json](data/ingame.json): **66 exemplos individuais**, com identificadores próprios, fontes e condicionais. São 23 exemplos de painéis/controles, 8 menus iniciais de NPC, 8 submenus de conversa e 27 telas comerciais. Os dois auxiliares de gamepad pertencem à mesma família; assim, os 23 exemplos cobrem 22 famílias de painel/controle.

Todos são **conceitos visuais propostos, não capturas do executável**. Nomes de heróis, mensagens, saldos, itens genéricos e seus valores são demonstrativos. O preço de 50 ouro para olhar a oferta de Wirt e os 100 ouro para identificar com Cain são regras atuais preservadas. Textos narrativos ilustrativos são autorais e não estabelecem lore canônica.

## Método e alcance

Foram lidos `AGENTS.md`, `docs/PROJECT-EXECUTION.md`, a ordem de desenho da partida e os módulos responsáveis. O inventário abaixo distingue comportamento encontrado no código, variante condicional e proposta de apresentação. Nenhuma partida foi iniciada para percorrer os fluxos; telas multiplayer, de toque, shareware, Hellfire e debug não foram validadas em execução. O guia registra o build local com `NONET=ON`.

O escopo de autoria fica restrito a este documento e seu JSON. Menus gerais, opções, áudio, executáveis, launchers e guia central pertencem a outros responsáveis. Não houve staging ou commit.

## Painéis e controles

| IDs | Tela ou família | Evidência |
| --- | --- | --- |
| P01 | Personagem: nome/classe, nível/XP/próximo nível; atributos Base/Agora, pontos; ouro, defesa, chance de acerto, dano, vida/mana, três resistências | [charpanel.cpp:132](../../Source/panels/charpanel.cpp#L132) |
| P02 | Inventário: 7 posições equipadas, mochila 10×4 e cinto de 8 | [inv.cpp:1187](../../Source/inv.cpp#L1187), [player.h:38](../../Source/player.h#L38) |
| P03 | Personagem e inventário simultâneos; inspeção também usa esses painéis | [scrollrt.cpp:1436](../../Source/engine/render/scrollrt.cpp#L1436) |
| P04 | Grimório: 7 posições por página, 4 abas Diablo e quinta condicional Hellfire | [spell_book.cpp:36](../../Source/panels/spell_book.cpp#L36) |
| P05 | Lista rápida: habilidade, magia, pergaminho e cajado; contadores e atalhos | [spell_list.cpp:115](../../Source/panels/spell_list.cpp#L115) |
| P06 | Diário: missões ativas selecionáveis e concluídas; ativa abre relato/voz | [quest_log.cpp:84](../../Source/panels/quest_log.cpp#L84) |
| P07 | Texto narrado de NPC, missão, livro ou nota; rolagem sincronizada ao áudio | [minitext.cpp:94](../../Source/minitext.cpp#L94) |
| P08 | Baú 10×10, 100 páginas, navegação ±1/±10 e saldo; inventário aberto junto | [stash.cpp:42](../../Source/qol/stash.cpp#L42), [stores.cpp:2022](../../Source/stores.cpp#L2022) |
| P09 | Sacar ouro: limite por saldo e espaço na mochila | [stash.cpp:590](../../Source/qol/stash.cpp#L590) |
| P10 | Dividir/remover ouro de pilha do inventário | [control_gold.cpp:48](../../Source/control/control_gold.cpp#L48) |
| P11 | Automapa opaco, transparente e minimapa; exploração/local/dificuldade | [automap.h:86](../../Source/automap.h#L86), [automap.cpp:1427](../../Source/automap.cpp#L1427) |
| P12 | Entrada de chat e seleção de destinatários | [control_chat.cpp:91](../../Source/control/control_chat.cpp#L91) |
| P13 | Histórico: contagem, hora, autor/nível, rolagem e não lidas | [chatlog.cpp:155](../../Source/qol/chatlog.cpp#L155) |
| P14 | Ajuda longa rolável; títulos Diablo/Hellfire/shareware | [help.cpp:191](../../Source/help.cpp#L191) |
| P15 | Detalhes de item único adjacentes a inventário/baú | [items.cpp:1734](../../Source/items.cpp#L1734), [items.cpp:4097](../../Source/items.cpp#L4097) |
| P16 | Grupo multiplayer: vida/mana/retratos/morte; inspeção de aliado em leitura | [partypanel.cpp:166](../../Source/panels/partypanel.cpp#L166) |
| P17 | Morte com ação contextual para solo com save, solo sem save e multiplayer | [control_panel.cpp:789](../../Source/control/control_panel.cpp#L789) |
| P18 | Notificações temporárias de sistema e conteúdo contextual | [diablo_msg.cpp:160](../../Source/diablo_msg.cpp#L160) |
| P19 | Documento ilustrado do item Cathedral Map; separado do automapa | [doom.cpp:23](../../Source/doom.cpp#L23), [items.cpp:4313](../../Source/items.cpp#L4313) |
| P20–P21 | Auxiliares transitórios de menus e quatro magias no gamepad | [modifier_hints.cpp:142](../../Source/controls/modifier_hints.cpp#L142) |
| P22 | Gamepad virtual de toque: direção, ações, magia, cancelar, poções e menu | [renderers.cpp:273](../../Source/controls/touch/renderers.cpp#L273) |
| P23 | Console Lua: entrada, histórico, saída/avisos/erros e autocomplete | [console.cpp:1](../../Source/panels/console.cpp#L1), [console.cpp:560](../../Source/panels/console.cpp#L560) |

Pausa isolada é a palavra “Pause” centralizada em [gmenu.cpp:191](../../Source/gmenu.cpp#L191); fica associada à auditoria de menus gerais. Rótulos de item no chão, barra de vida de inimigo, números flutuantes, aviso de durabilidade, botão de subir nível e feed temporário de mensagens são elementos do HUD, não menus separados. A ordem de desenho em [scrollrt.cpp:1432](../../Source/engine/render/scrollrt.cpp#L1432) é a referência para integrá-los.

## NPCs e submenus de conversa

| Menu inicial / Conversar | NPC | Ações atuais do menu inicial |
| --- | --- | --- |
| N01 / N09 | Griswold | Conversar; comprar básicos; comprar premium; vender; reparar; sair |
| N02 / N10 | Adria | Conversar; comprar; vender; recarregar cajados; sair |
| N03 / N11 | Pepin | Conversar; comprar; sair. A abertura cura automaticamente |
| N04 / N12 | Cain | Conversar; identificar item; despedir-se |
| N05 / N13 | Wirt | Conversar; pagar 50 ouro para olhar se há oferta; despedir-se |
| N06 / N14 | Ogden | Conversar; sair da taverna |
| N07 / N15 | Gillian | Conversar; acessar armazenamento; despedir-se |
| N08 / N16 | Farnham | Conversar; despedir-se |

Fontes dos menus iniciais: [stores.cpp:433](../../Source/stores.cpp#L433), [691](../../Source/stores.cpp#L691), [972](../../Source/stores.cpp#L972), [1031](../../Source/stores.cpp#L1031), [1073](../../Source/stores.cpp#L1073), [1248](../../Source/stores.cpp#L1248), [1261](../../Source/stores.cpp#L1261), [1274](../../Source/stores.cpp#L1274).

Conversar é uma tela própria com rumores, tópicos pertinentes das missões ativas e Voltar. Shareware mostra aviso de indisponibilidade. Os tópicos dos exemplos não provam que aquele NPC discute aquela missão em qualquer save; a seleção real depende de `GetTownerQuestDialog` e do estado da quest. Fonte: [stores.cpp:1205](../../Source/stores.cpp#L1205).

Homem ferido, vacas, Lester, Nut e Celia não possuem novas lojas no código: usam fala/quest. Lester/Nut/Celia dependem de Hellfire e de condições específicas. Reutilizam P07. Fonte: [towners.cpp:273](../../Source/towners.cpp#L273), [towners.cpp:741](../../Source/towners.cpp#L741).

## Telas comerciais

| IDs | Exemplos individuais |
| --- | --- |
| T01–T04 | Griswold: comprar básicos, comprar premium, vender e reparar |
| T05–T07 | Adria: comprar, vender e recarregar cajados |
| T08 | Pepin: comprar |
| T09 | Cain: selecionar item para identificar |
| T10 | Wirt: oferta de um item |
| T11–T14 | Grade visual de Griswold, Adria, Pepin e Wirt |
| T15–T20 | Confirmar compra, venda, reparo, recarga, identificação e negócio de Wirt |
| T21–T22 | Avisos de ouro insuficiente e inventário sem espaço |
| T23–T26 | Estados vazios: venda, reparo, recarga e identificação. Venda tem variantes Griswold/Adria |
| T27 | Resultado da identificação com nome e propriedades revelados |

A lista exibe nome, preço, qualidade/requisitos, poderes identificados, cargas, dano/defesa e durabilidade quando pertinentes, além de ouro e retorno; listas longas têm rolagem. Fonte: [stores.cpp:355](../../Source/stores.cpp#L355). O enumerador de estados e a construção de cada tela estão em [stores.h:48](../../Source/stores.h#L48) e [stores.cpp:2289](../../Source/stores.cpp#L2289).

Há três apresentações configuráveis: lista textual, lista com miniaturas e grade visual. Fonte: [options.cpp:975](../../Source/options.cpp#L975). A grade nativa usa 10×9 células, abre inventário ao lado e oferece abas Básicos/Premium no ferreiro, Diversos nos demais. Griswold pode reparar um ou todos; Adria mantém recarga textual; Pepin/Wirt não compram itens. Fonte: [visual_store.h:19](../../Source/qol/visual_store.h#L19), [visual_store.cpp:227](../../Source/qol/visual_store.cpp#L227), [423](../../Source/qol/visual_store.cpp#L423).

As confirmações Sim/Não pertencem às listas. A grade compra diretamente e alguns bloqueios de dinheiro/espaço apenas retornam, com mensagens comentadas no código. Feedback claro na proposta é melhoria, não comportamento já validado. Fonte: [stores.cpp:928](../../Source/stores.cpp#L928), [visual_store.cpp:629](../../Source/qol/visual_store.cpp#L629).

## Contratos que os conceitos preservam

- Personagem: conservar todos os campos; mostrar Base/Agora; distribuir pontos somente no próprio herói e dentro dos limites da classe.
- Mochila 10×4, sete posições equipadas, cinto com oito itens; baú 10×10 e 100 páginas; loja visual 10×9. Não introduzir capacidade, equipamento ou estatística nova.
- O personagem guarda 12 atalhos de magia; F5–F8 são somente os quatro padrões. A ajuda antiga não deve limitar a especificação. Fontes: [player.h:44](../../Source/player.h#L44), [diablo.cpp:1947](../../Source/diablo.cpp#L1947).
- O mapa deriva de exploração e nível reais; não é mapa de mundo novo. Dados de sessão nos conceitos são fictícios.
- Os botões legados “voice/mute” do chat controlam destinatários de texto; não comprovam integração de voz.
- Morte tem ação contextual, não três escolhas sempre disponíveis.
- Os botões visíveis de confirmar/cancelar, navegação e fechar nos conceitos podem reformular a apresentação de ações por teclado; o desenho não deve ser confundido com botões já existentes.
- Lado esquerdo: personagem, quests, baú ou loja visual. Lado direito: inventário ou grimório. Preservar exclusões e foco de entrada. Fontes: [control_panel.cpp:64](../../Source/control/control_panel.cpp#L64), [scrollrt.cpp:1436](../../Source/engine/render/scrollrt.cpp#L1436).

## Limitações e revisão

A interface atual possui painel inferior lógico de 640×128 e painéis laterais de 320×352; reposiciona componentes em telas largas. Isso não comprova escala independente HD. Fonte: [control_panel.cpp:241](../../Source/control/control_panel.cpp#L241). Arte HD, tamanhos de texto, hitboxes, foco por teclado/controle, clipping, reorganização responsiva e interação com picking precisam de uma implementação e validação próprias após a revisão do estudo.

A evidência desta entrega é auditoria estática e consistência do JSON: IDs únicos, campos do schema e referências a arquivos existentes. Não é teste do jogo nem aprovação artística. Próxima ação deste escopo: revisar os conceitos com o usuário e registrar quais famílias/variantes precisam de ajuste antes de implementar.
