# Trilha sonora do Diablo 3D

O jogo permite escolher **Vanilla**, **Rock** ou uma combinação por ambiente. Em 8 de outubro de 2026, o autor forneceu `Main_Menu_Rock2.mp3` como nova escolha padrão do menu. A primeira composição, `Main_Menu.mp3`, continua disponível como alternativa. Também forneceu `06.wav`, `06 (2).wav` e `06 (1).mp3`, disponíveis como Tristram 1, 2 e 3. São três opções de arquivo; a terceira aparenta ser outra exportação da primeira gravação, conforme comparação local, e foi mantida separadamente a pedido do autor. As cinco faixas personalizadas estão disponíveis localmente e sua publicação para ouvir e baixar gratuitamente no site foi autorizada. Essa publicação está em preparação. O repositório publica o suporte, os controles e o [inventário completo](MUSIC-CATALOG.md).

## Autoria dos arquivos personalizados e publicação

Por instrução explícita do autor, todo áudio personalizado que o projeto gerar, converter e distribuir deve trazer **Douglas Pan nas tags internas de artista/autoria**. Isso vale para as cópias instaláveis e os downloads gratuitos do site, incluindo novas faixas futuras. Gravar os campos equivalentes suportados por cada formato, como artista e artista do álbum em ID3/MP3 e Vorbis/OGG; título e álbum identificam a versão e o projeto. Confirmar por leitura independente dos arquivos finais antes de liberar os links. Somente escrever o nome na página ou no nome do arquivo não atende à instrução.

Os masters fornecidos ficam preservados. As cópias destinadas à publicação removem metadados pessoais não solicitados e mantêm apenas identificação/autoria autorizadas. O catálogo público deve validar a presença das tags no build e creditar o mesmo nome no player. O pedido autoriza ouvir e baixar gratuitamente as músicas personalizadas; não define uma licença adicional de remix ou uso comercial. A trilha vanilla continua com sua autoria original.

A autorização atual cobre **duas composições do menu e três opções de Tristram**. A [biblioteca do site](https://douglasopan.github.io/diablo-3d/musica/) está publicada em português e inglês, com player e downloads gratuitos. A revisão `582beac79` passou no build e deploy do GitHub Pages; os cinco MP3s servidos publicamente tiveram seus hashes e créditos ID3 conferidos novamente, coincidindo com as cópias instaladas. Evidência privada: `diagnostics/audio-metadata-20261008/public-live-proof.json`.

**Créditos instalados no jogo em 8 de outubro, 11:01:58 UTC:** as cinco faixas personalizadas em `build/assets/music/d3d/` e suas cinco fontes de build em `assets/music/d3d/` receberam cópias idênticas aos arquivos públicos validados, com **Douglas Pan** em artista, artista do álbum e Autor. Leitura ID3, leitura independente pelo ffprobe e decodificação completa passaram; os pacotes de áudio MPEG conservaram sua identidade, sem recompressão. Os masters fornecidos permanecem intactos. Os 39 arquivos do perfil, incluindo configuração, modelos, luz e saves, não mudaram. Recibo privado: `diagnostics/audio-metadata-20261008/installed-20261008T110158Z/receipt.json`. O [manifesto do site](../website/soundtrack.json) identifica os novos hashes dos arquivos com créditos; os hashes de fontes brutas abaixo permanecem como histórico. Atualizar as fontes de build evita perder os créditos na próxima compilação.

## Escolher no jogo

- No menu principal: **Settings → Soundtrack**.
- Durante a partida: **Esc → Configurações → Trilha sonora** (primeira categoria; em inglês: Settings → Soundtrack).
- **Vanilla** usa as músicas originais, inclusive a rotação herdada do menu.
- **Rock** usa as substituições instaladas; em Tristram, sorteia entre os três arquivos disponíveis. Onde ainda não há uma substituição, toca a original.
- **Custom** combina as escolhas individuais. Alterar uma faixa individual seleciona esse modo automaticamente.

O menu tem **Original**, **Rock2** e **Main Menu**, a composição anterior. Tristram tem **Original**, **Tristram 1**, **Tristram 2**, **Tristram 3** e **Random** (aleatório). Os seis ambientes restantes têm **Original** e **Rock**. O navegador da partida reutiliza as nove entradas de `MusicOptions`: modo e oito ambientes, respeitando a disponibilidade dos arquivos e do áudio. A paginação agora é responsiva: até **oito entradas em 960×540** e **18 em 1920×1080**, com anterior/próxima/voltar num rodapé horizontal separado. Tristram mostra suas cinco escolhas numa única página. A marca `*` no valor identifica a escolha efetiva de Vanilla/Rock/Custom; `pending` indica que falta o arquivo e a música original será usada. Ninho e Cripta são conteúdo Hellfire; aparecer no catálogo não os desbloqueia na campanha Diablo. Teclado, mouse e controle usam o navegador documentado em [INGAME-SETTINGS.md](INGAME-SETTINGS.md).

A revisão responsiva de 8 de outubro de 2026 passou **59.452 verificações do navegador de opções** e **540 de seleção musical**. Foi instalada às 10:45:49 UTC no iniciador habitual, com executável SHA-256 `a1e218fab604517f409557a5863352633b632f1f83e3f3c449b06393418140c9`; recibo privado `diagnostics/render-optimization-20261008/installed-20261008T104549Z/receipt.json`. As músicas não mudaram nessa instalação. Na revisão anterior, com cinco entradas fixas por página, a navegação foi compilada e instalada junto das câmeras: passaram 36.612 verificações do navegador, 535 de seleção e 55 de reprodução. O perfil, as músicas locais, as configurações e os saves foram preservados naquela instalação. As **55 verificações de reprodução** permanecem como evidência histórica; a revisão visual e a escuta durante uma partida continuam com o usuário. Os resultados abaixo também descrevem versões anteriores.
As escolhas são salvas por perfil em `[Music]`: `Theme` (0 Vanilla, 1 Rock, 2 Custom) e `Menu`, `Town`, `Cathedral`, `Catacombs`, `Caves`, `Hell`, `Nest`, `Crypt` (0 Original, 1 Rock; 2 alternativa no menu ou em Tristram; 3 aleatório somente em Tristram; 4 Tristram 3 somente em Tristram). O ID 3 continua significando Random nos perfis existentes. O padrão global é Rock e a escolha individual inicial de Tristram é Random. A escolha da música atualmente tocada é aplicada imediatamente; as demais valem quando seu ambiente for carregado. Uma preferência que conserva arquivo e política de repetição não reinicia a faixa. Trocar entre uma versão fixa e aleatória reinicia o playback para aplicar a nova política, mesmo se o arquivo escolhido for o mesmo. Volume, desligar música e mute conservam os controles existentes.

Em Tristram, **um novo sorteio acontece ao fim de cada música**, conforme solicitado pelo autor. Cada sorteio é independente e pode repetir a mesma versão. A seleção atribui a mesma probabilidade a cada arquivo disponível, inclusive quando só dois dos três estão instalados; com uma única versão instalada, ela será escolhida novamente. Escolher Tristram 1, Tristram 2 ou Tristram 3 fixa essa versão em repetição; escolher Vanilla mantém a música original. O gerador aleatório do áudio é separado do RNG da simulação, preservando mapa, itens e comportamento da partida.

O loop principal verifica o fim da execução e inicia a próxima escolha; o callback da thread de áudio não carrega arquivos nem recria streams. Mudanças em opções de outros ambientes conservam o sorteio atual. Desligar ou interromper a música limpa o estado da sequência, impedindo que ela volte sozinha.

A pausa mantém a música do ambiente. O contexto de menu é explícito, porque o carregamento da partida ocorre antes de `gbRunGame` se tornar verdadeiro. A rotação Vanilla do menu pode usar IDs de níveis, mas não usa as escolhas Rock desses níveis: menu e partida são independentes. Músicas/vozes embutidas nos vídeos não fazem parte deste seletor; estão listadas separadamente no inventário.

## Arquivos opcionais

| Ambiente | Recurso local, relativo a `assets/` | Disponível nesta entrega |
| --- | --- | --- |
| Menu — Rock2 | `music/d3d/menu-rock2.mp3` | Sim, instalado localmente. |
| Menu — primeira composição | `music/d3d/menu-alternative.mp3` | Sim, instalado localmente. |
| Tristram 1 — `06.wav` | `music/d3d/town-rock.mp3` | Recebida, convertida e validada no decoder; preparada localmente. |
| Tristram 2 — `06 (2).wav` | `music/d3d/town-alternative.mp3` | Recebida, convertida e validada no decoder; preparada localmente. |
| Tristram 3 — `06 (1).mp3` | `music/d3d/town-third.mp3` | Recebida e validada no decoder; cópia exata, sem recompressão. |
| Catedral | `music/d3d/cathedral-rock.mp3` | Pendente. |
| Catacumbas | `music/d3d/catacombs-rock.mp3` | Pendente. |
| Cavernas | `music/d3d/caves-rock.mp3` | Pendente. |
| Inferno | `music/d3d/hell-rock.mp3` | Pendente. |
| Ninho — Hellfire | `music/d3d/nest-rock.mp3` | Pendente. |
| Cripta — Hellfire | `music/d3d/crypt-rock.mp3` | Pendente. |

Preserve o master fora do repositório, copie a faixa para o caminho correspondente e configure/compile novamente, sem `-SkipConfigure` na primeira inclusão. CMake registra apenas os arquivos existentes e os mantém durante a limpeza de assets. Os originais permanecem nos dados locais do jogador, sem alteração da instalação GOG. O antigo caminho `music/d3d-main-menu.mp3` foi aposentado; use os caminhos da tabela.

Ausência de uma substituição retorna à original. Se o decoder rejeitar um arquivo instalado, o jogo registra o erro e tenta a original sem encerrar a partida. Em Random, esse fallback repete a original e não tenta carregar o arquivo inválido novamente a cada fim de música ou mudança de opção não relacionada. A presença de um MP3 autoral não faz uma instalação shareware parecer possuir a trilha completa. Arquivos originais ausentes também continuam sujeitos às limitações dos dados instalados.

O build SDL2 atual lê MP3 e WAV; não habilita Vorbis/OGG. Os MP3 recebidos para o menu e Tristram 3 são usados sem recompressão do áudio. Os dois WAVs de Tristram foram preservados como masters e convertidos em MP3 para os caminhos opcionais desta entrega. A exportação OGG da primeira composição continua disponível separadamente para uso futuro. A distribuição gratuita das cinco faixas foi autorizada e está em preparação, com autoria incorporada e origem documentada; a licença herdada da engine permanece a mesma.

## Fontes locais e validação

As tabelas abaixo registram os **snapshots anteriores à inclusão das tags de autoria** nas cópias de distribuição. Os masters fornecidos permanecem inalterados. Acrescentar metadados aos MP3 runtime muda seu SHA-256 mesmo quando o áudio comprimido é preservado sem recompressão. Os hashes finais, a autoria interna e a equivalência do áudio precisam ser conferidos nas cópias preparadas antes de liberar os downloads.

| Composição | SHA-256 histórico do MP3 fonte/runtime, antes das tags | Propriedades |
| --- | --- | --- |
| Rock2 | `f100f775332cd3d39357ece3b38549029075fe36d6f73476f14657c173edf2e4` | 1.854.648 bytes; 48 kHz, estéreo; 83,52 s decodificados. |
| Primeira versão | `4cacef45cfb55ce58fb3cee1761ffc848001ddfa71dafe3dcb084ef8d59793d7` | 1.983.623 bytes; 48 kHz, estéreo; 85,056 s decodificados. |

Masters privados em `audio/menu/main-menu-rock2-v1/` e `audio/menu/main-menu-v1/`. O decoder real SDL_audiolib passou leitura completa, ausência de valores não finitos, conteúdo não silencioso, rewind após EOF, segunda leitura idêntica e seek nas duas composições. Na Rock2 foram 4.008.960 frames / 8.017.920 amostras. Isso valida o arquivo e a repetição técnica; a transição musical no fim da composição depende da escuta do autor.

### Versões de Tristram

Masters privados preservados em `audio/town/tristram-rock-v1/`:

| Versão | Master WAV | SHA-256 do master |
| --- | --- | --- |
| Tristram 1 | `06.wav` — 44.811.280 bytes | `8b1f96892e0cda6c7c7c998c49384b4436af764a243d47942f70d786ec344861` |
| Tristram 2 | `06 (2).wav` — 45.064.720 bytes | `b4c89b5ba11d29cceb56fae8572438c3ba26c5f968beb490e215321fc64c35a4` |

| Versão | SHA-256 histórico do MP3 usado no jogo, antes das tags | Propriedades do MP3 decodificado |
| --- | --- | --- |
| Tristram 1 | `4d71870e20408ab64a7f5b734e2b81a2879061d83e1a7cd022e6b1c3ad0390d6` | 7.420.268 bytes; 48 kHz, estéreo; 233,376 s; 11.202.048 frames. |
| Tristram 2 | `1f8cfeeea408b72c16dac344b497555912fddb23d38969c617258114dc2c051a` | 7.426.316 bytes; 48 kHz, estéreo; 234,696 s; 11.265.408 frames. |
| Tristram 3 — MP3 recebido, sem recompressão | `015e013d60431f6b089e1568279302104bc811caaabaebd0390f94b476c568ba` | 5.511.329 bytes; 48 kHz, estéreo; 233,376 s; 11.202.048 frames. |

Os três MP3 passaram leitura integral pelo decoder real `Aulib::DecoderDrmp3`, sem amostras não finitas, com conteúdo não silencioso, rewind após EOF, segunda leitura integral idêntica, seek intermediário e rewind final. Evidência privada: `diagnostics/town-music/variation-1-decoder.json`, `variation-2-decoder.json` e `variation-3-decoder.json`. O arquivo fonte `06 (1).mp3` também foi preservado junto aos masters; naquele snapshot anterior às tags, seu hash era idêntico ao recurso `town-third.mp3`. Uma comparação de Tristram 3 com o primeiro WAV, em mono a 4 kHz durante os 233,32 s da fonte, encontrou correlação de 0,9996204: isso sugere a mesma gravação com outra compressão, sem remover a terceira opção solicitada. Esses testes de arquivo não substituem a escuta artística nem, isoladamente, comprovam o encadeamento dentro da partida.

Evidência da primeira entrega: `diagnostics/menu-music/`. Evidência dos seletores: `diagnostics/music-selection/`. O teste `music_selection_smoke` cobre seleção, contextos independentes, persistência, navegação dos menus e preferência inicial 3D, usando configuração descartável e sem alterar saves do jogador. `music_playback_smoke` exercita o decoder/mixer reais com MP3 sintético curto e WAVs gerados: EOF, novo sorteio, preservação de refresh, parada, mudança de política, contextos menu/partida e fallback. Esse diagnóstico usa uma pasta temporária e o dispositivo SDL dummy; não acessa arquivos proprietários ou saves.

Validação integrada histórica em 8 de outubro de 2026: **415 verificações de seleção/menu/persistência** e **38 de reprodução real/EOF**, sem acessar saves habituais. A terceira faixa fixa, o sorteio com somente ela presente e todos os subconjuntos de disponibilidade foram verificados. Naquela entrega, os áudios ficaram locais.

### Continuidade ao cancelar Multiplayer — 8 de outubro de 2026

Defeito relatado na partida: abrir Multiplayer e cancelar fazia a música reiniciar. A seleção de conexão e a seleção de herói mantinham o áudio, mas `InitMenu` retornava de `StartGame` também após cancelamento e chamava `RefreshMusic`; essa rotina avançava a rotação herdada e recriava o stream. O novo caminho compartilhado `RefreshMenuMusic` mantém a faixa ativa e consulta somente mudanças de preferência. Não libera nem reinicia um arquivo cuja seleção permaneceu igual, incluindo a versão Vanilla e o fallback de uma substituição rejeitada pelo decoder.

A primeira entrada, a exibição da introdução e a saída de uma partida real continuam iniciando a trilha do menu depois de `music_stop`. Uma troca explícita da música nas configurações continua imediata. A política aleatória de Tristram e seu sorteio ao fim da faixa não foram modificados.

O diagnóstico `music_playback_smoke` passou **55 verificações**, incluindo 17 novas de continuidade com SDL e decoder reais: retornos sucessivos de diálogos, versões Rock2/alternativa/Vanilla, IDs nativos de rotação, arquivos ausentes/corrompidos, retorno após uma partida parada, opção desativada e RNG da simulação. `music_selection_smoke` manteve suas **415 verificações** aprovadas. A prova observa os inícios reais de playback e a identidade da faixa; não mede a posição em amostras, não abre uma sessão de rede e não substitui a escuta do fluxo completo no jogo.
