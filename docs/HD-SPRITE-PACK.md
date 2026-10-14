# Atualização H23 — 11/10/2026

Pack eab2d3262d59: todas as 2.198 imagens continuam importadas;454 recursos/12.591 frames têm ligação. Novos: fonte de água tfountn conforme largura128 do objdat e três logos selecionados de Douglas em RGBA,240frames cada. Não é cobertura integral: fontes indexadas, terreno de Tristram e variantes sem recurso nativo seguem pendentes. Não substituir masters nem usar a versão r3 da fonte de água (largura96 rejeitada internamente).

`LoadD3dLogo` consulta o container antes do PCX; valida quantidade/dimensões e conserva o ciclo8s. Proxy de recuperação usa núcleo opaco sem ordered dither. `DrawD3dLogo` registra RGBA no menu ou painel aberto. Frame RGB de menu pertence à Surface/dimensões/pixels da UI, é limpo no begin/reset e mistura alpha real com o fundo; receitas de painel usam o cache RGB8MiB existente. Capturas SDL H23 de menu e pausa foram conferidas com chamas suaves; mundo e recuperação permanecem indexados.

Normal8a9796915cba, gateH23 PASS195.047, perfil sintético isolado. Não confundir readback SDL/GPU delimitado com FPS sustentado ou aceite humano do conjunto. Ver estado, fila e limitações em PROJECT-EXECUTION.md.

# Substituição gráfica na inicialização

## Revisão H12 — fontes, cinto, inventário e magias

O usuário rejeitou a H8 por fontes grossas e ícones com cores degradadas. A UI havia sido reduzida a proxies lógicos indexados e os atlases de fontes marcados `indexed` foram tratados como RGB. **Retomada motivada por defeito visual**, sem regenerar masters.

O candidato normal **05bc397f3b78**, pacote `8991ee91bca7` (1.198.136.907 bytes), preserva a importação integral das 2.198 imagens/2.205 arquivos. Tem 450 recursos ligados/11.855 frames: retiraram-se 44 substituições de fontes não fiéis e adicionou-se a ligação preferencial `data/spelicon`. A tipografia selecionada em `assets/fonts/*.clx` volta a ser usada. As fontes HD continuam importadas como masters, sem seleção automática.

Novas entradas `r/<frame-id>` guardam largura/altura e RGBA original, sem quantização. `LoadHdSpriteRgba` valida dimensões/limites e retorna ARGB para SDL. Cache pertence ao pack, tem teto de 8 MiB e é invalidado por troca de AssetsPath/reset/dispose; não compartilha tintas de instâncias. O acervo inteiro não é decodificado ao iniciar.

O cinto e as magias usam frames completos 4x na composição do HUD. `RecordD3dHudRgbaForeground` registra cor somente sobre cobertura com proxy de recuperação válido; máscaras pertencem a um frame, são limpas no begin/clear e liberadas no reset. Escritas indexadas posteriores invalidam a contribuição RGB sobreposta. A textura SDL final é ARGB8888. As regras nativas de skill/spell/scroll/charges/invalid continuam aplicadas; não alterar mana, disponibilidade ou input.

`DrawItem` registra RGB somente para itens usáveis em um painel aberto e com Surface pertencente ao frame. `RecordD3dPanelItemRgba` mantém cores por receita de painel, liberadas com invalidação/reset; composição respeita fundo e cursor nativos. Itens sem requisitos conservam o TRN nativo. Os proxies indexados permanecem como recuperação. **Não declarar que todo o mundo ou todas as interfaces passaram a true color**: mundo 3D/recuperação seguem indexados e o inventário ainda tem a geometria/resolução lógica nativa.

Gate H12 PASS **194.017 checks** delimitados: resolver sem MPQ, identidade dos 179 frames de inventário, fontes não sobrescritas, ícones 4x, RGB com mais de 256 cores, cache sem releitura, compositor SDL real do HUD e inventário, além de NEW/reentrada/save/load, GPU TP/FP, labels/combate/coleta C1. Readback do ícone mostrou **777 cores**; quatro contribuições RGB foram registradas pelo `DrawInv` real. Capturas `HD-HUD-RGB.png` e `HD-INVENTORY-RGB.png` foram inspecionadas: fonte fina original, poções e ícone legíveis sem o ruído vermelho/ciano. Isso é readback SDL por software em fixture privada, **não sessão física/FPS sustentado nem aceite humano**. H9–H11 preservam falhas da fixture em precondições do canal de cursor e upload da paleta; a engine não foi promovida com esses resultados.

Entrada de revisão: `D:/Diablo 1 3D/versions/diablo3d-hd-05bc397f3b78-h12/Iniciar-Diablo3D.cmd`; Um jogador → NativeLoadGate → Carregar jogo. Fonte isolada; habitual/web e demais frentes permanecem sem promoção.

## Correção de cores e importação completa — H8

O teste humano rejeitou o aspecto da H6: manchas vermelhas/ciano e fontes amarelas saturadas. A causa foi importar cores com índices da metade variável da paleta de Town, interpretados por L1 como outras cores. As fontes também ignoravam a faixa 192–207 esperada pelas TRNs. O importador foi corrigido e todo o payload gráfico foi reconstruído dos masters; não se reutilizou a quantização defeituosa.

H8 usa o mesmo EXE `458c20bbae45` com **novo pacote** SHA `7f235d4be574a39eed55fecb520ebd214076226ab8a0c5c4ed02a1a956da43b5`, 1.039.830.636 bytes. Contém **2.198 imagens originais / 2.205 arquivos de origem**, verificados individualmente, além de 494 recursos com ligação direta e 11.493 frames derivados. As imagens de origem estão comprimidas em entradas `a/`; montar o índice não as decodifica nem as torna automaticamente recursos ativos. Todas foram importadas; **nem todas as variantes possuem ligação de runtime**. Não declarar todo o jogo HD.

Arte mundial usa somente códigos compartilhados 0/128–254; glyphs usam a rampa 192–207. Conferência comprovou que a faixa compartilhada é idêntica em Town e nas quatro paletas L1. Isso elimina a interpretação errada e restaura tintas nativas de texto, mas **continua render indexado**, com perda de nuances na quantização. Não constitui renderer RGB de 24/32 bits; esse limite continua aberto.

Gate H8 PASS: 139.322 checks delimitados de loader sem MPQ, TRN, reentrada/save/load, GPU TP/FP, labels/combate/coleta. Contrato de cor: 11.917 verificações de índices. A captura `C1-native-item-labels-3D.png` da H7 foi inspecionada: label branca, sem o amarelo saturado. H8 preserva esses mesmos derivados e inclui o acervo comprimido completo. FPS sustentado/gameplay físico e aceite humano ainda não estão comprovados.

Entrada de revisão: `D:/Diablo 1 3D/versions/diablo3d-hd-458c20bbae45-h8/Iniciar-Diablo3D.cmd`; Um jogador → NativeLoadGate → Carregar jogo. H6 permanece histórica. Masters e demais frentes permanecem intactos. Usuário autorizou encerrar sessões e descartar saves de teste durante esta fase sem nova confirmação.

## Estado em 11/10/2026 — H6

O pedido humano reabriu a integração HD e determinou substituir o carregamento das imagens. O candidato **H6 / `458c20bbae45`** implementa um provedor de recursos que consulta `assets/d3d-sprites.art` **antes** de abrir o CEL, CL2, CLX ou PCX original. Um recurso presente usa a arte nova desde seu primeiro carregamento; não é uma sobreposição posterior à imagem original.

Entrega local: `D:/Diablo 1 3D/versions/diablo3d-hd-458c20bbae45-h6/Iniciar-Diablo3D.cmd`. Escolher **Um jogador → NativeLoadGate → Carregar jogo**. O herói é sintético e o perfil é separado, com Speed32, FOV80, GPU ligada, suavização desligada e nomes de itens ligados. R26, entrada habitual, versão web e saves pessoais foram preservados. Não se publicou binário nem arte privada.

O arquivo tem **218.128.921 bytes**, SHA-256 `df8b26c9c117fd1201cbdd721940d93983ecf4a144d6a6047af4d9ad39b9e05d`, **494 recursos** e **11.493 frames de textura HD**. Contagem de recursos não é contagem de personagens nem de toda a arte do jogo:

| Categoria | Recursos nativos substituídos |
| --- | ---: |
| NPCs | 9: oito NPCs e um sheet para as três vacas |
| Monstros presentes no pacote | 66 animações/sheets |
| Guerreiro com armadura leve | 39 animações/sheets |
| Itens no chão | 21 |
| Objetos | 31 |
| Interface/fontes | 115 |
| Pisos da Catedral | 213 materiais |

O master local permanece em `local-assets/damnationx-hd-b2c457645f39/hd`. A pasta web fornecida pelo usuário é o programa, não todo o acervo de imagens. O ZIP de origem tem SHA-256 `b2c457645f393d99c20b65a1b945725ebd80b84b7de557901dabef505c9e1cee`. Todos os derivados são locais; não colocar o container, MPQ, extrações ou imagens no Git.

## Contrato e donos

No checkout isolado `clean-base/devilutionx-c1-r1`, os componentes são:

| Componente | Responsabilidade |
| --- | --- |
| `Source/engine/hd_sprite_pack.*` | Índice, handle somente leitura, limites, resolução, validação de CLX e decodificação sob demanda |
| `Source/engine/load_{cel,cl2,clx,pcx}.cpp` | Consultar o provedor antes de qualquer acesso à imagem original |
| `Source/engine/clx_sprite.hpp` | Dimensões lógicas, identidade da textura HD e tabela de cores pertencente ao sprite |
| `Source/engine/render/clx_render.cpp` | Aplicar/compor TRN no proxy e na tabela HD do mesmo proprietário |
| `Source/engine/render/town_view.cpp` | Texturas 3x nos billboards; materiais de piso 128×128 escolhidos antes da decodificação original |
| `tools/build_hd_sprite_pack.py` | Importação offline, correspondências e recibo de cobertura/procedência |

O provedor monta o índice uma vez, no primeiro pedido gráfico, e mantém o arquivo aberto em leitura. Lê somente o recurso solicitado; não decodifica os 2.194 atlases na inicialização. `ResetHdSpritePack` libera handle/índice/estado, e mudança do caminho de assets invalida a montagem anterior. O handle tem liberação RAII. Sprites lógicos permanecem nos seus donos nativos; o cache existente de billboards é limitado a 192 entradas e segue a invalidação visual existente. Nenhum RNG novo, alteração de seed, relógio de simulação, colisão ou formato de save foi introduzido.

Formato `D3DSPR1`: cabeçalho de 16 bytes, índice de nomes/offsets/tamanhos, payloads CLX e paletas. Prefixos `s/` contêm sprites/materiais, `p/` paletas PCX e `h/` frames HD. O parser recusa limites inválidos, duplicatas, sobreposição, containers e RLE malformados. Recurso coberto e inválido retorna erro, em vez de carregar silenciosamente a imagem antiga. Ausência de correspondência é uma lacuna explícita e mantém o recurso nativo.

Contagem de frames, oito direções, dimensões lógicas e pivôs de contato seguem os contratos nativos. O importador lê arquivos originais **somente para metadados e paletas**; não copia seus pixels para o novo container. A arte HD é enquadrada com escala uniforme. A tabela TRN fica no header do sprite proprietário, evitando que duas variantes compartilhem uma tradução mutável. Os glifos mantêm seus índices; slots não atribuídos não deslocam os caracteres seguintes.

Os pisos reutilizam os derivados HD já preparados e conferidos por SHA, mantendo o ID de peça, MIN/SOL e geometria nativos. A retificação deixava dezesseis amostras de borda transparentes por imagem. O importador estende texels vizinhos **da própria imagem HD** nessa margem, sem reutilizar pixels antigos e sem abrir buracos artificiais no chão sólido.

## Evidência e limites

`diagnostics/dungeon-reference-integration-20261011-r1/hd-replacement-r1/GATE-H6.json` registra **139.322 verificações delimitadas**. A fixture comprovou:

- NPC, Guerreiro, item, objeto e PCX resolvidos com os arquivos MPQ desmontados; material de piso também carregado pelo arquivo novo nessa condição.
- Tradução TRN, composição de duas traduções, cobertura e isolamento entre dois proprietários do mesmo frame.
- NEW, Town↔Catedral, reentrada, SaveGame/full LoadGame e preservação de mapa, seed e colisão.
- Material HD publicado conferido com os pixels do novo arquivo, desenhados independentemente pelo renderizador CLX nativo.
- Render hardware em terceira/primeira pessoa, ataque segurado mirando no ar, labels nativas ON/OFF e coleta E pelo protocolo nativo.

O checkpoint H6 congela 737 arquivos de Source e confere 1.704 arquivos protegidos. A entrega passou VerifyOnly com **229 arquivos**: executável normal e 228 assets. A fixture e o EXE normal são separados; não usar o número de checks como aprovação visual, teste físico de UI ou FPS sustentado. O contador observou aproximadamente 18,5 MB de leitura na fixture, incluindo comparadores; não representa o consumo completo de RAM nem um benchmark de gameplay.

O pacote fornecido é incompleto. Há Guerreiro leve com espada/escudo/desarmado, mas faltam Ladina, Feiticeiro, outras armaduras e equipamentos, diversas famílias de criaturas, efeitos e materiais de outros ambientes. Paredes e composição de Tristram ainda não foram substituídas por este importador. Logo, fundos e créditos já selecionados foram preservados. `COBERTURA-HD.json`, ao lado do launcher, lista os recursos e gaps concretos.

A UI, renderização 2D e volumes derivados usam a imagem nova na resolução lógica atual; billboards 3D usam textura 3x e os pisos usam 128×128. A composição ainda usa a paleta nativa de 256 índices. **Não é uma migração completa para RGB/PBR nem toda a interface em 4x.** O MPQ legítimo continua necessário para simulação, mapas, sons e recursos ausentes.

O escopo qualificado é o primeiro andar normal. Andares2–4 e Rei Esqueleto permanecem na fila de qualificação própria: R30 falhou no retorno do setlevel, e R31 terminou com access violation durante render hardware sem pacote HD. H6 não resolve nem reclassifica esses recibos. Próxima ação: revisão humana desta entrega, conferência das correspondências e da UI, seguida da mesma fila de Catedral; sem refazer modelos aprovados ou promover WIP do repo principal.
