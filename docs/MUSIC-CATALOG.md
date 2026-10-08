# Catálogo da trilha sonora

Este inventário identifica a música ambiente selecionada pelo código do Diablo 3D e os contextos que compartilham cada faixa. Serve como lista de produção das versões autorais, incluindo rock, sem apagar a escolha das músicas originais. Os controles, instalação e revisão dos arquivos locais estão em [MUSIC.md](MUSIC.md).

O jogo tem **oito IDs de música ambiente**: seis usados pelo Diablo, incluindo o menu, e dois adicionais do Hellfire. Um andar, chefe ou missão não representa necessariamente uma composição distinta. As faixas repetem durante a partida; o menu de pausa conserva a música do ambiente.

## Lista de composições

Esta é a **ordem de produção de 1 a 8**, usada para organizar os arquivos que serão refeitos. O número de produção é separado do ID nativo preservado pelo jogo. Por isso, os arquivos `06.wav`, `06 (2).wav` e `06 (1).mp3` pertencem a Tristram, a sexta entrada da lista, cujo ID técnico é 0. Os caminhos WAV identificam as músicas originais; o loader procura primeiro um MP3 de mesmo nome quando disponível.

| Ordem | ID nativo / constante | Local / forma de reprodução | Recurso original completo | Versões autorais fornecidas | Estado de produção |
| --- | --- | --- | --- | --- | --- |
| **1** | 7 — `TMUSIC_INTRO` | Menu principal, créditos, suporte, configurações e seleção de herói | `music/dintro.wav` | `Main_Menu_Rock2.mp3` e `Main_Menu.mp3` | Rock2 é o padrão local; primeira composição preservada como alternativa. |
| **2** | 1 — `TMUSIC_CATHEDRAL` | Catedral, andares 1–4; mapas especiais com tipo Catedral | `music/dlvla.wav` | Nenhuma | Refazer: tema da Catedral. |
| **3** | 2 — `TMUSIC_CATACOMBS` | Catacumbas, andares 5–8; Câmara dos Ossos | `music/dlvlb.wav` | Nenhuma | Refazer: tema das Catacumbas. |
| **4** | 3 — `TMUSIC_CAVES` | Cavernas, andares 9–12; Abastecimento de Água Envenenado | `music/dlvlc.wav` | Nenhuma | Refazer: tema das Cavernas. |
| **5** | 4 — `TMUSIC_HELL` | Inferno, andares 13–16, incluindo o andar de Diablo | `music/dlvld.wav` | Nenhuma | Refazer: tema do Inferno. |
| **6** | 0 — `TMUSIC_TOWN` | Tristram, nível 0, inclusive com Hellfire ativo | `music/dtowne.wav` | `06.wav` → Tristram 1; `06 (2).wav` → Tristram 2; `06 (1).mp3` → Tristram 3 | Três opções locais validadas no decoder; fontes preservadas. Seleção fixa ou aleatória ao fim de cada música. A terceira aparenta ser outra exportação da primeira gravação. |
| **7** | 5 — `TMUSIC_NEST` | Ninho / Hive, andares 17–20, conteúdo condicional Hellfire | `music/dlvlf.wav` | Nenhuma | Refazer se incluído o escopo Hellfire. |
| **8** | 6 — `TMUSIC_CRYPT` | Cripta, andares 21–24, conteúdo condicional Hellfire | `music/dlvle.wav` | Nenhuma | Refazer se incluído o escopo Hellfire. |

Foram fornecidos cinco arquivos: duas opções para o menu e três para Tristram. Isso não implica cinco gravações distintas: a comparação local sugere que Tristram 3 é uma exportação MP3 da primeira gravação, mantida como opção separada conforme solicitado. Faltam **quatro temas dos ambientes do Diablo** — Catedral, Catacumbas, Cavernas e Inferno — e, separadamente, **dois do Hellfire** — Ninho e Cripta. O estado da instalação, hashes e testes pertence ao contrato [MUSIC.md](MUSIC.md), evitando confundir arquivo recebido, decoder validado e revisão artística.

No modo global Rock, Tristram usa um sorteio uniforme entre os arquivos disponíveis. Um novo sorteio independente acontece ao fim de cada faixa e pode repetir a mesma opção. O modo Custom permite fixar Tristram 1, 2 ou 3, escolher Random ou manter a original; Vanilla conserva a trilha original. A seleção aleatória não consome o RNG da simulação. Os WAVs originais recebidos continuam preservados e os MP3s convertidos ficam nos caminhos `music/d3d/town-rock.mp3` e `music/d3d/town-alternative.mp3`; `music/d3d/town-third.mp3` é uma cópia exata do MP3 recebido, sem recompressão.

Fontes: enumeração em [`sound.h`](../Source/engine/sound.h), caminhos originais e substituições em [`music_catalog.hpp`](../Source/engine/music_catalog.hpp), relação entre tipo de ambiente e faixa em `GetLevelMusic` de [`sound.cpp`](../Source/engine/sound.cpp) e andares em `GetLevelType` de [`gendung.cpp`](../Source/levels/gendung.cpp). A carga do nível decide a faixa pelo seu tipo em [`diablo.cpp`](../Source/diablo.cpp).

## Contextos compartilhados e exceções

| Contexto | Música / comportamento identificado | Consequência para a produção |
| --- | --- | --- |
| Menu original | A rotina herdada alterna Intro → Catacumbas → Cavernas → Inferno → Intro; com Hellfire, inclui Ninho e Cripta antes de voltar à Intro. Shareware usa Intro. | “Vanilla” deve respeitar o comportamento original, sem exigir cópias dos arquivos proprietários no projeto. A preferência por uma música autoral do menu deve identificá-la explicitamente. |
| Créditos e suporte | As telas não iniciam uma composição própria; continuam a faixa que já toca no menu. | Não adicionar “música de créditos” como arquivo obrigatório independente. |
| Menus de herói, jogo e configurações | Mantêm a faixa do fluxo do menu; entrar numa partida inicia a música do ambiente carregado. | São contextos do menu, não músicas adicionais encontradas no inventário. |
| Pausa, inventário, comércio e diálogos | Não existe troca de `_music_id` exclusiva identificada para esses contextos. | A música ambiente continua. Vozes, efeitos e falas pertencem a outro sistema. |
| Chefes | Não existe um ID ambiente exclusivo para Butcher, Leoric, Lazarus, Diablo, Defiler ou Na-Krul. | Novos temas de chefe exigiriam gatilhos e política de transição próprios; não são substituições já existentes. |
| Tumba de Leoric / Rei Esqueleto | Tipo Catedral, portanto `TMUSIC_CATHEDRAL`. | Compartilha a composição da Catedral. |
| Câmara dos Ossos | Tipo Catacumbas, portanto `TMUSIC_CATACOMBS`. | Compartilha a composição das Catacumbas. |
| Água Envenenada / A Dark Passage | Tipo Cavernas, portanto `TMUSIC_CAVES`, apesar da entrada próxima ao início do jogo. | Compartilha a composição das Cavernas. |
| Covil de Lazarus / Unholy Altar | O mapa especial é do tipo Catedral; a configuração de quests multijogador pode manter o encontro no andar 15, do tipo Inferno. Há também uma cena em vídeo e fala do personagem. | Não pressupor que Lazarus use sempre Catacumbas, nem que sua fala seja uma faixa musical. |
| Arenas opcionais do DevilutionX | Church Arena usa Catedral; Hell Arena e Circle of Life Arena usam Inferno. | Não há nova composição obrigatória. Não fazem parte da campanha original. |
| Fim da campanha | `DoEnding()` solicita Catacumbas antes de `loopdend.smk`, mas o playback de vídeos desliga a música ambiente e pode usar áudio próprio. | A chamada não comprova que Catacumbas seja ouvida durante o vídeo. Não tratar o encerramento como uma nona faixa ambiente editável. |

Fontes: menu e rotação em [`menu.cpp`](../Source/menu.cpp#L36), créditos/suporte em [`menu.cpp`](../Source/menu.cpp#L191) e [`credits.cpp`](../Source/DiabloUI/credits.cpp#L187); tipos dos mapas de missão em [`questdat.tsv`](../assets/txtdata/quests/questdat.tsv#L14), entrada em [`quests.cpp`](../Source/quests.cpp#L194), arenas em [`setmaps.h`](../Source/levels/setmaps.h#L17); encerramento em [`DoEnding`](../Source/monster.cpp#L4075) e suspensão da música em [`play_movie`](../Source/movie.cpp#L37).

## Shareware e ausência de músicas completas

O fallback de músicas originais não depende apenas de `gbIsSpawn`: `HaveFullMusic()` verifica se `music/dintro.wav` ou `music/dintro.mp3` existe. Na ausência deles, o loader usa a tabela shareware. Isso precisa continuar separado da presença de músicas autorais do projeto.

| ID / ambiente | Recurso na tabela shareware |
| --- | --- |
| Tristram | `music/stowne.wav` |
| Catedral, Catacumbas, Cavernas e Inferno | `music/slvla.wav`, compartilhado pelos quatro IDs |
| Ninho / Hellfire | `music/dlvlf.wav`, se o conteúdo estiver disponível |
| Cripta / Hellfire | `music/dlvle.wav`, se o conteúdo estiver disponível |
| Intro / menu | `music/sintro.wav` |

O inventário não transforma ambientes indisponíveis no shareware em conteúdo jogável. Fonte: caminhos shareware em [`music_catalog.hpp`](../Source/engine/music_catalog.hpp) e `HaveFullMusic` em [`assets.hpp`](../Source/engine/assets.hpp).

## Cenas em vídeo: lista separada

As cenas abaixo são chamadas pelo código, mas **não são IDs da trilha ambiente**. O player lê a faixa de áudio 0 do próprio arquivo Smacker quando ela existe. Não foi feita extração nem inspeção do conteúdo sonoro de cada vídeo neste inventário; não se deve concluir que todos contenham música separável de falas e efeitos.

| Vídeo | Contexto / condição | Estado da substituição musical |
| --- | --- | --- |
| `gendata/logo.smk` | Abertura com logotipo, conforme opção de inicialização | Fora do seletor de músicas ambiente. |
| `gendata/diablo1.smk` | Introdução do Diablo, inicialização ou apresentação a partir do menu | Fora do seletor; áudio embutido pode incluir vários elementos. |
| `gendata/Hellfire.smk` | Introdução quando Hellfire está ativo | Fora do seletor; conteúdo condicional. |
| `gendata/fprst3.smk` | Cena de Lazarus no fluxo da missão; playback em partida não suportado no multijogador | Fora do seletor; preservar sincronização e falas. |
| `gendata/diabvic1.smk` | Vitória de Sorcerer / Monk | Fora do seletor; depende da classe. |
| `gendata/diabvic2.smk` | Vitória de Warrior / Barbarian | Fora do seletor; depende da classe. |
| `gendata/diabvic3.smk` | Vitória das demais classes | Fora do seletor; depende da classe. |
| `gendata/diabend.smk` | Encerramento após o vídeo de vitória | Fora do seletor. |
| `gendata/loopdend.smk` | Loop de encerramento até o jogador fechar | Fora do seletor. |

Fontes: abertura em [`DiabloSplash`](../Source/diablo.cpp#L1401), introdução pelo menu em [`PlayIntro`](../Source/menu.cpp#L95), Lazarus em [`LazarusAi`](../Source/monster.cpp#L2879), vitória/encerramento em [`DoEnding`](../Source/monster.cpp#L4075) e áudio dos vídeos em [`SVidPlayBegin`](../Source/storm/storm_svid.cpp#L346).

Separar ou substituir trilhas de cinematics exige uma entrega própria: identificar o áudio disponível, preservar diálogos/efeitos e sincronização, definir direitos de distribuição e validar o player. Não é necessário para oferecer a escolha Vanilla / autoral dos oito ambientes.

## Organização das novas músicas

Cada composição recebida deve declarar seu ambiente da tabela, nome visível, revisão, autoria/origem e licença antes da distribuição. O título do arquivo, sozinho, não atribui uma faixa a todos os locais. Preserve masters fora do repositório e mantenha as músicas originais nos dados locais do jogador. Quando uma variante rock não existir para um ambiente, a interface deve informar isso e continuar usando a original, sem apresentar uma substituição inexistente como pronta.
