# Trilha sonora do Diablo 3D

O jogo permite escolher **Vanilla**, **Rock** ou uma combinação por ambiente. Em 8 de outubro de 2026, o autor forneceu `Main_Menu_Rock2.mp3` como nova escolha padrão do menu. A primeira composição, `Main_Menu.mp3`, continua disponível como alternativa. Os arquivos de áudio permanecem locais; o repositório publica o suporte, os controles e o [inventário completo](MUSIC-CATALOG.md).

## Escolher no jogo

- No menu principal: **Settings → Soundtrack**.
- Durante a partida: **Esc → Options → Audio Options → Soundtrack**.
- **Vanilla** usa as músicas originais, inclusive a rotação herdada do menu.
- **Rock** usa as substituições instaladas; onde ainda não há uma, toca a original.
- **Custom** combina as escolhas individuais. Alterar uma faixa individual seleciona esse modo automaticamente.

O menu tem **Original**, **Rock2** e **Main Menu**, a composição anterior. Os outros sete ambientes têm **Original** e **Rock**. No menu da partida, **Music by Location** lista os oito ambientes em três páginas, com até cinco linhas por tela. A marca `*` identifica a variante selecionada; `pending` indica que falta o arquivo e a música original será usada. Ninho e Cripta são conteúdo Hellfire; aparecer no catálogo não os desbloqueia na campanha Diablo.

As escolhas são salvas por perfil em `[Music]`: `Theme` (0 Vanilla, 1 Rock, 2 Custom) e `Menu`, `Town`, `Cathedral`, `Catacombs`, `Caves`, `Hell`, `Nest`, `Crypt` (0 Original, 1 Rock; 2 alternativa somente no menu). O padrão é Rock. A escolha da música atualmente tocada é aplicada imediatamente; as demais valem quando seu ambiente for carregado. Uma preferência que não muda o arquivo ativo não reinicia a faixa. Volume, desligar música e mute conservam os controles existentes.

A pausa mantém a música do ambiente. O contexto de menu é explícito, porque o carregamento da partida ocorre antes de `gbRunGame` se tornar verdadeiro. A rotação Vanilla do menu pode usar IDs de níveis, mas não usa as escolhas Rock desses níveis: menu e partida são independentes. Músicas/vozes embutidas nos vídeos não fazem parte deste seletor; estão listadas separadamente no inventário.

## Arquivos opcionais

| Ambiente | Recurso local, relativo a `assets/` | Disponível nesta entrega |
| --- | --- | --- |
| Menu — Rock2 | `music/d3d/menu-rock2.mp3` | Sim, instalado localmente. |
| Menu — primeira composição | `music/d3d/menu-alternative.mp3` | Sim, instalado localmente. |
| Tristram | `music/d3d/town-rock.mp3` | Pendente. |
| Catedral | `music/d3d/cathedral-rock.mp3` | Pendente. |
| Catacumbas | `music/d3d/catacombs-rock.mp3` | Pendente. |
| Cavernas | `music/d3d/caves-rock.mp3` | Pendente. |
| Inferno | `music/d3d/hell-rock.mp3` | Pendente. |
| Ninho — Hellfire | `music/d3d/nest-rock.mp3` | Pendente. |
| Cripta — Hellfire | `music/d3d/crypt-rock.mp3` | Pendente. |

Preserve o master fora do repositório, copie a faixa para o caminho correspondente e configure/compile novamente, sem `-SkipConfigure` na primeira inclusão. CMake registra apenas os arquivos existentes e os mantém durante a limpeza de assets. Os originais permanecem nos dados locais do jogador, sem alteração da instalação GOG. O antigo caminho `music/d3d-main-menu.mp3` foi aposentado; use os caminhos da tabela.

Ausência de uma substituição retorna à original. Se o decoder rejeitar um arquivo instalado, o jogo registra o erro e tenta a original sem encerrar a partida. A presença de um MP3 autoral não faz uma instalação shareware parecer possuir a trilha completa. Arquivos originais ausentes também continuam sujeitos às limitações dos dados instalados.

O build SDL2 atual lê MP3 e WAV; não habilita Vorbis/OGG. O runtime usa os MP3 recebidos sem recompressão. A exportação OGG da primeira composição continua disponível separadamente para uso futuro. Distribuir músicas exigirá registrar autoria, origem e licença; esta entrega não publica os arquivos nem seus metadados incorporados e não altera a licença herdada da engine.

## Fontes locais e validação

| Composição | SHA-256 do MP3 fonte/runtime | Propriedades |
| --- | --- | --- |
| Rock2 | `f100f775332cd3d39357ece3b38549029075fe36d6f73476f14657c173edf2e4` | 1.854.648 bytes; 48 kHz, estéreo; 83,52 s decodificados. |
| Primeira versão | `4cacef45cfb55ce58fb3cee1761ffc848001ddfa71dafe3dcb084ef8d59793d7` | 1.983.623 bytes; 48 kHz, estéreo; 85,056 s decodificados. |

Masters privados em `audio/menu/main-menu-rock2-v1/` e `audio/menu/main-menu-v1/`. O decoder real SDL_audiolib passou leitura completa, ausência de valores não finitos, conteúdo não silencioso, rewind após EOF, segunda leitura idêntica e seek nas duas composições. Na Rock2 foram 4.008.960 frames / 8.017.920 amostras. Isso valida o arquivo e a repetição técnica; a transição musical no fim da composição depende da escuta do autor.

Evidência da primeira entrega: `diagnostics/menu-music/`. Evidência desta entrega e dos seletores: `diagnostics/music-selection/`. O teste `music_selection_smoke` cobre seleção, contextos independentes, persistência, navegação dos menus e preferência inicial 3D, usando configuração descartável e sem alterar saves do jogador.
