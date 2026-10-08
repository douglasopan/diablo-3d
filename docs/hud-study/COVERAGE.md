# Cobertura do catálogo de estudo

Índice completo dos exemplos da [galeria estática](index.html), derivado de [frontend.json](data/frontend.json), [options.json](data/options.json), [ingame.json](data/ingame.json) e [ingame-variants.json](data/ingame-variants.json). A contagem representa frames de estudo e estados de apresentação; não corresponde a 223 menus distintos do jogo. Inclui telas, painéis, formulários, diálogos, transições e variantes de uma mesma interface.

## Contagem confirmada

| Catálogo | Entradas-base | Variantes objeto com frame | Frames | Estados textuais sem frame |
| --- | ---: | ---: | ---: | ---: |
| [frontend.json](data/frontend.json) | 20 | 48 | 68 | 0 |
| [options.json](data/options.json) | 56 | 0 | 56 | 8 |
| [ingame.json](data/ingame.json) | 66 | 0 | 66 | 198 |
| [ingame-variants.json](data/ingame-variants.json) | 33 | 0 | 33 | 0 |
| **Total** | **175** | **48** | **223** | **206** |

As 142 entradas-base iniciais são 20 de front-end, 56 de opções/partida e 66 de interfaces durante o jogo. Somam-se 33 variantes auditadas, já registradas como entradas-base V01–V33 em ingame-variants.json. O front-end contém 49 objetos de variante: 48 produzem exemplos individuais; “Nenhum herói salvo” redireciona diretamente para [F05 — escolha de classe](index.html#F05) e não constitui uma tela. Portanto: 142 + 33 + 48 = 223 frames.

Os frames se distribuem em 13 famílias de navegação: 165 com disponibilidade atual, 56 condicionais e 2 de debug. “Atual” significa que a interface existe no código auditado; não garante que toda configuração, conteúdo ou estado esteja ativo na compilação em execução. Requisitos específicos permanecem nos catálogos e na evidência lateral da galeria.

## Regra de expansão

A regra acompanha [study.js](study.js#L78): cada entrada-base gera um frame. Cada objeto de variants gera outro com ID original-Vn, em que n é a posição original da variante, começando em 1; o título é v.title, sem acrescentar o título da entrada-base. Campos ausentes, incluindo família, disponibilidade e fontes, são herdados da base. A exceção “Nenhum herói salvo” é somente o redirecionamento F04 → F05; o ID F04-V3 não é emitido e a numeração seguinte conserva sua posição original. Variantes escritas como strings são estados documentados e não geram IDs ou frames adicionais.

O índice mantém a ordem dos quatro catálogos e da expansão. Os links na coluna Tela abrem o exemplo individual da galeria; as fontes apontam para os arquivos auditados, relativos a esta pasta, com a linha de referência.

## Famílias de navegação

| Família | Entradas-base | Variantes objeto com frame | Frames |
| --- | ---: | ---: | ---: |
| Entrada | 3 | 4 | 7 |
| Heróis | 4 | 8 | 12 |
| Partidas | 2 | 6 | 8 |
| Rede | 4 | 9 | 13 |
| Informações | 2 | 0 | 2 |
| Diálogos | 4 | 10 | 14 |
| Transições | 1 | 11 | 12 |
| Configurações | 34 | 0 | 34 |
| Partida | 22 | 0 | 22 |
| Painéis | 30 | 0 | 30 |
| Controles | 4 | 0 | 4 |
| NPCs | 27 | 0 | 27 |
| Comércio | 38 | 0 | 38 |
| **Total** | **175** | **48** | **223** |

## Índice completo de exemplos

| ID | Família | Tela | Disponibilidade | Fontes |
| --- | --- | --- | --- | --- |
| F01 | Entrada | [Abertura — título](index.html#F01) | Atual | [Source/DiabloUI/title.cpp:40](../../Source/DiabloUI/title.cpp#L40) · [Source/DiabloUI/title.cpp:67](../../Source/DiabloUI/title.cpp#L67) · [Source/diablo.cpp:1423](../../Source/diablo.cpp#L1423) |
| F01-V1 | Entrada | [Abertura herdada de Hellfire](index.html#F01-V1) | Atual | [Source/DiabloUI/title.cpp:46](../../Source/DiabloUI/title.cpp#L46) |
| F02 | Entrada | [Escolher Diablo ou Hellfire](index.html#F02) | Condicional | [Source/DiabloUI/selstart.cpp:41](../../Source/DiabloUI/selstart.cpp#L41) · [Source/DiabloUI/selstart.cpp:49](../../Source/DiabloUI/selstart.cpp#L49) · [Source/diablo.cpp:1350](../../Source/diablo.cpp#L1350) · [Source/diablo.cpp:1378](../../Source/diablo.cpp#L1378) |
| F03 | Entrada | [Menu principal — Diablo](index.html#F03) | Atual | [Source/DiabloUI/mainmenu.cpp:51](../../Source/DiabloUI/mainmenu.cpp#L51) · [Source/DiabloUI/diabloui.cpp:657](../../Source/DiabloUI/diabloui.cpp#L657) · [Source/menu.cpp:175](../../Source/menu.cpp#L175) |
| F03-V1 | Entrada | [Menu principal — Hellfire](index.html#F03-V1) | Atual | [Source/DiabloUI/mainmenu.cpp:51](../../Source/DiabloUI/mainmenu.cpp#L51) · [Source/DiabloUI/diabloui.cpp:657](../../Source/DiabloUI/diabloui.cpp#L657) · [Source/menu.cpp:175](../../Source/menu.cpp#L175) |
| F03-V2 | Entrada | [Menu principal — shareware](index.html#F03-V2) | Atual | [Source/DiabloUI/mainmenu.cpp:51](../../Source/DiabloUI/mainmenu.cpp#L51) · [Source/DiabloUI/diabloui.cpp:657](../../Source/DiabloUI/diabloui.cpp#L657) · [Source/menu.cpp:175](../../Source/menu.cpp#L175) |
| F03-V3 | Entrada | [Menu principal — plataforma sem saída](index.html#F03-V3) | Condicional | [Source/DiabloUI/mainmenu.cpp:51](../../Source/DiabloUI/mainmenu.cpp#L51) · [Source/DiabloUI/diabloui.cpp:657](../../Source/DiabloUI/diabloui.cpp#L657) · [Source/menu.cpp:175](../../Source/menu.cpp#L175) |
| F04 | Heróis | [Personagens de um jogador](index.html#F04) | Atual | [Source/DiabloUI/hero/selhero.cpp:139](../../Source/DiabloUI/hero/selhero.cpp#L139) · [Source/DiabloUI/hero/selhero.cpp:484](../../Source/DiabloUI/hero/selhero.cpp#L484) · [Source/DiabloUI/hero/selhero.cpp:530](../../Source/DiabloUI/hero/selhero.cpp#L530) |
| F04-V1 | Heróis | [Personagens multijogador](index.html#F04-V1) | Atual | [Source/DiabloUI/hero/selhero.cpp:565](../../Source/DiabloUI/hero/selhero.cpp#L565) |
| F04-V2 | Heróis | [Novo herói selecionado](index.html#F04-V2) | Atual | [Source/DiabloUI/hero/selhero.cpp:139](../../Source/DiabloUI/hero/selhero.cpp#L139) · [Source/DiabloUI/hero/selhero.cpp:484](../../Source/DiabloUI/hero/selhero.cpp#L484) · [Source/DiabloUI/hero/selhero.cpp:530](../../Source/DiabloUI/hero/selhero.cpp#L530) |
| F04-V4 | Heróis | [Personagens — diagnóstico](index.html#F04-V4) | Debug | [Source/DiabloUI/hero/selhero.cpp:139](../../Source/DiabloUI/hero/selhero.cpp#L139) · [Source/DiabloUI/hero/selhero.cpp:484](../../Source/DiabloUI/hero/selhero.cpp#L484) · [Source/DiabloUI/hero/selhero.cpp:530](../../Source/DiabloUI/hero/selhero.cpp#L530) |
| F05 | Heróis | [Novo herói — escolher classe](index.html#F05) | Atual | [Source/DiabloUI/hero/selhero.cpp:164](../../Source/DiabloUI/hero/selhero.cpp#L164) · [Source/DiabloUI/hero/selhero.cpp:179](../../Source/DiabloUI/hero/selhero.cpp#L179) · [Source/DiabloUI/hero/selhero.cpp:250](../../Source/DiabloUI/hero/selhero.cpp#L250) · [assets/txtdata/classes/warrior/attributes.tsv:3](../../assets/txtdata/classes/warrior/attributes.tsv#L3) |
| F05-V1 | Heróis | [Novo herói — Hellfire](index.html#F05-V1) | Condicional | [Source/DiabloUI/hero/selhero.cpp:179](../../Source/DiabloUI/hero/selhero.cpp#L179) |
| F05-V2 | Heróis | [Novo herói — classes opcionais](index.html#F05-V2) | Condicional | [Source/DiabloUI/hero/selhero.cpp:183](../../Source/DiabloUI/hero/selhero.cpp#L183) · [Source/DiabloUI/hero/selhero.cpp:194](../../Source/DiabloUI/hero/selhero.cpp#L194) |
| F05-V3 | Heróis | [Novo herói — classe indisponível no shareware](index.html#F05-V3) | Condicional | [Source/DiabloUI/hero/selhero.cpp:289](../../Source/DiabloUI/hero/selhero.cpp#L289) |
| F06 | Heróis | [Novo herói — nome](index.html#F06) | Atual | [Source/DiabloUI/hero/selhero.cpp:289](../../Source/DiabloUI/hero/selhero.cpp#L289) · [Source/DiabloUI/hero/selhero.cpp:311](../../Source/DiabloUI/hero/selhero.cpp#L311) |
| F06-V1 | Heróis | [Novo herói multijogador — nome](index.html#F06-V1) | Atual | [Source/DiabloUI/hero/selhero.cpp:335](../../Source/DiabloUI/hero/selhero.cpp#L335) |
| F06-V2 | Heróis | [Novo herói — nome sugerido](index.html#F06-V2) | Atual | [Source/DiabloUI/hero/selhero.cpp:267](../../Source/DiabloUI/hero/selhero.cpp#L267) · [Source/DiabloUI/hero/selhero.cpp:304](../../Source/DiabloUI/hero/selhero.cpp#L304) |
| F07 | Heróis | [Partida salva existente](index.html#F07) | Condicional | [Source/DiabloUI/hero/selhero.cpp:217](../../Source/DiabloUI/hero/selhero.cpp#L217) · [Source/DiabloUI/hero/selhero.cpp:363](../../Source/DiabloUI/hero/selhero.cpp#L363) |
| F08 | Partidas | [Criar partida — dificuldade](index.html#F08) | Atual | [Source/DiabloUI/multi/selgame.cpp:331](../../Source/DiabloUI/multi/selgame.cpp#L331) · [Source/DiabloUI/multi/selgame.cpp:396](../../Source/DiabloUI/multi/selgame.cpp#L396) · [Source/DiabloUI/multi/selgame.cpp:433](../../Source/DiabloUI/multi/selgame.cpp#L433) |
| F08-V1 | Partidas | [Dificuldade — multijogador](index.html#F08-V1) | Atual | [Source/DiabloUI/multi/selgame.cpp:415](../../Source/DiabloUI/multi/selgame.cpp#L415) |
| F08-V2 | Partidas | [Dificuldade — Inferno](index.html#F08-V2) | Atual | [Source/DiabloUI/multi/selgame.cpp:331](../../Source/DiabloUI/multi/selgame.cpp#L331) · [Source/DiabloUI/multi/selgame.cpp:396](../../Source/DiabloUI/multi/selgame.cpp#L396) · [Source/DiabloUI/multi/selgame.cpp:433](../../Source/DiabloUI/multi/selgame.cpp#L433) |
| F08-V3 | Partidas | [Dificuldade — pública ou privada](index.html#F08-V3) | Atual | [Source/DiabloUI/multi/selgame.cpp:331](../../Source/DiabloUI/multi/selgame.cpp#L331) · [Source/DiabloUI/multi/selgame.cpp:396](../../Source/DiabloUI/multi/selgame.cpp#L396) · [Source/DiabloUI/multi/selgame.cpp:433](../../Source/DiabloUI/multi/selgame.cpp#L433) |
| F09 | Rede | [Multijogador — conexão Offline](index.html#F09) | Atual | [Source/DiabloUI/multi/selconn.cpp:51](../../Source/DiabloUI/multi/selconn.cpp#L51) · [Source/DiabloUI/multi/selconn.cpp:64](../../Source/DiabloUI/multi/selconn.cpp#L64) · [Source/DiabloUI/multi/selconn.cpp:126](../../Source/DiabloUI/multi/selconn.cpp#L126) · [CMakeLists.txt:35](../../CMakeLists.txt#L35) |
| F09-V1 | Rede | [Multijogador — ZeroTier](index.html#F09-V1) | Condicional | [Source/DiabloUI/multi/selconn.cpp:55](../../Source/DiabloUI/multi/selconn.cpp#L55) · [Source/DiabloUI/multi/selconn.cpp:129](../../Source/DiabloUI/multi/selconn.cpp#L129) · [Source/multi.h:26](../../Source/multi.h#L26) |
| F09-V2 | Rede | [Multijogador — TCP](index.html#F09-V2) | Condicional | [Source/DiabloUI/multi/selconn.cpp:59](../../Source/DiabloUI/multi/selconn.cpp#L59) · [Source/DiabloUI/multi/selconn.cpp:126](../../Source/DiabloUI/multi/selconn.cpp#L126) · [Source/multi.h:26](../../Source/multi.h#L26) |
| F10 | Rede | [ZeroTier — ações e partidas públicas](index.html#F10) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F10-V1 | Rede | [TCP — selecionar ação](index.html#F10-V1) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F10-V2 | Rede | [ZeroTier — descobrindo partidas](index.html#F10-V2) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F10-V3 | Rede | [ZeroTier — nenhuma partida pública](index.html#F10-V3) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F10-V4 | Rede | [Ações — sem criptografia de pacotes](index.html#F10-V4) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F10-V5 | Rede | [Partida — conexão por retransmissão](index.html#F10-V5) | Condicional | [Source/DiabloUI/multi/selgame.cpp:111](../../Source/DiabloUI/multi/selgame.cpp#L111) · [Source/DiabloUI/multi/selgame.cpp:152](../../Source/DiabloUI/multi/selgame.cpp#L152) · [Source/DiabloUI/multi/selgame.cpp:214](../../Source/DiabloUI/multi/selgame.cpp#L214) · [Source/DiabloUI/multi/selgame.cpp:270](../../Source/DiabloUI/multi/selgame.cpp#L270) |
| F11 | Rede | [Entrar em partida — ID ZeroTier](index.html#F11) | Condicional | [Source/DiabloUI/multi/selgame.cpp:354](../../Source/DiabloUI/multi/selgame.cpp#L354) · [Source/DiabloUI/multi/selgame.cpp:369](../../Source/DiabloUI/multi/selgame.cpp#L369) |
| F11-V1 | Rede | [Entrar em partida — endereço TCP](index.html#F11-V1) | Condicional | [Source/DiabloUI/multi/selgame.cpp:354](../../Source/DiabloUI/multi/selgame.cpp#L354) · [Source/DiabloUI/multi/selgame.cpp:369](../../Source/DiabloUI/multi/selgame.cpp#L369) |
| F12 | Partidas | [Criar partida — velocidade](index.html#F12) | Atual | [Source/DiabloUI/multi/selgame.cpp:483](../../Source/DiabloUI/multi/selgame.cpp#L483) · [Source/DiabloUI/multi/selgame.cpp:506](../../Source/DiabloUI/multi/selgame.cpp#L506) · [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) |
| F12-V1 | Partidas | [Velocidade — Rápida](index.html#F12-V1) | Atual | [Source/DiabloUI/multi/selgame.cpp:483](../../Source/DiabloUI/multi/selgame.cpp#L483) · [Source/DiabloUI/multi/selgame.cpp:506](../../Source/DiabloUI/multi/selgame.cpp#L506) · [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) |
| F12-V2 | Partidas | [Velocidade — Mais rápida](index.html#F12-V2) | Atual | [Source/DiabloUI/multi/selgame.cpp:483](../../Source/DiabloUI/multi/selgame.cpp#L483) · [Source/DiabloUI/multi/selgame.cpp:506](../../Source/DiabloUI/multi/selgame.cpp#L506) · [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) |
| F12-V3 | Partidas | [Velocidade — Máxima](index.html#F12-V3) | Atual | [Source/DiabloUI/multi/selgame.cpp:483](../../Source/DiabloUI/multi/selgame.cpp#L483) · [Source/DiabloUI/multi/selgame.cpp:506](../../Source/DiabloUI/multi/selgame.cpp#L506) · [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) |
| F13 | Rede | [Criar partida privada — senha](index.html#F13) | Condicional | [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) · [Source/DiabloUI/multi/selgame.cpp:562](../../Source/DiabloUI/multi/selgame.cpp#L562) · [Source/DiabloUI/ui_item.h:272](../../Source/DiabloUI/ui_item.h#L272) |
| F13-V1 | Rede | [Entrar em partida — senha opcional](index.html#F13-V1) | Condicional | [Source/DiabloUI/multi/selgame.cpp:550](../../Source/DiabloUI/multi/selgame.cpp#L550) · [Source/DiabloUI/multi/selgame.cpp:562](../../Source/DiabloUI/multi/selgame.cpp#L562) · [Source/DiabloUI/ui_item.h:272](../../Source/DiabloUI/ui_item.h#L272) |
| F14 | Informações | [Créditos](index.html#F14) | Atual | [Source/DiabloUI/credits.cpp:151](../../Source/DiabloUI/credits.cpp#L151) · [Source/DiabloUI/credits.cpp:187](../../Source/DiabloUI/credits.cpp#L187) · [Source/DiabloUI/credits_lines.cpp:9](../../Source/DiabloUI/credits_lines.cpp#L9) · [Source/DiabloUI/credits_lines.cpp:214](../../Source/DiabloUI/credits_lines.cpp#L214) |
| F15 | Informações | [Suporte](index.html#F15) | Atual | [Source/DiabloUI/credits.cpp:195](../../Source/DiabloUI/credits.cpp#L195) · [Source/DiabloUI/support_lines.cpp:10](../../Source/DiabloUI/support_lines.cpp#L10) · [Source/DiabloUI/support_lines.cpp:16](../../Source/DiabloUI/support_lines.cpp#L16) · [Source/DiabloUI/support_lines.cpp:19](../../Source/DiabloUI/support_lines.cpp#L19) |
| D01 | Diálogos | [Confirmar exclusão de herói](index.html#D01) | Atual | [Source/DiabloUI/hero/selhero.cpp:613](../../Source/DiabloUI/hero/selhero.cpp#L613) · [Source/DiabloUI/selyesno.cpp:58](../../Source/DiabloUI/selyesno.cpp#L58) · [Source/DiabloUI/selyesno.cpp:74](../../Source/DiabloUI/selyesno.cpp#L74) |
| D01-V1 | Diálogos | [Confirmar exclusão de herói multijogador](index.html#D01-V1) | Atual | [Source/DiabloUI/hero/selhero.cpp:613](../../Source/DiabloUI/hero/selhero.cpp#L613) · [Source/DiabloUI/selyesno.cpp:58](../../Source/DiabloUI/selyesno.cpp#L58) · [Source/DiabloUI/selyesno.cpp:74](../../Source/DiabloUI/selyesno.cpp#L74) |
| D02 | Diálogos | [Aviso — dificuldade indisponível](index.html#D02) | Condicional | [Source/DiabloUI/selok.cpp:58](../../Source/DiabloUI/selok.cpp#L58) · [Source/DiabloUI/multi/selgame.cpp:424](../../Source/DiabloUI/multi/selgame.cpp#L424) |
| D02-V1 | Diálogos | [Aviso — Inferno indisponível](index.html#D02-V1) | Condicional | [Source/DiabloUI/multi/selgame.cpp:426](../../Source/DiabloUI/multi/selgame.cpp#L426) |
| D02-V2 | Diálogos | [Aviso — nome inválido](index.html#D02-V2) | Condicional | [Source/DiabloUI/hero/selhero.cpp:340](../../Source/DiabloUI/hero/selhero.cpp#L340) |
| D02-V3 | Diálogos | [Aviso — edição completa necessária](index.html#D02-V3) | Condicional | [Source/DiabloUI/hero/selhero.cpp:294](../../Source/DiabloUI/hero/selhero.cpp#L294) |
| D02-V4 | Diálogos | [Aviso — partida incompatível](index.html#D02-V4) | Condicional | [Source/DiabloUI/multi/selgame.cpp:100](../../Source/DiabloUI/multi/selgame.cpp#L100) · [Source/DiabloUI/multi/selgame.cpp:607](../../Source/DiabloUI/multi/selgame.cpp#L607) |
| D02-V5 | Diálogos | [Aviso — falha de rede](index.html#D02-V5) | Condicional | [Source/DiabloUI/multi/selgame.cpp:658](../../Source/DiabloUI/multi/selgame.cpp#L658) · [Source/DiabloUI/multi/selgame.cpp:684](../../Source/DiabloUI/multi/selgame.cpp#L684) |
| D02-V6 | Diálogos | [Aviso — assets incompatíveis com multijogador](index.html#D02-V6) | Condicional | [Source/menu.cpp:88](../../Source/menu.cpp#L88) |
| D03 | Diálogos | [Erro — criar personagem](index.html#D03) | Atual | [Source/DiabloUI/dialogs.cpp:77](../../Source/DiabloUI/dialogs.cpp#L77) · [Source/DiabloUI/dialogs.cpp:164](../../Source/DiabloUI/dialogs.cpp#L164) · [Source/DiabloUI/hero/selhero.cpp:347](../../Source/DiabloUI/hero/selhero.cpp#L347) |
| D03-V1 | Diálogos | [Informação — partida encerrada](index.html#D03-V1) | Atual | [Source/msg.cpp:2749](../../Source/msg.cpp#L2749) |
| D03-V2 | Diálogos | [Erro — dados do nível](index.html#D03-V2) | Atual | [Source/msg.cpp:2755](../../Source/msg.cpp#L2755) |
| D03-V3 | Diálogos | [Popup com título](index.html#D03-V3) | Atual | [Source/DiabloUI/dialogs.cpp:77](../../Source/DiabloUI/dialogs.cpp#L77) · [Source/DiabloUI/dialogs.cpp:164](../../Source/DiabloUI/dialogs.cpp#L164) · [Source/DiabloUI/hero/selhero.cpp:347](../../Source/DiabloUI/hero/selhero.cpp#L347) |
| D04 | Diálogos | [Sincronização — progresso cancelável](index.html#D04) | Condicional | [Source/DiabloUI/progress.cpp:54](../../Source/DiabloUI/progress.cpp#L54) · [Source/DiabloUI/progress.cpp:82](../../Source/DiabloUI/progress.cpp#L82) · [Source/DiabloUI/progress.cpp:106](../../Source/DiabloUI/progress.cpp#L106) · [Source/msg.cpp:2741](../../Source/msg.cpp#L2741) |
| L01 | Transições | [Carregamento de partida ou nível](index.html#L01) | Atual | [Source/interfac.cpp:59](../../Source/interfac.cpp#L59) · [Source/interfac.cpp:99](../../Source/interfac.cpp#L99) · [Source/interfac.cpp:137](../../Source/interfac.cpp#L137) · [Source/interfac.cpp:241](../../Source/interfac.cpp#L241) |
| L01-V1 | Transições | [Carregamento — início](index.html#L01-V1) | Atual | [Source/interfac.cpp:102](../../Source/interfac.cpp#L102) · [Source/interfac.cpp:143](../../Source/interfac.cpp#L143) |
| L01-V2 | Transições | [Carregamento — cidade](index.html#L01-V2) | Atual | [Source/interfac.cpp:149](../../Source/interfac.cpp#L149) |
| L01-V3 | Transições | [Carregamento — Catedral](index.html#L01-V3) | Atual | [Source/interfac.cpp:155](../../Source/interfac.cpp#L155) |
| L01-V4 | Transições | [Carregamento — Catacumbas](index.html#L01-V4) | Atual | [Source/interfac.cpp:161](../../Source/interfac.cpp#L161) |
| L01-V5 | Transições | [Carregamento — Cavernas](index.html#L01-V5) | Atual | [Source/interfac.cpp:167](../../Source/interfac.cpp#L167) |
| L01-V6 | Transições | [Carregamento — Inferno](index.html#L01-V6) | Atual | [Source/interfac.cpp:173](../../Source/interfac.cpp#L173) |
| L01-V7 | Transições | [Carregamento — Cripta](index.html#L01-V7) | Condicional | [Source/interfac.cpp:179](../../Source/interfac.cpp#L179) |
| L01-V8 | Transições | [Carregamento — Ninho](index.html#L01-V8) | Condicional | [Source/interfac.cpp:185](../../Source/interfac.cpp#L185) |
| L01-V9 | Transições | [Carregamento — portal azul](index.html#L01-V9) | Atual | [Source/interfac.cpp:191](../../Source/interfac.cpp#L191) |
| L01-V10 | Transições | [Carregamento — portal vermelho](index.html#L01-V10) | Atual | [Source/interfac.cpp:197](../../Source/interfac.cpp#L197) |
| L01-V11 | Transições | [Carregamento — portão](index.html#L01-V11) | Atual | [Source/interfac.cpp:203](../../Source/interfac.cpp#L203) |
| O01 | Configurações | [Configurações](index.html#O01) | Atual | [Source/DiabloUI/settingsmenu.cpp:407](../../Source/DiabloUI/settingsmenu.cpp#L407) |
| O02 | Configurações | [Idioma](index.html#O02) | Atual | [Source/options.cpp:1185](../../Source/options.cpp#L1185) |
| O03 | Configurações | [Escolher idioma](index.html#O03) | Atual | [Source/options.cpp:1119](../../Source/options.cpp#L1119) |
| O04 | Configurações | [Mods](index.html#O04) | Condicional | [Source/options.cpp:1686](../../Source/options.cpp#L1686) |
| O05 | Configurações | [Modo de jogo](index.html#O05) | Condicional | [Source/options.cpp:471](../../Source/options.cpp#L471) |
| O06 | Configurações | [Inicialização](index.html#O06) | Atual | [Source/options.cpp:491](../../Source/options.cpp#L491) |
| O07 | Configurações | [Tela de abertura](index.html#O07) | Atual | [Source/options.cpp:505](../../Source/options.cpp#L505) |
| O08 | Configurações | [Gráficos](index.html#O08) | Atual | [Source/options.cpp:830](../../Source/options.cpp#L830) |
| O09 | Configurações | [Resolução interna — exemplo](index.html#O09) | Condicional | [Source/options.cpp:638](../../Source/options.cpp#L638) |
| O10 | Configurações | [Qualidade de ampliação](index.html#O10) | Condicional | [Source/options.cpp:856](../../Source/options.cpp#L856) |
| O11 | Configurações | [Controle da taxa de quadros](index.html#O11) | Condicional | [Source/options.cpp:863](../../Source/options.cpp#L863) |
| O12 | Configurações | [Tamanho máximo do cursor de hardware](index.html#O12) | Condicional | [Source/options.cpp:896](../../Source/options.cpp#L896) |
| O13 | Configurações | [Áudio](index.html#O13) | Atual | [Source/options.cpp:550](../../Source/options.cpp#L550) |
| O14 | Configurações | [Taxa de amostragem](index.html#O14) | Atual | [Source/options.cpp:558](../../Source/options.cpp#L558) |
| O15 | Configurações | [Buffer de áudio](index.html#O15) | Atual | [Source/options.cpp:560](../../Source/options.cpp#L560) |
| O16 | Configurações | [Qualidade do resampler](index.html#O16) | Condicional | [Source/options.cpp:561](../../Source/options.cpp#L561) |
| O17 | Configurações | [Dispositivo de áudio — exemplo](index.html#O17) | Condicional | [Source/options.cpp:733](../../Source/options.cpp#L733) |
| O18 | Configurações | [Trilha sonora](index.html#O18) | Atual | [Source/options.cpp:586](../../Source/options.cpp#L586) |
| O19 | Configurações | [Modo da trilha sonora](index.html#O19) | Atual | [Source/options.cpp:588](../../Source/options.cpp#L588) |
| O20 | Configurações | [Música do menu principal](index.html#O20) | Atual | [Source/options.cpp:373](../../Source/options.cpp#L373) |
| O21 | Configurações | [Gameplay — Diablo](index.html#O21) | Atual | [Source/options.cpp:985](../../Source/options.cpp#L985) |
| O22 | Configurações | [Gameplay — Hellfire](index.html#O22) | Condicional | [Source/options.cpp:942](../../Source/options.cpp#L942) · [Source/options.cpp:985](../../Source/options.cpp#L985) |
| O23 | Configurações | [Interface de lojas](index.html#O23) | Atual | [Source/options.cpp:975](../../Source/options.cpp#L975) |
| O24 | Configurações | [Coleta automática — poções de vida](index.html#O24) | Atual | [Source/options.cpp:969](../../Source/options.cpp#L969) |
| O25 | Configurações | [Coleta automática — poções de vida cheia](index.html#O25) | Atual | [Source/options.cpp:970](../../Source/options.cpp#L970) |
| O26 | Configurações | [Coleta automática — poções de mana](index.html#O26) | Atual | [Source/options.cpp:971](../../Source/options.cpp#L971) |
| O27 | Configurações | [Coleta automática — poções de mana cheia](index.html#O27) | Atual | [Source/options.cpp:972](../../Source/options.cpp#L972) |
| O28 | Configurações | [Coleta automática — poções de rejuvenescimento](index.html#O28) | Atual | [Source/options.cpp:973](../../Source/options.cpp#L973) |
| O29 | Configurações | [Coleta automática — poções de rejuvenescimento cheio](index.html#O29) | Atual | [Source/options.cpp:974](../../Source/options.cpp#L974) |
| O30 | Configurações | [Mapeamento de teclas](index.html#O30) | Atual | [Source/options.cpp:1196](../../Source/options.cpp#L1196) · [Source/diablo.cpp:1928](../../Source/diablo.cpp#L1928) · [Source/player.h:44](../../Source/player.h#L44) · [Source/quick_messages.hpp:14](../../Source/quick_messages.hpp#L14) |
| O31 | Configurações | [Mapeamento do controle](index.html#O31) | Atual | [Source/options.cpp:1416](../../Source/options.cpp#L1416) · [Source/diablo.cpp:2301](../../Source/diablo.cpp#L2301) |
| O32 | Configurações | [Alterar tecla — inventário](index.html#O32) | Atual | [Source/DiabloUI/settingsmenu.cpp:446](../../Source/DiabloUI/settingsmenu.cpp#L446) |
| O33 | Configurações | [Alterar controle — inventário](index.html#O33) | Atual | [Source/DiabloUI/settingsmenu.cpp:501](../../Source/DiabloUI/settingsmenu.cpp#L501) |
| O34 | Configurações | [Captura do controle em andamento](index.html#O34) | Atual | [Source/DiabloUI/settingsmenu.cpp:501](../../Source/DiabloUI/settingsmenu.cpp#L501) |
| G01 | Partida | [Menu da partida — solo](index.html#G01) | Atual | [Source/gamemenu.cpp:73](../../Source/gamemenu.cpp#L73) |
| G02 | Partida | [Menu da partida — multiplayer](index.html#G02) | Condicional | [Source/gamemenu.cpp:85](../../Source/gamemenu.cpp#L85) |
| G03 | Partida | [Opções da partida — solo](index.html#G03) | Atual | [Source/gamemenu.cpp:94](../../Source/gamemenu.cpp#L94) |
| G04 | Partida | [Opções da partida — multiplayer](index.html#G04) | Condicional | [Source/gamemenu.cpp:239](../../Source/gamemenu.cpp#L239) |
| G05 | Partida | [Áudio da partida](index.html#G05) | Atual | [Source/gamemenu.cpp:105](../../Source/gamemenu.cpp#L105) |
| G06 | Partida | [Vídeo da partida](index.html#G06) | Atual | [Source/gamemenu.cpp:139](../../Source/gamemenu.cpp#L139) |
| G07 | Partida | [Trilha sonora na partida](index.html#G07) | Atual | [Source/gamemenu.cpp:114](../../Source/gamemenu.cpp#L114) |
| G08 | Partida | [Música por local — página 1 de 3](index.html#G08) | Atual | [Source/gamemenu.cpp:316](../../Source/gamemenu.cpp#L316) |
| G09 | Partida | [Música por local — página 2 de 3](index.html#G09) | Atual | [Source/gamemenu.cpp:316](../../Source/gamemenu.cpp#L316) |
| G10 | Partida | [Música por local — página 3 de 3](index.html#G10) | Atual | [Source/gamemenu.cpp:316](../../Source/gamemenu.cpp#L316) |
| G11 | Partida | [Variante musical — Menu principal](index.html#G11) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G12 | Partida | [Variante musical — Tristram](index.html#G12) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G13 | Partida | [Variante musical — Catedral](index.html#G13) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G14 | Partida | [Variante musical — Catacumbas](index.html#G14) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G15 | Partida | [Variante musical — Cavernas](index.html#G15) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G16 | Partida | [Variante musical — Inferno](index.html#G16) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G17 | Partida | [Variante musical — Ninho — Hellfire](index.html#G17) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G18 | Partida | [Variante musical — Cripta — Hellfire](index.html#G18) | Atual | [Source/gamemenu.cpp:348](../../Source/gamemenu.cpp#L348) |
| G19 | Partida | [Pausa](index.html#G19) | Atual | [Source/gmenu.cpp:191](../../Source/gmenu.cpp#L191) |
| G20 | Partida | [Salvando](index.html#G20) | Atual | [Source/gamemenu.cpp:609](../../Source/gamemenu.cpp#L609) |
| G21 | Partida | [Carregando](index.html#G21) | Atual | [Source/gamemenu.cpp:571](../../Source/gamemenu.cpp#L571) |
| G22 | Partida | [Jogo salvo](index.html#G22) | Atual | [Source/gamemenu.cpp:629](../../Source/gamemenu.cpp#L629) |
| P01 | Painéis | [Personagem](index.html#P01) | Atual | [Source/panels/charpanel.cpp:132](../../Source/panels/charpanel.cpp#L132) · [Source/control/control_panel.cpp:692](../../Source/control/control_panel.cpp#L692) |
| P02 | Painéis | [Inventário](index.html#P02) | Atual | [Source/inv.cpp:1187](../../Source/inv.cpp#L1187) · [Source/player.h:38](../../Source/player.h#L38) |
| P03 | Painéis | [Personagem e inventário abertos](index.html#P03) | Atual | [Source/engine/render/scrollrt.cpp:1436](../../Source/engine/render/scrollrt.cpp#L1436) · [Source/control/control_panel.cpp:64](../../Source/control/control_panel.cpp#L64) |
| P04 | Painéis | [Grimório](index.html#P04) | Atual | [Source/panels/spell_book.cpp:36](../../Source/panels/spell_book.cpp#L36) · [Source/panels/spell_book.cpp:135](../../Source/panels/spell_book.cpp#L135) |
| P05 | Painéis | [Lista rápida de magias](index.html#P05) | Atual | [Source/panels/spell_list.cpp:115](../../Source/panels/spell_list.cpp#L115) · [Source/diablo.cpp:1947](../../Source/diablo.cpp#L1947) |
| P06 | Painéis | [Diário de missões](index.html#P06) | Atual | [Source/panels/quest_log.cpp:84](../../Source/panels/quest_log.cpp#L84) · [Source/panels/quest_log.cpp:167](../../Source/panels/quest_log.cpp#L167) |
| P07 | Painéis | [Diálogo e relato narrado](index.html#P07) | Atual | [Source/minitext.cpp:94](../../Source/minitext.cpp#L94) · [Source/minitext.cpp:159](../../Source/minitext.cpp#L159) |
| P08 | Painéis | [Baú e inventário](index.html#P08) | Atual | [Source/qol/stash.cpp:42](../../Source/qol/stash.cpp#L42) · [Source/qol/stash.cpp:357](../../Source/qol/stash.cpp#L357) · [Source/stores.cpp:2022](../../Source/stores.cpp#L2022) |
| P09 | Painéis | [Sacar ouro do baú](index.html#P09) | Atual | [Source/qol/stash.cpp:590](../../Source/qol/stash.cpp#L590) · [Source/qol/stash.cpp:641](../../Source/qol/stash.cpp#L641) |
| P10 | Painéis | [Dividir pilha de ouro](index.html#P10) | Atual | [Source/control/control_gold.cpp:48](../../Source/control/control_gold.cpp#L48) |
| P11 | Painéis | [Automapa e minimapa](index.html#P11) | Atual | [Source/automap.h:86](../../Source/automap.h#L86) · [Source/automap.cpp:1427](../../Source/automap.cpp#L1427) · [Source/automap.cpp:1702](../../Source/automap.cpp#L1702) |
| P12 | Painéis | [Escrever mensagem](index.html#P12) | Condicional | [Source/control/control_chat.cpp:91](../../Source/control/control_chat.cpp#L91) |
| P13 | Painéis | [Histórico de mensagens](index.html#P13) | Atual | [Source/qol/chatlog.cpp:155](../../Source/qol/chatlog.cpp#L155) |
| P14 | Painéis | [Ajuda](index.html#P14) | Atual | [Source/help.cpp:191](../../Source/help.cpp#L191) |
| P15 | Painéis | [Detalhes de item único](index.html#P15) | Atual | [Source/items.cpp:1734](../../Source/items.cpp#L1734) · [Source/items.cpp:4097](../../Source/items.cpp#L4097) |
| P16 | Painéis | [Grupo e inspeção de aliado](index.html#P16) | Condicional | [Source/panels/partypanel.cpp:166](../../Source/panels/partypanel.cpp#L166) · [Source/panels/partypanel.cpp:105](../../Source/panels/partypanel.cpp#L105) |
| P17 | Painéis | [Morte](index.html#P17) | Atual | [Source/control/control_panel.cpp:789](../../Source/control/control_panel.cpp#L789) |
| P18 | Painéis | [Notificação da partida](index.html#P18) | Atual | [Source/diablo_msg.cpp:160](../../Source/diablo_msg.cpp#L160) |
| P19 | Painéis | [Mapa da Catedral](index.html#P19) | Condicional | [Source/doom.cpp:23](../../Source/doom.cpp#L23) · [Source/items.cpp:4313](../../Source/items.cpp#L4313) |
| P20 | Controles | [Auxiliar de menus no controle](index.html#P20) | Condicional | [Source/controls/modifier_hints.cpp:142](../../Source/controls/modifier_hints.cpp#L142) |
| P21 | Controles | [Auxiliar de magias no controle](index.html#P21) | Condicional | [Source/controls/modifier_hints.cpp:153](../../Source/controls/modifier_hints.cpp#L153) |
| P22 | Controles | [Controles de toque](index.html#P22) | Condicional | [Source/controls/touch/renderers.cpp:273](../../Source/controls/touch/renderers.cpp#L273) · [Source/control/control_panel.cpp:241](../../Source/control/control_panel.cpp#L241) |
| P23 | Controles | [Console de desenvolvimento](index.html#P23) | Debug | [Source/panels/console.cpp:1](../../Source/panels/console.cpp#L1) · [Source/panels/console.cpp:560](../../Source/panels/console.cpp#L560) |
| N01 | NPCs | [Griswold — Ferraria](index.html#N01) | Atual | [Source/stores.cpp:433](../../Source/stores.cpp#L433) |
| N09 | NPCs | [Conversar com Griswold](index.html#N09) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N02 | NPCs | [Adria — Cabana da bruxa](index.html#N02) | Atual | [Source/stores.cpp:691](../../Source/stores.cpp#L691) |
| N10 | NPCs | [Conversar com Adria](index.html#N10) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N03 | NPCs | [Pepin — Casa do curandeiro](index.html#N03) | Atual | [Source/stores.cpp:1031](../../Source/stores.cpp#L1031) |
| N11 | NPCs | [Conversar com Pepin](index.html#N11) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N04 | NPCs | [Cain — O ancião da cidade](index.html#N04) | Atual | [Source/stores.cpp:1073](../../Source/stores.cpp#L1073) |
| N12 | NPCs | [Conversar com Cain](index.html#N12) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N05 | NPCs | [Wirt — O rapaz da perna de pau](index.html#N05) | Atual | [Source/stores.cpp:972](../../Source/stores.cpp#L972) |
| N13 | NPCs | [Conversar com Wirt](index.html#N13) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N06 | NPCs | [Ogden — Taverna](index.html#N06) | Atual | [Source/stores.cpp:1248](../../Source/stores.cpp#L1248) |
| N14 | NPCs | [Conversar com Ogden](index.html#N14) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N07 | NPCs | [Gillian — Armazenamento](index.html#N07) | Atual | [Source/stores.cpp:1261](../../Source/stores.cpp#L1261) |
| N15 | NPCs | [Conversar com Gillian](index.html#N15) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| N08 | NPCs | [Farnham — Morador de Tristram](index.html#N08) | Atual | [Source/stores.cpp:1274](../../Source/stores.cpp#L1274) |
| N16 | NPCs | [Conversar com Farnham](index.html#N16) | Atual | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) |
| T01 | Comércio | [Griswold — comprar básicos](index.html#T01) | Atual | [Source/stores.cpp:465](../../Source/stores.cpp#L465) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T02 | Comércio | [Griswold — comprar premium](index.html#T02) | Atual | [Source/stores.cpp:497](../../Source/stores.cpp#L497) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T03 | Comércio | [Griswold — vender](index.html#T03) | Atual | [Source/stores.cpp:544](../../Source/stores.cpp#L544) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T04 | Comércio | [Griswold — reparar](index.html#T04) | Atual | [Source/stores.cpp:630](../../Source/stores.cpp#L630) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T05 | Comércio | [Adria — comprar](index.html#T05) | Atual | [Source/stores.cpp:735](../../Source/stores.cpp#L735) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T06 | Comércio | [Adria — vender](index.html#T06) | Atual | [Source/stores.cpp:769](../../Source/stores.cpp#L769) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T07 | Comércio | [Adria — recarregar cajados](index.html#T07) | Atual | [Source/stores.cpp:862](../../Source/stores.cpp#L862) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T08 | Comércio | [Pepin — comprar](index.html#T08) | Atual | [Source/stores.cpp:1051](../../Source/stores.cpp#L1051) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T09 | Comércio | [Cain — identificar](index.html#T09) | Atual | [Source/stores.cpp:1105](../../Source/stores.cpp#L1105) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T10 | Comércio | [Wirt — oferta de um item](index.html#T10) | Atual | [Source/stores.cpp:991](../../Source/stores.cpp#L991) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) |
| T11 | Comércio | [Griswold — loja em grade visual](index.html#T11) | Atual | [Source/qol/visual_store.h:19](../../Source/qol/visual_store.h#L19) · [Source/qol/visual_store.cpp:423](../../Source/qol/visual_store.cpp#L423) · [Source/qol/visual_store.cpp:629](../../Source/qol/visual_store.cpp#L629) |
| T12 | Comércio | [Adria — loja em grade visual](index.html#T12) | Atual | [Source/qol/visual_store.h:19](../../Source/qol/visual_store.h#L19) · [Source/qol/visual_store.cpp:423](../../Source/qol/visual_store.cpp#L423) · [Source/qol/visual_store.cpp:629](../../Source/qol/visual_store.cpp#L629) |
| T13 | Comércio | [Pepin — loja em grade visual](index.html#T13) | Atual | [Source/qol/visual_store.h:19](../../Source/qol/visual_store.h#L19) · [Source/qol/visual_store.cpp:423](../../Source/qol/visual_store.cpp#L423) · [Source/qol/visual_store.cpp:629](../../Source/qol/visual_store.cpp#L629) |
| T14 | Comércio | [Wirt — loja em grade visual](index.html#T14) | Atual | [Source/qol/visual_store.h:19](../../Source/qol/visual_store.h#L19) · [Source/qol/visual_store.cpp:423](../../Source/qol/visual_store.cpp#L423) · [Source/qol/visual_store.cpp:629](../../Source/qol/visual_store.cpp#L629) |
| T15 | Comércio | [Confirmação — Comprar item](index.html#T15) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T16 | Comércio | [Confirmação — Vender item](index.html#T16) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T17 | Comércio | [Confirmação — Reparar item](index.html#T17) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T18 | Comércio | [Confirmação — Recarregar cajado](index.html#T18) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T19 | Comércio | [Confirmação — Identificar item](index.html#T19) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T20 | Comércio | [Confirmação — Negócio de Wirt](index.html#T20) | Atual | [Source/stores.cpp:928](../../Source/stores.cpp#L928) |
| T21 | Comércio | [Compra bloqueada — ouro insuficiente](index.html#T21) | Atual | [Source/stores.cpp:910](../../Source/stores.cpp#L910) |
| T22 | Comércio | [Compra bloqueada — inventário cheio](index.html#T22) | Atual | [Source/stores.cpp:920](../../Source/stores.cpp#L920) |
| T23 | Comércio | [Nenhum item para vender](index.html#T23) | Atual | [Source/stores.cpp:594](../../Source/stores.cpp#L594) · [Source/stores.cpp:819](../../Source/stores.cpp#L819) |
| T24 | Comércio | [Nenhum item para reparar](index.html#T24) | Atual | [Source/stores.cpp:673](../../Source/stores.cpp#L673) |
| T25 | Comércio | [Nenhum cajado para recarregar](index.html#T25) | Atual | [Source/stores.cpp:893](../../Source/stores.cpp#L893) |
| T26 | Comércio | [Nenhum item para identificar](index.html#T26) | Atual | [Source/stores.cpp:1173](../../Source/stores.cpp#L1173) |
| T27 | Comércio | [Identificação concluída](index.html#T27) | Atual | [Source/stores.cpp:1191](../../Source/stores.cpp#L1191) |
| V01 | Comércio | [Griswold — comprar básicos — lista com miniaturas](index.html#V01) | Atual | [Source/stores.cpp:465](../../Source/stores.cpp#L465) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V02 | Comércio | [Griswold — comprar premium — lista com miniaturas](index.html#V02) | Atual | [Source/stores.cpp:497](../../Source/stores.cpp#L497) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V03 | Comércio | [Griswold — vender — lista com miniaturas](index.html#V03) | Atual | [Source/stores.cpp:544](../../Source/stores.cpp#L544) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V04 | Comércio | [Griswold — reparar — lista com miniaturas](index.html#V04) | Atual | [Source/stores.cpp:630](../../Source/stores.cpp#L630) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V05 | Comércio | [Adria — comprar — lista com miniaturas](index.html#V05) | Atual | [Source/stores.cpp:735](../../Source/stores.cpp#L735) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V06 | Comércio | [Adria — vender — lista com miniaturas](index.html#V06) | Atual | [Source/stores.cpp:769](../../Source/stores.cpp#L769) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V07 | Comércio | [Adria — recarregar cajados — lista com miniaturas](index.html#V07) | Atual | [Source/stores.cpp:862](../../Source/stores.cpp#L862) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V08 | Comércio | [Pepin — comprar — lista com miniaturas](index.html#V08) | Atual | [Source/stores.cpp:1051](../../Source/stores.cpp#L1051) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V09 | Comércio | [Cain — identificar — lista com miniaturas](index.html#V09) | Atual | [Source/stores.cpp:1105](../../Source/stores.cpp#L1105) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V10 | Comércio | [Wirt — oferta de um item — lista com miniaturas](index.html#V10) | Atual | [Source/stores.cpp:991](../../Source/stores.cpp#L991) · [Source/stores.cpp:355](../../Source/stores.cpp#L355) · [Source/stores.cpp:2218](../../Source/stores.cpp#L2218) · [Source/options.cpp:975](../../Source/options.cpp#L975) |
| V11 | Painéis | [Automapa opaco](index.html#V11) | Atual | [Source/automap.h:86](../../Source/automap.h#L86) · [Source/automap.cpp:1752](../../Source/automap.cpp#L1752) · [Source/automap.cpp:1427](../../Source/automap.cpp#L1427) |
| V12 | Painéis | [Automapa transparente](index.html#V12) | Atual | [Source/automap.h:86](../../Source/automap.h#L86) · [Source/automap.cpp:1752](../../Source/automap.cpp#L1752) · [Source/automap.cpp:1427](../../Source/automap.cpp#L1427) |
| V13 | Painéis | [Minimapa](index.html#V13) | Atual | [Source/automap.h:86](../../Source/automap.h#L86) · [Source/automap.cpp:1752](../../Source/automap.cpp#L1752) · [Source/automap.cpp:1427](../../Source/automap.cpp#L1427) |
| V14 | Painéis | [Inspecionar Mira](index.html#V14) | Condicional | [Source/panels/partypanel.cpp:105](../../Source/panels/partypanel.cpp#L105) · [Source/panels/charpanel.cpp:132](../../Source/panels/charpanel.cpp#L132) · [Source/inv.cpp:1210](../../Source/inv.cpp#L1210) |
| V15 | Painéis | [Você morreu — solo com save](index.html#V15) | Atual | [Source/control/control_panel.cpp:789](../../Source/control/control_panel.cpp#L789) · [Source/control/control_panel.cpp:823](../../Source/control/control_panel.cpp#L823) |
| V16 | Painéis | [Você morreu — solo sem save](index.html#V16) | Atual | [Source/control/control_panel.cpp:789](../../Source/control/control_panel.cpp#L789) · [Source/control/control_panel.cpp:823](../../Source/control/control_panel.cpp#L823) |
| V17 | Painéis | [Você morreu — multiplayer](index.html#V17) | Condicional | [Source/control/control_panel.cpp:789](../../Source/control/control_panel.cpp#L789) · [Source/control/control_panel.cpp:823](../../Source/control/control_panel.cpp#L823) |
| V18 | Painéis | [Grimório — página 2](index.html#V18) | Atual | [Source/panels/spell_book.cpp:45](../../Source/panels/spell_book.cpp#L45) · [Source/panels/spell_book.cpp:148](../../Source/panels/spell_book.cpp#L148) · [assets/txtdata/spells/spelldat.tsv:2](../../assets/txtdata/spells/spelldat.tsv#L2) |
| V19 | Painéis | [Grimório — página 3](index.html#V19) | Atual | [Source/panels/spell_book.cpp:45](../../Source/panels/spell_book.cpp#L45) · [Source/panels/spell_book.cpp:148](../../Source/panels/spell_book.cpp#L148) · [assets/txtdata/spells/spelldat.tsv:2](../../assets/txtdata/spells/spelldat.tsv#L2) |
| V20 | Painéis | [Grimório — página 4](index.html#V20) | Atual | [Source/panels/spell_book.cpp:45](../../Source/panels/spell_book.cpp#L45) · [Source/panels/spell_book.cpp:148](../../Source/panels/spell_book.cpp#L148) · [assets/txtdata/spells/spelldat.tsv:2](../../assets/txtdata/spells/spelldat.tsv#L2) |
| V21 | Painéis | [Grimório — página 5 · Hellfire](index.html#V21) | Condicional | [Source/panels/spell_book.cpp:45](../../Source/panels/spell_book.cpp#L45) · [Source/panels/spell_book.cpp:148](../../Source/panels/spell_book.cpp#L148) · [mods/hf/txtdata/spells/spelldat.tsv:41](../../mods/hf/txtdata/spells/spelldat.tsv#L41) |
| V22 | NPCs | [Griswold — grade visual selecionada](index.html#V22) | Atual | [Source/stores.cpp:433](../../Source/stores.cpp#L433) · [Source/stores.cpp:1286](../../Source/stores.cpp#L1286) |
| V23 | NPCs | [Adria — grade visual selecionada](index.html#V23) | Atual | [Source/stores.cpp:691](../../Source/stores.cpp#L691) · [Source/stores.cpp:1532](../../Source/stores.cpp#L1532) |
| V24 | NPCs | [Wirt — sem oferta](index.html#V24) | Atual | [Source/stores.cpp:972](../../Source/stores.cpp#L972) |
| V25 | NPCs | [Conversar com Griswold — shareware](index.html#V25) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V26 | NPCs | [Conversar com Adria — shareware](index.html#V26) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V27 | NPCs | [Conversar com Pepin — shareware](index.html#V27) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V28 | NPCs | [Conversar com Cain — shareware](index.html#V28) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V29 | NPCs | [Conversar com Wirt — shareware](index.html#V29) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V30 | NPCs | [Conversar com Ogden — shareware](index.html#V30) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V31 | NPCs | [Conversar com Gillian — shareware](index.html#V31) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V32 | NPCs | [Conversar com Farnham — shareware](index.html#V32) | Condicional | [Source/stores.cpp:1205](../../Source/stores.cpp#L1205) · [Source/stores.cpp:1212](../../Source/stores.cpp#L1212) |
| V33 | Comércio | [Adria — nenhum item para vender](index.html#V33) | Atual | [Source/stores.cpp:769](../../Source/stores.cpp#L769) · [Source/stores.cpp:819](../../Source/stores.cpp#L819) |

## Estados documentados sem frame adicional

As 206 strings de variants abaixo complementam as entradas-base. Permanecem descrições de estados, condições e diferenças de comportamento; esta seção não as transforma em telas independentes nem promete exemplos individuais além dos 223 frames indexados acima.

| Entrada-base | Família | Estados documentados |
| --- | --- | --- |
| [O08](index.html#O08) | Configurações | SDL1 pode omitir filtros, escala inteira e cursores de hardware.<br>Tela cheia é omitida em plataformas móveis sem suporte a janela e no menu Vita.<br>Controle de frames fica invisível em NXDK/Android; cursor depende de suporte. |
| [O30](index.html#O30) | Configurações | Em build _DEBUG também aparecem Console e Alternar debug; omitidos da experiência normal. |
| [O33](index.html#O33) | Configurações | Repouso: combinação clicável, Desvincular e Voltar visíveis.<br>Captura ativa: mensagem regressiva, ações de navegação ocultas temporariamente. |
| [G17](index.html#G17) | Partida | Ambiente de Hellfire; o submenu consta na lista atual. |
| [G18](index.html#G18) | Partida | Ambiente de Hellfire; o submenu consta na lista atual. |
| [P01](index.html#P01) | Painéis | Sem pontos disponíveis<br>Atributo no limite da classe<br>Nível máximo: próximo nível sem valor<br>Inspeção de outro jogador: somente leitura, sem botões + |
| [P02](index.html#P02) | Painéis | Fundo por classe<br>Item não identificado<br>Requisitos não atendidos<br>Item na mão do cursor<br>Inspeção de aliado em leitura |
| [P03](index.html#P03) | Painéis | Desktop 16:9<br>Ultrawide com centro livre<br>Viewport menor<br>Inspeção de aliado em leitura |
| [P04](index.html#P04) | Painéis | Quatro páginas Diablo<br>Quinta página condicional Hellfire<br>Magia indisponível em Tristram<br>Cajado com cargas<br>Inspeção de aliado em leitura |
| [P05](index.html#P05) | Painéis | Somente habilidade inicial<br>Múltiplas origens para uma magia<br>Atalhos extras configurados<br>Magia inválida em Tristram |
| [P06](index.html#P06) | Painéis | Lista vazia<br>Somente missões ativas<br>Ativas e concluídas<br>Conteúdo Hellfire condicionado ao estado |
| [P07](index.html#P07) | Painéis | Fala de NPC<br>Livro ou nota<br>Relato de missão<br>NPCs condicionais Hellfire |
| [P08](index.html#P08) | Painéis | Página vazia<br>Página cheia<br>Primeira ou última página<br>Item grande sendo transferido |
| [P09](index.html#P09) | Painéis | Quantidade zero<br>Saldo insuficiente<br>Inventário sem espaço para ouro |
| [P10](index.html#P10) | Painéis | Uma moeda<br>Pilha cheia<br>Quantidade inválida |
| [P11](index.html#P11) | Painéis | Automapa opaco<br>Automapa transparente<br>Minimapa<br>Nível de masmorra ou missão<br>Nest/Crypt condicionais Hellfire<br>Metadados multiplayer fictícios |
| [P12](index.html#P12) | Painéis | Todos destinatários<br>Destinatário individual<br>Entrada recuperada do histórico |
| [P13](index.html#P13) | Painéis | Sem mensagens<br>Mensagens novas durante rolagem<br>Mensagem longa com quebra |
| [P14](index.html#P14) | Painéis | Diablo<br>Hellfire<br>Shareware<br>Texto longo com rolagem |
| [P15](index.html#P15) | Painéis | Item do inventário<br>Item do baú<br>Painéis dos dois lados abertos<br>Propriedades longas |
| [P16](index.html#P16) | Painéis | Aliado vivo<br>Aliado morto<br>Escudo de mana<br>Personagem e inventário do aliado abertos |
| [P17](index.html#P17) | Painéis | Solo com save: carregar último<br>Solo sem save: menu principal<br>Multiplayer: reiniciar na cidade<br>Ícone do botão conforme dispositivo |
| [P18](index.html#P18) | Painéis | Salvando ou carregando<br>Pausa não disponível<br>Recurso indisponível em shareware<br>Mensagem de santuário |
| [P19](index.html#P19) | Painéis | Abrir pelo inventário<br>Abrir pelo baú |
| [P20](index.html#P20) | Controles | Xbox<br>PlayStation<br>Controle genérico |
| [P21](index.html#P21) | Controles | Magia válida<br>Magia proibida em Tristram<br>Atalho vazio |
| [P22](index.html#P22) | Controles | Tristram<br>Masmorra: ficar parado disponível<br>Pontos de atributo disponíveis |
| [P23](index.html#P23) | Controles | Saída normal<br>Aviso<br>Erro<br>Autocomplete |
| [N01](index.html#N01) | NPCs | Comprar básicos e premium<br>Vender e reparar |
| [N09](index.html#N09) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N02](index.html#N02) | NPCs | Comprar e vender<br>Recarregar cajado |
| [N10](index.html#N10) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N03](index.html#N03) | NPCs | Comprar itens<br>Vida restaurada ao abrir |
| [N11](index.html#N11) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N04](index.html#N04) | NPCs | Item não identificado<br>Nenhum item elegível |
| [N12](index.html#N12) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N05](index.html#N05) | NPCs | Oferta disponível<br>Oferta esgotada<br>Ouro insuficiente para olhar |
| [N13](index.html#N13) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N06](index.html#N06) | NPCs | Conversar sobre missão<br>Rumores |
| [N14](index.html#N14) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N07](index.html#N07) | NPCs | Conversar<br>Abrir baú |
| [N15](index.html#N15) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [N08](index.html#N08) | NPCs | Rumores<br>Tópicos de missão |
| [N16](index.html#N16) | NPCs | Rumores sem quest ativa<br>Tópicos de quests ativas<br>Shareware: conversa indisponível |
| [T01](index.html#T01) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T02](index.html#T02) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T03](index.html#T03) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T04](index.html#T04) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T05](index.html#T05) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T06](index.html#T06) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T07](index.html#T07) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T08](index.html#T08) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T09](index.html#T09) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T10](index.html#T10) | Comércio | Lista textual<br>Lista com miniaturas<br>Item não utilizável<br>Preço não acessível |
| [T11](index.html#T11) | Comércio | Item realçado<br>Grade com estoque reduzido<br>Página adicional quando houver<br>Ouro insuficiente |
| [T12](index.html#T12) | Comércio | Item realçado<br>Grade com estoque reduzido<br>Página adicional quando houver<br>Ouro insuficiente |
| [T13](index.html#T13) | Comércio | Item realçado<br>Grade com estoque reduzido<br>Página adicional quando houver<br>Ouro insuficiente |
| [T14](index.html#T14) | Comércio | Item realçado<br>Grade com estoque reduzido<br>Página adicional quando houver<br>Ouro insuficiente |
| [T15](index.html#T15) | Comércio | Foco em Sim<br>Foco em Não |
| [T16](index.html#T16) | Comércio | Foco em Sim<br>Foco em Não |
| [T17](index.html#T17) | Comércio | Foco em Sim<br>Foco em Não |
| [T18](index.html#T18) | Comércio | Foco em Sim<br>Foco em Não |
| [T19](index.html#T19) | Comércio | Foco em Sim<br>Foco em Não |
| [T20](index.html#T20) | Comércio | Foco em Sim<br>Foco em Não |
| [T23](index.html#T23) | Comércio | Griswold<br>Adria |

O estado de ausência de heróis pertence à evidência de F04 e segue para F05; não aparece na lista de frames. Os exemplos são propostas visuais com conteúdo demonstrativo, não capturas do jogo ou implementação de novas mecânicas.
