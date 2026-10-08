# Configurações durante a partida

Requisito novo de 8 de outubro de 2026: tornar o seletor de músicas evidente na partida e levar ao menu ingame o máximo de preferências do Settings inicial. A entrega substitui o pequeno conjunto manual de áudio/vídeo/trilha por um navegador único do modelo de opções; não altera o HUD principal, a escala global, os assets selecionados ou a simulação.

**Estado: integrado com a câmera, compilado, validado nos três testes nativos abaixo e instalado pelo chat principal em 8 de outubro de 2026.** O iniciador habitual continua único. A instalação conferiu os três aliases e preservou os 26 arquivos do perfil por hash, tamanho e data; a preparação posterior renovou somente o recibo de execução. Esta frente não alterou o perfil diretamente. A revisão visual do menu durante a partida permanece pendente.

## Acesso e navegação

Durante a partida: **Esc → Configurações → Trilha sonora**. Trilha sonora é a primeira categoria; Gráficos, Áudio, Jogabilidade e mapeamentos vêm a seguir. Categorias restantes seguem a ordem de `GetOptions().GetCategories()`, omitindo somente aquelas sem entradas visíveis. Em inglês, a entrada da partida agora se chama `Settings`, como no menu inicial.

- Setas e Enter selecionam; Esc retorna um nível. No nível de categorias, volta ao menu da partida, que conserva Salvar/Carregar/saídas.
- Page Up/Page Down, roda do mouse e os botões de página percorrem cinco entradas por página. Home/End vão ao início/fim da página. O cursor atualiza o foco; o clique usa os mesmos retângulos do desenho.
- No controle, direcional/stick navegam, A/Y confirmam e B/Back/Start voltam conforme o layout nativo. Os botões de ombro mudam de página. Botões sem função no menu não acionam itens ou ações da partida.
- Booleanos alternam; listas de até duas escolhas alternam como no Settings inicial. Listas maiores abrem escolhas explícitas. Trilha sonora sempre abre a lista explícita, mesmo com duas variantes.
- Música, Som, Dicas sonoras, Gamma e Velocidade usam páginas de slider. Enter alterna seus extremos como no menu nativo; setas/clique/arraste ajustam o valor. Velocidade fica em Jogabilidade e continua bloqueada em multiplayer.

A apresentação usa uma placa central com fontes nativas, título, nome/valor de cada entrada e descrição acima do painel. São no máximo oito linhas: cinco entradas, duas ações de página e Voltar. A geometria é compartilhada com input; textos longos recebem reticências UTF-8, sem sair horizontalmente da placa. A descrição prioriza aplicação/motivo de bloqueio. Fontes pequenas na resolução lógica alta continuam uma limitação, sem introduzir escala independente de interface. A fidelidade visual precisa de revisão do candidato renderizado.

## Modelo e cobertura

`Source/ingame_settings.cpp` consome as categorias, entradas, traduções, valores e setters de `options.h/.cpp`. Não registra ou substitui callbacks. A única ordenação especial coloca as categorias mais usadas na primeira página. Opções futuras, como os novos campos da câmera, aparecem automaticamente quando incluídas em `GetEntries()` com suas flags corretas.

O filtro preserva `Invisible`, `OnlyDiablo`, `OnlyHellfire` e `NeedDiabloMpq`, com a mesma consulta a `HaveIntro()` do Settings inicial. Somente cinco controles herdados invisíveis são explicitamente adaptados: os três volumes, Gamma e Velocidade. Não se expõem IDs de herói, dados de perfil, rede/chat sem entradas ou seleções artísticas de modelos.

| Categoria | Cobertura e aplicação |
| --- | --- |
| Trilha sonora | Modo Vanilla/Rock/Custom e os oito ambientes; escolhas, nomes e disponibilidade vêm de `MusicOptions`. |
| Gráficos | Todas as entradas visíveis, incluindo 3D, filtro, zoom, luz/ciclagem e FPS; Gamma usa o handler original. Zoom também recalcula o viewport, como os atalhos nativos. |
| Áudio | Volumes separados de música/efeitos/dicas, sons de caminhada/equipamento/coleta e todas as entradas técnicas visíveis, com as restrições originais. |
| Jogabilidade | Todas as preferências visíveis, incluindo HUD, coleta/equipamento, poções, lojas, magia e opções condicionais da expansão; Velocidade conserva seu handler solo. |
| Teclas/mouse e controle | Todas as ações registradas pelo jogo, com valor atual, associar, desvincular e voltar. Sem lista paralela de nomes de ações. |
| Idioma, mods, modo de jogo e inicialização | Entradas visíveis do modelo; bloqueadas quando exigem reconstrução ou mudança de sessão. Intro/splash podem ser salvos para a próxima inicialização. |

## Restrições e persistência

| Contrato | Comportamento durante a partida |
| --- | --- |
| `CantChangeInGame` | Entrada consultável; abre detalhes com motivo e Voltar. Nenhum setter é chamado. |
| `CantChangeInMultiPlayer` | Mesmo bloqueio durante multiplayer; solo respeita a implementação existente. |
| `RecreateUI` | Bloqueada mesmo quando não possui `CantChangeInGame`. Evita recriar renderer, idioma, modo ou mods dentro da sessão sem suporte validado. |
| Iniciar em 3D | Grava preferência para a próxima sessão; conserva a câmera/F4 atual. |
| Correr em Tristram, fogo amigo e quests completas MP | Preferências salvas para a próxima sessão. Não reescreve o snapshot de regras em `sgGameInitInfo`. No multiplayer, suas flags impedem alteração. |
| Intro/splash | Salvos para a próxima inicialização do programa. |
| Música/áudio disponível | Aplicação pelos handlers nativos; ausência de dispositivo deixa volume consultável com motivo. |
| Demais entradas sem restrição | Setter existente, redesenho e gravação da preferência; efeitos específicos continuam a cargo do contrato de cada opção. |

Gravações usam `SaveOptions()` no perfil corrente e são suprimidas em playback de demo, como no menu herdado. Não se edita o INI diretamente nem se muda um perfil habitual durante os testes. O navegador não consome RNG da simulação. A velocidade solo usa os mesmos limites 20–50 e a mesma atualização de `gnTickDelay` que o menu anterior.

## Trilha sonora

O navegador usa exclusivamente as nove entradas de `MusicOptions`; o antigo seletor manual foi removido do `gamemenu.cpp`. Não há outra preferência ou ID para a mesma faixa. As escolhas incluem Menu Original/Rock2/alternativa e Tristram Original/1/2/Aleatório/3. Os cinco valores de Tristram cabem em uma página compacta com Voltar. Ninho e Cripta continuam visíveis no catálogo sem desbloquear a expansão.

O valor e a marca `*` refletem a política efetiva de Vanilla/Rock/Custom. Uma escolha individual usa o setter existente, que ativa Custom. Aleatório mantém ID 3; Tristram 3 mantém ID 4. Refresh, continuidade de um arquivo/política idênticos, fallback, mute, volume, contexto, EOF e sorteio independente por fim de música permanecem na engine de áudio, sem nova implementação no menu. Arquivos de música continuam privados. Contrato e evidências anteriores em [MUSIC.md](MUSIC.md) e [MUSIC-CATALOG.md](MUSIC-CATALOG.md).

## Captura e ciclo de vida

O navegador instala um wrapper do handler de eventos enquanto está aberto. Captura de tecla/mouse/controle ocorre antes do dispatch de ações da partida. A associação usa os setters nativos e suas regras de conflito. A tecla e seu release/repeat são consumidos; o clique que inicia captura limpa o estado de botão pressionado. Esc ou Voltar por clique cancelam, conservando o vínculo anterior. A captura tem limite de dez segundos; o controle confirma a combinação quando os botões são soltos. Desvincular utiliza o valor nativo vazio.

Voltar ao menu da partida ou `gamemenu_off()` restaura o handler anterior. Um handler temporário já instalado por outra operação é preservado. **Integração realizada em `diablo.cpp`:** inclusão de `ingame_settings.h` e chamada de `CloseInGameSettings()` em `RunGameLoop`, imediatamente antes da restauração final de `SetEventHandler(previousHandler)`. O evento de fechar a janela é processado antes do handler de jogo, portanto precisa desse encerramento explícito; não basta interceptar Quit no wrapper. O teste nativo confirmou que o encerramento explícito restaura o handler anterior.

## Validação do candidato e entrega

Arquivos implementados nesta frente:

- `Source/ingame_settings.cpp/.h`: navegador, opções, aplicação, captura e wrapper.
- `Source/gamemenu.cpp`: entrada Settings e remoção das tabelas/setters duplicados; salvar/carregar/saídas preservados.
- `Source/gmenu.cpp/.h`: apresentação opcional, geometria, foco/input e valores por linha; menus legados conservam sua apresentação.
- `tools/ingame_settings_smoke.cpp` e `tools/ingame_settings_geometry_checks.hpp`: fixtures finitos.
- `tools/music_selection_smoke.cpp`: navegação atualizada para o seletor único.
- `Translations/pt_BR.po`: 21 novos textos traduzidos; demais catálogos usam o fallback existente para novos msgids.

O integrador adicionou `Source/ingame_settings.cpp` à biblioteca e criou `ingame_settings_smoke`, ligado a `libdevilutionx`, com `/EHsc` no MSVC como os smokes existentes. A compilação integrada exigiu somente a inclusão mínima de `controls/control_mode.hpp` no navegador, para declarar `GamepadType`. O candidato compilou após esse reparo.

O fixture executado usou diretório TEMP recém-criado, SDL dummy, handlers/setters reais e nenhuma janela, MPQ, mundo, perfil habitual ou save. Comparou visibilidade e bloqueios em Diablo/Hellfire × solo/multiplayer, percorreu páginas/voltar e verificou Boolean/List, sliders, bindings de tecla/mouse/roda, combinação num controle virtual, gravação/releitura de INI e preservação do RNG. A captura no controle virtual passou, sem skip. O helper geométrico verificou os retângulos reais em 640×480, 853×480, 1080p e ultrawide/1440p. O teste de seleção musical percorreu os modos e cada variante dos oito ambientes; reprodução usou somente áudio gerado.

A primeira execução de configurações revelou uma referência de nome invalidada no helper de paginação do próprio teste: `Action::GetName()` reutiliza sua string dinâmica durante a navegação. O helper passou a copiar o nome solicitado antes de mudar de página, com regressões explícitas para `QuickSpell10` e `QuickMessage10`. O navegador já mantinha cópias próprias dos nomes; esse reparo não exigiu mudança de produção. Após recompilar somente esse teste, os três resultados do mesmo candidato integrado foram:

| Teste nativo | Resultado | Evidência |
| --- | --- | --- |
| `ingame_settings_smoke` | **36.612 verificações aprovadas; zero falhas** | [ingame-settings.log](../../diagnostics/camera-apply-20261008/ingame-settings.log) |
| `music_selection_smoke` | **535 verificações aprovadas; zero falhas** | [music-selection.log](../../diagnostics/camera-apply-20261008/music-selection.log) |
| `music_playback_smoke` | **55 verificações aprovadas; zero falhas** | [music-playback.log](../../diagnostics/camera-apply-20261008/music-playback.log) |

A comparação exaustiva confirmou a cobertura das entradas visíveis do modelo, incluindo os campos de câmera integrados. Os cinco controles invisíveis adaptados são verificados separadamente e não entram nestes totais:

| Contexto | Entradas visíveis | Somente leitura |
| --- | --- | --- |
| Diablo solo | 211 | 20 |
| Diablo multiplayer | 211 | 24 |
| Hellfire solo | 217 | 25 |
| Hellfire multiplayer | 217 | 29 |

**Verificado nesta frente:** revisão de contrato, índices, ownership de strings, callbacks, input e diff sem whitespace; os 21 novos msgids são únicos, compilaram pelo fallback real e retornaram as traduções pt-BR esperadas num catálogo temporário, removido ao fim. Settings/Trilha sonora conservaram as traduções. A integração confirmou compilação C++ e os resultados nativos registrados acima, inclusive persistência/releitura, encerramento do wrapper e preservação do RNG.

**Ainda não verificado nesta frente:** render/capturas do menu, legibilidade no jogo, partida prolongada e instalação no perfil habitual. Geometria e eventos aprovados em SDL dummy não constituem aprovação visual. O chat principal deve completar o recibo e a revisão do executável, registrar os limites de apresentação e confirmar a instalação; esta documentação não atribui esses resultados ao jogo anteriormente instalado.
