# Música do menu do Diablo 3D

Em 8 de outubro de 2026, o responsável pelo projeto forneceu `Main_Menu.mp3` como nova música do menu. O arquivo foi instalado localmente, sem alterar seu áudio ou substituir arquivos da instalação GOG. O MP3 original, a cópia de trabalho e a cópia carregada pelo jogo têm o mesmo SHA-256.

## Carregamento

O recurso opcional é `music/d3d-main-menu.mp3`. Com ele presente, a faixa INTRO usa esse arquivo, e o menu retorna a essa faixa depois de uma partida ou da apresentação. Sem ele, permanece a escolha/rotação herdada. O arquivo não faz o jogo confundir uma instalação shareware com uma instalação que contém todas as músicas.

As músicas da cidade e dos níveis conservam seus caminhos originais. O menu de pausa não troca a música da partida. Volume, mute e streaming continuam usando os controles de áudio existentes; o playback usa a repetição nativa. Isso não comprova uma transição musical imperceptível no fim da composição.

O build SDL2 atual possui decoders MP3 e WAV, sem Vorbis/OGG. Por isso, o runtime usa o MP3 fornecido, sem recompressão. A cópia OGG é uma exportação separada para uso futuro; não é carregada por esta versão do jogo.

## Preparar um arquivo local

1. Preserve sua fonte fora do repositório. Neste workspace, o master está em `audio/menu/main-menu-v1/Main_Menu.mp3` e a conversão em `Main_Menu.ogg` na mesma pasta.
2. Copie a fonte MP3 para `assets/music/d3d-main-menu.mp3` na raiz do código-fonte.
3. Configure e compile novamente, sem `-SkipConfigure` nessa primeira inclusão. `CMake/Assets.cmake` registra a faixa opcional e a copia para `build/assets/music/d3d-main-menu.mp3`. A lista de assets preserva o arquivo na limpeza dos próximos builds.
4. Inicie o jogo pelo atalho habitual. Um override com o mesmo caminho em um perfil/mod tem a precedência normal da engine.

O arquivo de áudio, seus metadados, masters e diagnósticos permanecem locais nesta entrega. O repositório publica o suporte opcional e este contrato; não distribui a faixa. Para uma futura distribuição da música, registrar sua autoria, origem e licença junto do asset. O contrato não altera a licença herdada da engine.

## Revisão local selecionada

| Arquivo | SHA-256 | Uso |
| --- | --- | --- |
| `Main_Menu.mp3` | `4cacef45cfb55ce58fb3cee1761ffc848001ddfa71dafe3dcb084ef8d59793d7` | Fonte e runtime; 1.983.623 bytes, 48 kHz, estéreo. |
| `Main_Menu.ogg` | `a1f9a503b7dc415982c13161bde71725ba783cef39bf218c38299859d00d6261` | Vorbis, qualidade 6, 48 kHz estéreo; 1.780.482 bytes. Exportação sem capa/metadados da fonte. |

Conversão utilizada: FFmpeg, seleção apenas do stream de áudio, `libvorbis -q:a 6`, sem alteração do MP3 original. A duração informada pelo contêiner é 85 segundos; o decoder real do jogo lê 85,056 segundos incluindo o padding do MP3.

## Evidência técnica

O probe privado foi ligado às mesmas bibliotecas estáticas SDL_audiolib e SDL2 do jogo. `Aulib::DecoderDrmp3` leu 4.082.688 frames / 8.165.376 amostras, sem valores não finitos e com conteúdo não silencioso. Rewind após EOF, segunda decodificação integral idêntica e seek para o meio passaram, sem abrir dispositivo ou tocar som. Evidência local: `diagnostics/menu-music/decoder-evidence.json`.

O build candidato foi concluído, e os bytes da fonte e do asset copiado foram comparados. A revisão artística da composição e da transição de repetição pertence ao autor; os testes de arquivo não substituem essa escuta.

O menu foi iniciado com vídeo/áudio normais, e o log confirmou `Diablo 3D menu music playing: music\d3d-main-menu.mp3` após o sucesso de `Play`. Os executáveis Godot, habitual e de qualidade receberam os mesmos bytes: SHA-256 `87536019928150c931b681d378ec2e85bd750a8cff1d679c3cdfdade023892be`. A instalação preservou configuração/saves, e os saves continuaram idênticos após o teste do menu. Evidência: `diagnostics/menu-music/installed-20261008-023431/validation.json`. Uma tentativa anterior com drivers SDL dummy foi inconclusiva na inicialização; ela não foi usada como prova de playback.
