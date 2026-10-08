# Auditoria de opções e menus da partida

Estudo realizado em 8 de outubro de 2026, por leitura do código local. O escopo é documentação e exemplos visuais; não houve execução do jogo, alteração de perfil, implementação, staging ou commit.

Os **56 exemplos** de [data/options.json](data/options.json) cobrem **34 telas/estados de Configurações (O01–O34)** e **22 telas/estados da Partida (G01–G22)**. Cada entrada contém campos, ações, condição, notas e referência de arquivo/linha. Títulos e valores estão em português; os valores de seleção, vínculos e equipamentos de áudio são fictícios para revisão.

“Atual” significa comportamento encontrado no código, sem validação no executável. “Condicional” identifica requisitos de plataforma, recursos ou multiplayer. O estudo não comprova arte HD, conteúdo 3D fora de Tristram nem funcionamento de rede no build local.

## Estrutura existente e regra de cobertura

A interface de Configurações tem cinco layouts nativos: categorias, opções da categoria, escolha de valor, captura de tecla/mouse e captura do controle. A seleção de valor só abre subtela quando a lista possui **mais de duas opções**; listas de até duas entradas alternam diretamente.

Fontes: [settingsmenu.cpp:60](../../Source/DiabloUI/settingsmenu.cpp#L60), [settingsmenu.cpp:286](../../Source/DiabloUI/settingsmenu.cpp#L286), [settingsmenu.cpp:407](../../Source/DiabloUI/settingsmenu.cpp#L407).

O layout atual centraliza uma largura lógica de 640–720 pixels, usa lista com linhas de 26 pixels e reserva uma região inferior para a descrição. Textos longos podem ocupar duas linhas. A proposta visual do estudo pode ampliar leitura e distribuição, preservando os campos e as regras encontrados.

Fontes: [settingsmenu.cpp:372](../../Source/DiabloUI/settingsmenu.cpp#L372), [settingsmenu.cpp:557](../../Source/DiabloUI/settingsmenu.cpp#L557).

## Categorias e escolhas individuais

| Exemplos | Cobertura comprovada |
| --- | --- |
| O01 | Categorias visíveis na ordem do modelo; Mods e Modo de jogo com condição explícita. |
| O02–O03 | Categoria Idioma e lista completa de idiomas conhecidos, com fontes extras identificadas. |
| O04 | Mods dinâmicos. Os dois nomes do exemplo são fictícios; não reproduzem instalação local. |
| O05 | Modo de jogo: restringir para Shareware. Seletor interno Diablo/Hellfire é Invisible e não aparece como campo. |
| O06–O07 | Inicialização: introdução do modo ativo e três escolhas de splash. |
| O08–O12 | Todos os campos gráficos visíveis; seletores de resolução, filtro, controle de frames e tamanho do cursor. |
| O13–O17 | Todos os campos de Áudio; seletores de taxa, buffer, qualidade Speex e dispositivos. |
| O18–O20 | Trilha: tema, oito locais e três variantes do Menu. Demais locais alternam Original/Rock diretamente nessa categoria. |
| O21–O22 | Gameplay completo de Diablo e Hellfire, com as cinco entradas exclusivas da expansão. |
| O23 | As três interfaces de loja existentes. |
| O24–O29 | Seis telas individuais para quantidades de poções; 0, 1, 2, 4, 8 e 16 em cada uma. |
| O30 | Todas as 65 ações normais registradas para teclas/mouse, incluindo oito itens do cinto, 12 magias e dez mensagens rápidas. |
| O31 | Todas as 68 ações registradas do controle, inclusive vínculos duplicados de clique/menu. |
| O32 | Captura de tecla/mouse: associação, instrução, desvincular e voltar. |
| O33–O34 | Captura do controle em repouso e em andamento; janela nativa de dez segundos e navegação temporariamente oculta. |

Fontes de campos: [options.cpp:471](../../Source/options.cpp#L471), [options.cpp:491](../../Source/options.cpp#L491), [options.cpp:550](../../Source/options.cpp#L550), [options.cpp:586](../../Source/options.cpp#L586), [options.cpp:830](../../Source/options.cpp#L830), [options.cpp:985](../../Source/options.cpp#L985), [options.cpp:1119](../../Source/options.cpp#L1119), [options.cpp:1662](../../Source/options.cpp#L1662). A ordem global está em [options.h:930](../../Source/options.h#L930).

### Gameplay completo

O21 contém fogo amigo, quests completas no multiplayer, quests aleatórias, correr em Tristram, lançamento rápido, experiência, informação flutuante de item, interface de lojas, valores de vida/mana, grupo, vida/tipo de inimigo, rótulos de itens e reabastecimento do cinto. Também contém cinco opções de equipamento automático, coleta automática de ouro/elixires, seis quantidades de poções, coleta em Tristram, bloqueio de santuários prejudiciais, captura do mouse e pausa ao perder foco.

O22 acrescenta Theo, Jersey, Bardo, Bárbaro e coleta automática de óleos. A galeria deve permitir rolagem sem ocultar opções do conjunto de dados. Velocidade e limiar para omitir carregamento são Invisible nessa categoria e não foram apresentados.

### Valores e disponibilidade

- Resoluções e dispositivos são enumerados no runtime. O09/O17 usam valores explicitamente rotulados como exemplos; não afirmam uma lista fixa suportada.
- O seletor de dispositivos aparece com mais de duas entradas. Com até duas, alterna na própria categoria.
- O resampler atual admite no máximo Speex e SDL, conforme flags de compilação; portanto não foi inventada uma subtela de três escolhas. Qualidade 0–5 aparece quando Speex está ativo.
- Cursores de hardware, filtros, escala inteira, tela cheia e controle de frames dependem de backend/plataforma. O08 e os seletores contêm essas condições.
- Idiomas asiáticos dependem de fontes extras; um idioma personalizado do perfil pode ampliar a lista.
- O gráfico “Iniciar em 3D” vale ao iniciar/carregar. GPU/suavização podem afetar o próximo desenho, com fallback CPU para GPU indisponível. Resolução e outros campos possuem restrições de alteração/recriação de interface.
- Música usa apenas nomes genéricos encontrados no código. Nenhuma faixa, nome de arquivo ou metadado privado acompanha o estudo.

Fontes: [options.cpp:127](../../Source/options.cpp#L127), [options.cpp:638](../../Source/options.cpp#L638), [options.cpp:723](../../Source/options.cpp#L723), [options.cpp:733](../../Source/options.cpp#L733), [options.cpp:887](../../Source/options.cpp#L887), [options.cpp:1119](../../Source/options.cpp#L1119).

## Menus e estados na partida

| Exemplos | Conteúdo encontrado |
| --- | --- |
| G01 | Solo: Opções, Salvar, Carregar, Sair para menu principal, Sair do jogo. |
| G02 | Multiplayer: Opções e duas saídas; sem Salvar/Carregar. |
| G03–G04 | Opções rápidas solo/multi: áudio, vídeo e velocidade. |
| G05 | Música e Som com slider/mute; acesso à trilha. |
| G06 | GPU, suavização, iniciar em 3D e Gamma. |
| G07 | Tema da trilha e acesso aos locais. |
| G08–G10 | As três páginas de locais: Menu/Tristram/Catedral; Catacumbas/Cavernas/Inferno; Ninho/Cripta. |
| G11–G18 | Um exemplo individual de variantes para cada um dos oito locais. |
| G19 | Pausa simples, separada do menu com opções. |
| G20–G22 | Salvando, Carregando e Jogo salvo; mensagens sobre a partida. |

Fontes: [gamemenu.cpp:73](../../Source/gamemenu.cpp#L73), [gamemenu.cpp:94](../../Source/gamemenu.cpp#L94), [gamemenu.cpp:105](../../Source/gamemenu.cpp#L105), [gamemenu.cpp:139](../../Source/gamemenu.cpp#L139), [gamemenu.cpp:316](../../Source/gamemenu.cpp#L316), [gamemenu.cpp:348](../../Source/gamemenu.cpp#L348), [gmenu.cpp:191](../../Source/gmenu.cpp#L191).

Velocidade solo usa 20–50 ticks por segundo, em 46 posições. Multiplayer exibe a velocidade da partida sem slider habilitado. Gamma usa 0–100 em 21 posições. Música/Som usam escala nativa de -1600 a 0, em 64 passos; percentuais G05 são apresentação ilustrativa. Os submenus atuais ficam em até cinco linhas acima do painel lógico de 640×480.

Fontes: [gamemenu.cpp:204](../../Source/gamemenu.cpp#L204), [gamemenu.cpp:233](../../Source/gamemenu.cpp#L233), [gamemenu.cpp:239](../../Source/gamemenu.cpp#L239), [sound_defs.hpp:9](../../Source/engine/sound_defs.hpp#L9).

Salvar e Carregar são ações imediatas com status; não existe navegador de slots ou confirmação nesses comandos. O menu atual não possui “Retomar”; Esc fecha. Qualquer botão Retomar na proposta geral precisa ser identificado como melhoria visual/comportamental proposta. Não se deve presumir pausa do mundo multiplayer.

Fontes: [gamemenu.cpp:571](../../Source/gamemenu.cpp#L571), [gamemenu.cpp:609](../../Source/gamemenu.cpp#L609), [gmenu.cpp:299](../../Source/gmenu.cpp#L299).

## Omissões corretas e divergências encontradas

As categorias internas Diablo, Hellfire, Controller, Network e Chat não têm entradas visíveis: estão vazias ou contêm somente Invisible. O filtro omite categorias sem entradas válidas. Não há categoria atual chamada Acessibilidade; recursos relacionados estão distribuídos por Gameplay, idioma, áudio e mapeamentos.

Os volumes de música, efeitos e dicas de navegação são Invisible nas Configurações pré-partida; Música/Som aparecem no menu Áudio da partida. Não existe terceiro slider de dicas nesse submenu.

A ajuda herdada ainda descreve quatro atalhos de magia, mas o código registra 12. O30/O31 cobrem 12; a divergência fica registrada sem correção fora do escopo. Console e alternância de debug aparecem somente sob _DEBUG e constam como variante de O30.

Fontes: [options.cpp:523](../../Source/options.cpp#L523), [options.cpp:537](../../Source/options.cpp#L537), [options.cpp:552](../../Source/options.cpp#L552), [options.cpp:1030](../../Source/options.cpp#L1030), [options.cpp:1039](../../Source/options.cpp#L1039), [options.cpp:1051](../../Source/options.cpp#L1051), [settingsmenu.cpp:88](../../Source/DiabloUI/settingsmenu.cpp#L88), [player.h:44](../../Source/player.h#L44), [help.cpp:90](../../Source/help.cpp#L90), [diablo.cpp:2282](../../Source/diablo.cpp#L2282).

## Verificação e limite da entrega

O JSON foi validado estruturalmente: IDs únicos, grupos/enumerações válidos, arrays obrigatórios presentes, referências de origem com linhas positivas e arquivos existentes. A contagem das ações foi confrontada com os registros em [diablo.cpp:1928](../../Source/diablo.cpp#L1928) e [diablo.cpp:2301](../../Source/diablo.cpp#L2301).

Somente este relatório e data/options.json foram escritos por esta subtarefa. O guia central, Source, opções persistidas, áudio e executáveis permaneceram fora do escopo. As imagens e a galeria pertencem ao chat responsável pelo estudo, que deve marcar cada resultado como conceito.

