# Música do site e biblioteca pública

Entrega solicitada pelo usuário em 8 de outubro de 2026. A biblioteca em `/musica/` e `/en/musica/` permite ouvir e baixar gratuitamente regravações/reinterpretações das músicas originais de Diablo. As gravações originais do jogo não entram nessa publicação. Não se exige cadastro de e-mail.

## Composição original e produção das versões

**Composição original: Matt Uelmen, para Diablo (Blizzard Entertainment). Regravação/reinterpretação produzida por Douglas Pan com auxílio de IA.**

Em inglês: **Original composition: Matt Uelmen, for Diablo (Blizzard Entertainment). Cover/reinterpretation produced by Douglas Pan using AI.** A fonte de autoria original é o [manual oficial de Diablo, página impressa 78](https://ftp.blizzard.com/pub/misc/Diablo.PDF). `Main Menu` permanece o rótulo local; o slot técnico `dintro` não confirma por si só o título oficial da composição.

`music_credits.py` aplica esse crédito ao player, faixas, cards do YouTube, descrição da página, JSON-LD e manifestos públicos PT/EN. No JSON-LD, `MusicRecording.recordingOf.composer` identifica Matt Uelmen; `producer` e `creditText` identificam a produção de Douglas Pan com IA. O manifesto público recebe campos adicionais `credits`; os campos de identidade, hashes e `tags` continuam descrevendo os mesmos bytes validados no manifesto fonte.

Esta correção editorial preserva todos os MP3s e masters. A lista permitida de tags existente não inclui `TCOM`; ela é validada sem mudança nesta entrega. As tags Douglas Pan creditam a produção das versões e não substituem o crédito da composição original exibido no site. Alterações futuras nas tags de áudio exigem coordenação com o responsável pelo catálogo/preparo, fora desta correção textual.

## Catálogo e autoria interna

`Source/engine/music_catalog.hpp` é a fonte única dos IDs, ambientes, variantes e caminhos elegíveis sob `music/d3d/`. `prepare_soundtrack.py` descobre os arquivos personalizados disponíveis e produz `soundtrack.json` e cópias públicas em `public/assets/audio/soundtrack/`. Hoje são cinco arquivos: `menu-rock2`, `menu-alternative`, `town-rock`, `town-alternative` e `town-third`. A terceira versão de Tristram aparenta ser outra exportação da primeira; cinco arquivos não significam cinco composições diferentes. Outros seis slots personalizados ainda não têm áudio disponível.

**Douglas Pan** é obrigatório nas tags internas de artista (`TPE1`), artista do álbum (`TPE2`) e autoria (`TXXX:AUTHOR`). Título e álbum também são gravados. MP3s usam ID3v2.3, com texto UTF-16 compatível com leitores do Windows. Esse crédito solicitado pelo responsável aplica-se aos arquivos personalizados; não reatribui a composição da trilha original. Download gratuito não cria uma licença adicional para remix ou exploração comercial.

O preparo preserva os arquivos de entrada e seus hashes. O áudio é copiado sem recompressão. Capas, capítulos, tags antigas e dados privados são removidos antes da gravação da lista permitida. A verificação compara os pacotes de áudio com a fonte, lê as tags com `ffprobe` e decodifica integralmente cada cópia. Uma falha durante a promoção restaura a biblioteca e o manifesto anteriores.

## Publicação de novas faixas

1. Entregar a faixa personalizada no caminho aprovado do catálogo do engine, preservando o master.
2. Executar `python website/prepare_soundtrack.py`. Pode-se indicar `--assets-root` para outra pasta de assets e `--receipt` para o recibo privado. Isso atualiza as cópias e o manifesto; não publica o site.
3. Executar `python website/prepare_soundtrack.py --check` para conferir também os arquivos locais do jogo, novos slots disponíveis e hashes de origem.
4. Executar os testes, o build e `python website/verify.py`. Revisar títulos/contexto PT/EN e a prévia. Publicar os arquivos exatos pelo fluxo coordenado do projeto.
5. Baixar os MP3s do site publicado e conferir hashes e tags dos bytes efetivamente servidos antes de registrar a entrega como concluída.

O build valida o manifesto contra o catálogo atual e lê os MP3s reais, incluindo decodificação completa. Tags ausentes ou diferentes de Douglas Pan, arquivos adulterados, conteúdo privado incorporado, caminhos fora da área pública e áudios não declarados bloqueiam a publicação. A CI não exige masters privados: valida as cópias públicas e a identidade do catálogo. O `--check` local é necessário para detectar uma nova faixa ainda ausente do manifesto quando apenas o arquivo local mudou. Dependências: `mutagen` fixado em `requirements.txt`, `ffmpeg` para o gate de leitura e `ffprobe` para preparar as cópias.

`source_sha256` registra a origem histórica antes da gravação dos créditos. O `--check` local aceita os bytes dessa origem ou os bytes exatos da cópia pública já validada, permitindo que o integrador instale os MP3s creditados também no jogo. Qualquer outra alteração de bytes continua sendo rejeitada; instalar as cópias no runtime não reescreve os masters nem exige outra recompressão.

## Player da biblioteca

**Correção dos plays nos cards, 8 de outubro:** os círculos ▶ das músicas e dos vídeos WIP eram spans decorativos, sem ação. Agora são botões nativos acessíveis por clique, Enter e Espaço, com o mesmo comando de “Ouvir aqui”/“Assistir aqui”. Esse clique cria o player no card e solicita reprodução (`autoplay=1`, permissão `autoplay` e `playsinline`); nenhum iframe do YouTube é carregado antes da escolha. Ao abrir outro vídeo, o anterior é descarregado e seu poster restaurado; os players de áudio locais são pausados para evitar sobreposição. Iniciar o áudio local também encerra o iframe ativo. Links externos continuam disponíveis sem JavaScript. A política do navegador ou a disponibilidade do YouTube ainda podem impedir início automático. Não altera MP3s, tags, créditos, catálogo ou player nativo da biblioteca.

Há um único player HTML nativo com controles de reprodução, volume e posição, sem início automático. Os botões da lista selecionam a faixa nesse mesmo player e seguem para a próxima ao terminar; a última encerra a sequência. Volume e silêncio podem ser lembrados localmente. Títulos, durações, crédito e downloads ficam acessíveis sem JavaScript; cada arquivo também pode ser aberto diretamente. O player de fundo é omitido nessa página, evitando reprodução simultânea mesmo sem JavaScript.

## Música de fundo nas demais páginas

O player compacto continua usando [Plyr 3.8.5](https://github.com/sampotts/plyr/tree/v3.8.5), com JavaScript, CSS, ícones e licença MIT auto-hospedados. A revisão upstream é `de3eeb6b60fbcb592a5961731183be7f603b5ddf`. Controles nativos permanecem como fallback.

`public/assets/audio/background-music.mp3` agora é uma cópia idêntica de `town-third.mp3`, já creditada internamente: **5.485.365 bytes**, SHA-256 `b9e6797b0e4e3c73ec08f53be6de9d23df424f48297e5bfc22ed353e65d16d9b`. O valor exato e as durações estão no manifesto gerado. O áudio fornecido permanece preservado em sua origem.

Volume inicial de 25%, repetição ligada e tentativa normal de início automático. Se o navegador bloquear áudio, o visitante usa o botão de tocar. Pausa, volume, silêncio e posição são lembrados quando o armazenamento local está disponível; a navegação completa pode interromper brevemente a faixa ou exigir outro clique. PT/EN e retomada pelo cache de navegação permanecem suportados. O parâmetro de versão acompanha os bytes dos assets.
