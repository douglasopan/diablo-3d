# Configurações durante a partida

Revisão solicitada em 8 de outubro de 2026 após a primeira instalação: remover da partida as opções que não podem ser alteradas nela, mostrar mais opções e melhorar a legibilidade e a coerência visual entre Esc e Configurações. O navegador continua usando o modelo nativo, com trilha sonora acessível e sem alterar o HUD principal, os assets selecionados ou a simulação.

**Estado atual: correção de convivência com o HUD compilada, validada e instalada pelo principal às 19:51 (Brasília), 8 de outubro.** O chat principal mantém CMake/build/cache, perfil, instalação, Git, recibo do executável e o guia operacional. A revisão inicial foi instalada às 10:45:49 UTC; os resultados abaixo distinguem os incrementos históricos. Nenhum perfil habitual foi alterado pela frente do menu.

## Convivência com o HUD HD — 8 de outubro

Alternar Gameplay 1→2→1 podia mudar a arte inferior: a página longa ainda usava o topo do painel nativo de 128 px, cruzava os globos escalados e acionava o gate conservador do compositor HD. A segunda página menor podia conservar a arte. O teste anterior à correção falhou no cruzamento real em Full HD; a fixture composta confirmou cobertura sobre o HUD e resultado `Inactive`.

`gmenu_settings_bottom()` agora fornece o mesmo limite para `GetSettingsGeometry()` e `PageCapacity()`, usando frame, globos e botões visíveis do HUD. Chat/Friendly também entram no multiplayer; sem HUD ativo, mantém o limite legado. Getters seguem o layout autorado. Desenho e cliques continuam usando os mesmos retângulos do menu; nenhuma arte, hitbox, callback, limpeza de foreground ou renderer do HUD foi alterado. A página pode mostrar menos entradas para liberar os globos, conservando fonte, todas as opções e navegação. Com layout padrão/solo, a fixture confirmou **14 entradas em 1920×1080** e **9 em 640×480**; multiplayer e overrides podem mudar a capacidade.

Release/NONET MSVC x64 passou, assim como **59.916 verificações de configurações, 2.777 de input produtivo e 946 do HUD**. O novo teste percorre páginas Gameplay em quatro resoluções, solo e multiplayer, verificando que o painel não cruza footprints visíveis; a suíte existente verifica acessibilidade das entradas, geometria, navegação, resize, captura e persistência em perfil descartável.

A comparação offscreen usou as fontes congeladas anterior/candidata com o mesmo diagnóstico, mundo original CPU e compositor SDL software: **36 quadros por versão**, 623 verificações estruturais por execução. Páginas 1→2→1 repetidas, rodapé por Enter/mouse, retorno ao jogo, personagem/inventário abrir/fechar e resize 1920→640→1920 foram exercitados. Todos os quadros elegíveis do candidato apresentaram a arte HD sem overlap ou falha de desenho. Os 13 retângulos do HUD permaneceram iguais entre versões; **14 retornos ao estado anterior tiveram zero diferenças na região inferior de pixels**. Quatro amostras antes/depois/compactas foram inspecionadas visualmente, sem novo bloqueador. O resize é lógico e offscreen, não uma janela física.

Instalado no mesmo `Iniciar-Tristram.cmd`, SHA-256 `b14993a682f85b4d3e8070ebfce59a4fddc517224ce1ee27efb7a06dd57e1692`, aliases v4/quality/godot idênticos, backup e nenhum processo encerrado. **44 arquivos do perfil** preservaram hash/tamanho/data; preparação posterior alterou somente `runtime-baseline-receipt.json`. Recibo privado: `diagnostics/hud-ui-transition-20261008/installed-20261008T225108Z/receipt.json`; comparação em `comparison-final.json` no mesmo diretório pai. Modelos, texturas, luzes, músicas, saves e preferências habituais foram preservados.

As capturas humanas da Library não ficaram acessíveis e não foram comparadas. A reprodução técnica própria não equivale à confirmação artística/interativa do usuário. Sem GPU física, input/foco físicos ou benchmark de FPS nesta entrega. Combinações arbitrárias de layout/painéis e gameplay prolongado continuam para revisão. Próxima ação: conferir Esc → Configurações → Jogabilidade, trocar páginas, abrir/fechar painéis e retornar à partida pelo iniciador habitual.

## Acesso e navegação

Durante a partida: **Esc → Configurações → Trilha sonora**. Trilha sonora é a primeira categoria quando o áudio está disponível; Gráficos, Áudio, Jogabilidade e mapeamentos vêm a seguir. Categorias restantes seguem a ordem de `GetOptions().GetCategories()`, omitindo aquelas sem entradas editáveis durante a sessão. Em inglês, a entrada da partida se chama `Settings`, como no menu inicial.

- Setas e Enter selecionam; Esc retorna um nível. No nível de categorias, volta ao menu da partida, que conserva Salvar/Carregar/saídas.
- Page Up/Page Down, roda do mouse e os botões de página percorrem a lista. A capacidade depende do espaço acima do HUD, com limite de dezoito entradas por página. Anterior, Próxima e Voltar compartilham um rodapé com retângulos separados. Home/End vão ao início/fim da página. O cursor atualiza o foco; o clique usa os mesmos retângulos do desenho, inclusive no rodapé.
- No controle, direcional/stick navegam, A/Y confirmam e B/Back/Start voltam conforme o layout nativo. Os botões de ombro mudam de página. Botões sem função no menu não acionam itens ou ações da partida.
- Booleanos alternam; listas de até duas escolhas alternam como no Settings inicial. Listas maiores abrem escolhas explícitas. Trilha sonora sempre abre a lista explícita, mesmo com duas variantes.
- Música, Som, Dicas sonoras, Gamma e Velocidade usam páginas de slider. Enter alterna seus extremos como no menu nativo; setas/clique/arraste ajustam o valor. Velocidade fica em Jogabilidade somente no solo. Áudio e Trilha sonora são omitidos sem dispositivo inicializado; silenciar música ou efeitos conserva os controles para permitir reativá-los.

A apresentação usa fontes, cores e seletores nativos sobre uma placa central escura, compartilhada entre Esc e Configurações. Nomes e valores ocupam colunas quando há espaço. As fontes crescem com a resolução lógica, sem reduzir a letra para acomodar mais itens; título e descrição têm áreas próprias. Textos longos recebem reticências UTF-8. Uma mudança de capacidade preserva o índice absoluto do item selecionado e o retorno à entrada/categoria correspondente. Esta revisão não adiciona arte HD, novas fontes ou escala independente de toda a interface. A revisão técnica das capturas está registrada abaixo; não equivale à aprovação artística final do usuário.

| Resolução lógica | Entradas úteis por página | Fonte dos itens / título |
| --- | --- | --- |
| 640×480 e 853×480 | 9 | 12 / 24 |
| 960×540 | 8 | 24 / 30 |
| 1280×720 | 13 | 24 / 30 |
| 1920×1080 | 18 | 30 / 42 |
| 1440p com largura a partir de 960 | 18 | 42 / 46 |

Esta tabela registra as capacidades históricas calculadas com o limite nativo. A correção atual usa o limite visível do HUD descrito acima; em Full HD/solo são 14 entradas, mantendo fonte 30/título 42. O menu Esc mantém seu logo e seletores de pausa, ajustando o tier nativo ao espaço disponível. Sliders conservam as dimensões e sprites originais; o bloqueio de navegação por stick durante a captura não impede clicar em Voltar para cancelá-la.

## Modelo e cobertura

`Source/ingame_settings.cpp` consome as categorias, entradas, traduções, valores e setters de `options.h/.cpp`. Não registra ou substitui callbacks. A única ordenação especial coloca as categorias mais usadas na primeira página. Opções futuras, como os novos campos da câmera, aparecem automaticamente quando incluídas em `GetEntries()` com suas flags corretas.

O filtro preserva `Invisible`, `OnlyDiablo`, `OnlyHellfire` e `NeedDiabloMpq`, com a mesma consulta a `HaveIntro()` do Settings inicial. Também omite opções bloqueadas, adiadas ou sem alternativas reais. Categorias e entradas usam o mesmo predicado, incluindo os cinco controles herdados invisíveis explicitamente adaptados: os três volumes, Gamma e Velocidade. O Settings principal e as flags do modelo não são modificados. Não se expõem IDs de herói, dados de perfil, rede/chat sem entradas ou seleções artísticas de modelos.

| Categoria | Cobertura e aplicação |
| --- | --- |
| Trilha sonora | Modo Vanilla/Rock/Custom e os oito ambientes; escolhas, nomes e disponibilidade vêm de `MusicOptions`. |
| Gráficos | Entradas editáveis, incluindo GPU, descarte espacial, câmera, filtro, zoom, luz/ciclagem e FPS; Gamma usa o handler original. Zoom também recalcula o viewport, como os atalhos nativos. Iniciar em 3D e reconstruções de interface ficam no menu principal. |
| Áudio | Volumes separados de música/efeitos/dicas e sons de caminhada/equipamento/coleta quando há áudio inicializado. Dispositivo, frequência, canais, buffer e resampling ficam no menu principal. |
| Jogabilidade | Preferências aplicáveis à sessão, incluindo HUD, coleta/equipamento, poções, lojas e magia; Velocidade conserva seu handler solo. Regras de inicialização e entradas sem efeito na sessão são omitidas. |
| Teclas/mouse e controle | Todas as ações registradas pelo jogo, com valor atual, associar, desvincular e voltar. Sem lista paralela de nomes de ações. |
| Idioma, mods, modo de jogo e inicialização | Permanecem no Settings principal. Categorias sem nenhuma entrada editável não aparecem durante a partida. |

## Restrições e persistência

| Contrato | Comportamento durante a partida |
| --- | --- |
| `CantChangeInGame` | Omitida; nenhum setter é chamado. |
| `CantChangeInMultiPlayer` | Omitida durante multiplayer. |
| `RecreateUI` | Omitida mesmo quando não possui `CantChangeInGame`. |
| Iniciar em 3D | Omitida; F4 mantém seu contrato temporário e a preferência inicial permanece no menu principal. |
| Correr em Tristram, fogo amigo e quests completas MP | Omitidas; não reescrevem o snapshot de regras em `sgGameInitInfo`. |
| Informações do grupo MP | Omitida: não tem efeito no solo e é bloqueada em multiplayer. |
| Intro/splash | Categoria de inicialização omitida. |
| Música/áudio | Handlers nativos; categorias omitidas sem áudio inicializado. Mute não bloqueia alteração. |
| Listas com zero ou uma escolha | Omitidas por não oferecerem mudança útil. |
| Demais entradas sem restrição | Setter existente, redesenho e gravação da preferência; efeitos específicos continuam a cargo do contrato de cada opção. |

Gravações usam `SaveOptions()` no perfil corrente e são suprimidas em playback de demo, como no menu herdado. Não se edita o INI diretamente nem se muda um perfil habitual durante os testes. O navegador não consome RNG da simulação. A velocidade solo usa os mesmos limites 20–50 e a mesma atualização de `gnTickDelay` que o menu anterior.

## Trilha sonora

O navegador usa exclusivamente as nove entradas de `MusicOptions`; o antigo seletor manual foi removido do `gamemenu.cpp`. Não há outra preferência ou ID para a mesma faixa. As escolhas incluem Menu Original/Rock2/alternativa e Tristram Original/1/2/Aleatório/3. Os cinco valores de Tristram cabem em uma página compacta com Voltar. Ninho e Cripta continuam visíveis no catálogo sem desbloquear a expansão.

O valor e a marca `*` refletem a política efetiva de Vanilla/Rock/Custom. Uma escolha individual usa o setter existente, que ativa Custom. Aleatório mantém ID 3; Tristram 3 mantém ID 4. Refresh, continuidade de um arquivo/política idênticos, fallback, mute, volume, contexto, EOF e sorteio independente por fim de música permanecem na engine de áudio, sem nova implementação no menu. Arquivos de música continuam privados. Contrato e evidências anteriores em [MUSIC.md](MUSIC.md) e [MUSIC-CATALOG.md](MUSIC-CATALOG.md).

## Captura e ciclo de vida

O navegador instala um wrapper do handler de eventos enquanto está aberto. Captura de tecla/mouse/controle ocorre antes do dispatch de ações da partida. A associação usa os setters nativos e suas regras de conflito. A tecla e seu release/repeat são consumidos; o clique que inicia captura limpa o estado de botão pressionado. Esc ou Voltar por clique cancelam, conservando o vínculo anterior. A captura tem limite de dez segundos; o controle confirma a combinação quando os botões são soltos. Desvincular utiliza o valor nativo vazio.

Voltar ao menu da partida ou `gamemenu_off()` restaura o handler anterior. Um handler temporário já instalado por outra operação é preservado. **Integração realizada em `diablo.cpp`:** inclusão de `ingame_settings.h` e chamada de `CloseInGameSettings()` em `RunGameLoop`, imediatamente antes da restauração final de `SetEventHandler(previousHandler)`. O evento de fechar a janela é processado antes do handler de jogo, portanto precisa desse encerramento explícito; não basta interceptar Quit no wrapper. O teste nativo confirmou que o encerramento explícito restaura o handler anterior.

## Validação e entrega desta revisão

O principal compilou o candidato após adicionar a inclusão mínima de `control/control.hpp` em `ingame_settings.cpp`, necessária à declaração de `GetMainPanel()`. Navegador, desenho e testes permanecem congelados para entrega. Revisão independente do filtro, índices e ciclo de captura e `git diff --check` também passaram.

O principal adaptou `tools/ingame_settings_smoke.cpp` e seu helper geométrico para verificar ausência das opções indisponíveis, preservação do Settings principal, capacidade responsiva e retângulos do rodapé. O fixture musical agora distingue áudio disponível de mute e respeita a nova paginação; o teste legado de vídeo passou a localizar opções por nome e página, sem posições fixas.

| Validação desta revisão | Resultado | Evidência |
| --- | --- | --- |
| `ingame_settings_smoke` | **59.452 verificações aprovadas; zero falhas** | [ingame-settings-r2.log](../../diagnostics/render-optimization-20261008/ingame-settings-r2.log) |
| `music_selection_smoke` | **540 verificações aprovadas; zero falhas** | [music-selection-r2.log](../../diagnostics/render-optimization-20261008/music-selection-r2.log) |
| Captura nativa `--ingame-menu-visual` | **20 PNGs em 960×540 e 1920×1080; sem falhas** | [relatório de geometria e capturas](../../diagnostics/render-optimization-20261008/menu-r1/ingame-menu-visual.txt), [checks nativos](../../diagnostics/render-optimization-20261008/menu-r1/town-view-smoke.txt) |

Os testes cobriram filtro em Diablo/Hellfire × solo/multiplayer × áudio disponível/indisponível, mute reversível, persistência das escolhas editáveis e omitidas, páginas/rodapé, resize, foco, mouse/roda, teclado/controle, sliders, captura de bindings, encerramento do wrapper e preservação do RNG. Configurações usou INI temporário, sem janela, arquivo do jogo, perfil habitual ou save; seleção musical também não carregou assets do jogo ou saves.

As capturas usaram o desenho real de `gmenu`, fontes/logo/seletores dos arquivos locais, catálogo `pt_BR` e fundo nativo de Tristram. Cada página foi capturada com foco no primeiro e no último item; todas registraram `outsidePanel=0` e proteção das linhas externas. A captura executou somente navegação/foco, sem ativar valores de opções ou saves. **O HUD principal não foi desenhado:** estas imagens validam exclusivamente Esc e Configurações, sem concluir a frente do HUD.

Foram inspecionadas nesta frente as imagens de [Esc em 960×540](../../diagnostics/render-optimization-20261008/menu-r1/960x540-escape-focus-first.png), [Gráficos em 960×540](../../diagnostics/render-optimization-20261008/menu-r1/960x540-graphics-page-1-focus-first.png) e [Trilha sonora em 1920×1080](../../diagnostics/render-optimization-20261008/menu-r1/1920x1080-music-page-1-focus-first.png). Fontes, foco, colunas e rodapé estão legíveis, sem transbordamento visível nessas três composições. O catálogo português está ativo, com os fallbacks ingleses existentes para textos sem tradução. As capturas são renders nativos finitos, não screenshots de uma janela ou comprovação de uma partida prolongada.

**Entrega instalada:** o executável SHA-256 `a1e218fab604517f409557a5863352633b632f1f83e3f3c449b06393418140c9` foi aplicado pelo principal aos três aliases do iniciador habitual, com backup e zero partidas abertas. Os 39 arquivos do perfil foram preservados por hash/tamanho/data, exceto a associação vazia da câmera preenchida com K após confirmar a tecla livre. Recibo privado: `diagnostics/render-optimization-20261008/installed-20261008T104549Z/receipt.json`. Os testes acima pertencem aos bytes instalados; não representam aprovação artística integral do usuário.

## Validação histórica da primeira versão instalada

Arquivos implementados nesta frente:

- `Source/ingame_settings.cpp/.h`: navegador, opções, aplicação, captura e wrapper.
- `Source/gamemenu.cpp`: entrada Settings e remoção das tabelas/setters duplicados; salvar/carregar/saídas preservados.
- `Source/gmenu.cpp/.h`: apresentação opcional, geometria, foco/input e valores por linha; menus legados conservam sua apresentação.
- `tools/ingame_settings_smoke.cpp` e `tools/ingame_settings_geometry_checks.hpp`: fixtures finitos.
- `tools/music_selection_smoke.cpp`: navegação atualizada para o seletor único.
- `Translations/pt_BR.po`: 21 novos textos traduzidos; demais catálogos usam o fallback existente para novos msgids.

O integrador adicionou `Source/ingame_settings.cpp` à biblioteca e criou `ingame_settings_smoke`, ligado a `libdevilutionx`, com `/EHsc` no MSVC como os smokes existentes. A compilação integrada exigiu somente a inclusão mínima de `controls/control_mode.hpp` no navegador, para declarar `GamepadType`. O candidato compilou após esse reparo.

O fixture executado usou diretório TEMP recém-criado, SDL dummy, handlers/setters reais e nenhuma janela, MPQ, mundo, perfil habitual ou save. Comparou visibilidade e bloqueios em Diablo/Hellfire × solo/multiplayer, percorreu páginas/voltar e verificou Boolean/List, sliders, bindings de tecla/mouse/roda, combinação num controle virtual, gravação/releitura de INI e preservação do RNG. A captura no controle virtual passou, sem skip. O helper geométrico verificou os retângulos reais em 640×480, 853×480, 1080p e ultrawide/1440p. O teste de seleção musical percorreu os modos e cada variante dos oito ambientes; reprodução usou somente áudio gerado.

A primeira execução de configurações revelou uma referência de nome invalidada no helper de paginação do próprio teste: `Action::GetName()` reutiliza sua string dinâmica durante a navegação. O helper passou a copiar o nome solicitado antes de mudar de página, com regressões explícitas para `QuickSpell10` e `QuickMessage10`. O navegador já mantinha cópias próprias dos nomes; esse reparo não exigiu mudança de produção. Após recompilar somente esse teste, os três resultados do candidato anterior foram:

| Teste nativo | Resultado | Evidência |
| --- | --- | --- |
| `ingame_settings_smoke` | **36.612 verificações aprovadas; zero falhas** | [ingame-settings.log](../../diagnostics/camera-apply-20261008/ingame-settings.log) |
| `music_selection_smoke` | **535 verificações aprovadas; zero falhas** | [music-selection.log](../../diagnostics/camera-apply-20261008/music-selection.log) |
| `music_playback_smoke` | **55 verificações aprovadas; zero falhas** | [music-playback.log](../../diagnostics/camera-apply-20261008/music-playback.log) |

A comparação exaustiva anterior confirmou a cobertura das entradas visíveis do modelo, incluindo campos somente leitura. Essa política foi substituída pelo novo requisito; os totais seguintes são históricos e não são metas da lista editável. Os cinco controles invisíveis adaptados foram verificados separadamente:

| Contexto | Entradas visíveis | Somente leitura |
| --- | --- | --- |
| Diablo solo | 211 | 20 |
| Diablo multiplayer | 211 | 24 |
| Hellfire solo | 217 | 25 |
| Hellfire multiplayer | 217 | 29 |

**Verificado nesta frente:** revisão de contrato, índices, ownership de strings, callbacks, input e diff sem whitespace; os 21 novos msgids são únicos, compilaram pelo fallback real e retornaram as traduções pt-BR esperadas num catálogo temporário, removido ao fim. Settings/Trilha sonora conservaram as traduções. A integração confirmou compilação C++ e os resultados nativos registrados acima, inclusive persistência/releitura, encerramento do wrapper e preservação do RNG.

**Limites históricos:** a instalação anterior conferiu os três aliases e preservou os 26 arquivos do perfil por hash, tamanho e data; a preparação posterior renovou somente o recibo. Esses fatos pertencem à versão anterior. A instalação atual e suas validações estão registradas acima. Geometria e eventos aprovados em SDL dummy não constituem aprovação artística nem medição de partida prolongada.
