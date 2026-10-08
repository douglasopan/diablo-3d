# Controles de primeira pessoa em Tristram

Atualização: **8 de outubro de 2026**. O pedido de mouse para olhar e setas relativas à câmera foi implementado, compilado, validado tecnicamente e instalado no iniciador habitual. Continua a frente de [câmeras e horizonte](TRISTRAM-HORIZON-CAMERAS.md) na fila de [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md). A validação interativa do mouse físico continua pendente.

## Estado verificável

| Etapa | Resultado |
| --- | --- |
| Preparação | Handoff privado congelado e conferido por hash; integração exclusiva pelo principal. |
| Implementação | Política pura, adaptador produtivo, caminhada nativa, retículo e fila ordenada de cliques/olhar aplicados ao repositório. |
| Compilação | Release/NONET x64 com MSVC; jogo e diagnósticos compilados. |
| Testes | 673 verificações puras, 1.652 produtivas, 946 do HUD e 59.452 das configurações passaram. Limites abaixo. |
| Instalação | 8 de outubro, **20:30:00 UTC**: aliases v4/quality/godot idênticos, pelo mesmo `Iniciar-Tristram.cmd`; nenhum processo encerrado. |

Executável instalado: SHA-256 `30701f820a9908e4a81205e5e560acb1e901f6e29be1cead7737b58ae7c0e8f5`. Recibo privado: `diagnostics/first-person-integration-20261008/installed-20261008T202953Z/receipt.json`.

Os **41 arquivos do perfil** preservaram bytes, tamanho e data na instalação. A preparação posterior do iniciador alterou somente `runtime-baseline-receipt.json`, como esperado. Launcher, preferências, saves, assets selecionados e arquivos de outras frentes foram preservados. Este incremento não altera backend GPU, qualidade dos modelos, LOD, materiais ou iluminação; não houve benchmark de desempenho. A participação de `scrollrt.cpp` e do compositor se limita ao retículo e à máscara do cursor.

## Comportamento

Em Tristram, **F4** alterna original/3D. **K** percorre isométrica → órbita livre → terceira pessoa → primeira pessoa → isométrica; o atalho é remapeável. Essas associações foram conferidas no perfil habitual, sem alteração. **Home** restaura a câmera original.

Em Tristram com 3D e primeira pessoa ativos, teclado/mouse e sessão elegível, o adaptador solicita captura relativa uma vez. Somente a confirmação da plataforma ativa o controle. Mouse horizontal gira yaw; vertical altera pitch, limitado pelo rig a ±1,4 rad. A sensibilidade usa a opção já salva, com base de 0,006 rad por pixel a 100%, sem multiplicação por tempo ou por suavização de bordas. Não é necessário segurar o botão central.

↑/↓ avançam/recuam; ←/→ deslocam lateralmente. Combinações opostas se anulam. As demais combinações são normalizadas e quantizadas nas oito direções nativas, com margem angular de 3° enquanto a mesma combinação permanece pressionada. Pitch não inclina o deslocamento. A intenção é consumida uma vez na cadência nativa de movimento, pelo mesmo `WalkInDir`/`CMD_WALKXY` existente. Não há escrita direta de posição, caminho, animação, velocidade, colisão ou RNG.

Escape libera o mouse e segue para a ação nativa. Menus, textos, painéis, inventário, atributos, livro de magias, stash, comércio, automapa, item na mão, cursor especial, timeout, morte, transição de nível, troca de handler, saída e perda de foco suspendem a captura e limpam as setas possuídas. Captura perdida ou falha não é repetida a cada quadro. Retornar foco ou fechar a interface não recaptura automaticamente: um novo clique esquerdo ou direito dentro do mundo retoma o controle, e esse clique, incluindo sua soltura, é consumido. Deltas antigos são descartados ao adquirir ou liberar; teclas já seguradas só voltam a mover após soltura e nova pressão.

F4/Home e troca de modo conservam os contratos existentes da câmera. Sair de primeira pessoa libera a captura; uma entrada explícita posterior no modo pode solicitar captura novamente. Os outros modos continuam com seus controles existentes. Controlador/gamepad conserva a rotina nativa e suspende o adaptador ao assumir o dispositivo.

Durante a captura, o ponto de seleção é exatamente `{gnScreenWidth / 2, gnViewportHeight / 2}`, no viewport lógico do mundo. O retículo branco com contorno escuro tem 11×11 pixels, centro aberto e cobertura explícita: 12 pixels claros, 32 de contorno, 77 transparentes. A composição conserva o mundo de maior densidade atrás dos pixels transparentes e restaura o fundo ao remover o retículo. O cursor de hardware fica oculto durante captura; o cursor nativo retorna ao suspender.

Girar a câmera invalida o picking. Um clique antes do próximo desenho aguarda em uma fila limitada a 256 registros, em vez de usar alvo antigo ou cair no tile do herói. Cliques, solturas e deltas posteriores mantêm sua ordem. Depois de desenhar, o despacho usa o pick fresco do centro; um novo clique após outro giro aguarda o desenho correspondente. Deltas verticais de sinais opostos ficam separados para conservar o clamp sequencial do pitch. Suspensão limpa a fila; overflow suspende com segurança. Céu sem pick não produz comando de chão. Não há desenho síncrono adicional por clique.

Um clique de interação passa a possuir sua ação e impede que uma parada anterior de setas cancele o novo comando. O adaptador não muda `pcurs` nem substitui seleção, interação, ataque, regras de itens ou HUD. A caminhada enviada pelo adaptador possui um marcador separado; suspensão cancela somente essa caminhada, inclusive se seu comando ainda estiver na fila. O passo de animação já iniciado pode terminar normalmente. A fixture exercita seleção de chão; NPCs, itens e combate ainda precisam de revisão na partida.

## Fronteiras de implementação

| Arquivo | Responsabilidade |
| --- | --- |
| `controls/town_first_person_input.hpp/.cpp` | Máquina de estados pura e determinística, deltas de olhar, bloqueio de teclas antigas, quantização e pulsos de captura/parada. Sem SDL ou jogador. |
| `diablo.cpp/.h` | Serviços SDL, gates da sessão/interface/foco, dispatcher produtivo, ponte com câmera e APIs mínimas do diagnóstico. Sincronização antes da saída antecipada de `ProcessInput` em pausa. |
| `engine/events.cpp` | Suspender antes da troca do handler, cobrindo loops aninhados de progresso/carregamento/configuração. |
| `controls/plrctrls.cpp` | Consumir intenção na cadência nativa, reutilizar comando/colisão e cancelar apenas caminhada possuída. |
| `engine/render/scrollrt.cpp` | Retículo na fase de cursor, buffer de fundo e retorno do cursor nativo. |
| `engine/render/d3d_hud_presentation.hpp/.cpp` | Copiar pixels/máscara do retículo para o cursor do compositor sem tornar o fundo opaco. |
| `tools/town_first_person_input_smoke.cpp` | Casos puros de lifecycle, oito orientações, combinações, bordas angulares, preferências, entradas inválidas e determinismo. |
| `tools/town_first_person_runtime_smoke.cpp` e dois headers de checks | Dispatcher, movimento/comandos nativos, colisão, picking CPU e suspensões. |

Os diagnósticos são condicionados a `BUILD_TOWN_VIEW_SMOKE`. O puro não requer MPQ, GPU, saves ou janela. O produtivo recebe a instalação original, assets de build e diretório privado de saída; lê o MPQ e usa janela dummy oculta, GPU desligada e perfil temporário. Hashes dos binários/fontes e logs constam do recibo privado.

## Fixture produtiva e critérios de validação

`DispatchGameEventForDiagnostics(event, modifiers)` chama o mesmo handler da sessão; `SyncFirstPersonInputForDiagnostics()` chama a mesma sincronização; `GetGameEventHandlerForDiagnostics()` permite instalar esse handler com `SetEventHandler`. `TownFirstPersonInputServicesForDiagnostics` substitui somente serviços SDL de foco, captura, estado das setas e descarte de deltas; seu valor é copiado. `nullptr` restaura os serviços reais. Um fixture com esses serviços pode rodar sem tomar o mouse físico do jogador; isso **não comprova captura do sistema operacional, Alt+Tab ou foco de uma janela real**.

O fixture produtivo chama o handler real por `SetEventHandler`/`HandleMessage`, a sincronização de produção e `plrctrls_after_game_logic`. Observa os comandos emitidos por `NetSendCmdLoc`, recebe bytes reais no loopback via `SNetReceiveMessage` e os entrega a `ParseCmd`. Não substitui movimento por escrita de posição. As comparações de caminhada/corrida usam `ProcessPlayers` nativo, com tick, frame, tile, posição futura e caminho confrontados nas oito direções.

Passaram movimento relativo nas oito orientações, bloqueio por towner, captura/retomada, KEYUP possuído, suspensão por inventário/foco, descarte de deltas e 16 comparações de cadência (oito direções × caminhada/corrida). O suplemento desenha Tristram na CPU para verificar motion→down→motion→up, alvo anterior ao giro seguinte, clamp 1,3→1,4→0,8, céu sem fallback, cancelamento da fila e comandos walk/stop ainda não processados ao pausar, abrir chat, perder foco ou mudar de modo. O RNG foi preservado. Nos demais modos o adaptador ficou neutro e o ponteiro absoluto foi preservado.

Registros privados: `diagnostics/first-person-integration-20261008/pure-final.log`, `native-final.log`, `hud-r1.log` e `settings-r1.log`. A primeira falha do fixture vinha de exigir caminho vazio para um destino bloqueado: o pathfinder nativo conserva o destino, e `ProcessPlayers` impede a entrada. O teste foi corrigido para verificar essa colisão real. Logs anteriores são históricos, não resultados finais.

Matriz de revisão do comportamento: checklist de revisão, sem cobertura automática ou física integral. Os resultados exercitados estão descritos acima; esta lista não representa todos os casos aprovados:

- Build do jogo e diagnóstico; registro explícito de falhas, skips e limites.
- Forward/back/strafe e diagonais em oito yaw, incluindo giro com setas seguradas, sem comandos extras por evento/quadro.
- Captura confirmada, falha/lost capture, descarte de delta e nenhuma captura automática após UI/foco.
- `KEYUP` possuído processado antes de retornos de chat/controlador e pausa; nenhum movimento preso após menu, Escape, Alt+Tab simulado ou troca de handler.
- Pressão → comando pendente → suspensão antes de `ParseCmd`: cancelamento ainda válido; clique nativo posterior preservado.
- F4, Home, troca de modo, pausa, inventário/atributos/magias/stash/comércio/textos, timeout, morte, nível, saída, mudança de janela/fullscreen.
- Retículo e seleção no mesmo centro lógico, céu sem alvo falso, interação/clique/combat/HUD nativos, cursor de hardware e software, suavização 1×/2× e restauração exata do fundo. Motion→click antes do próximo desenho foi corrigido e exercitado com picking CPU de chão.
- Perfil, saves, pacotes selecionados e arquivos alheios preservados; candidato separado e nenhum processo humano encerrado.

Captura física, confinamento/ocultação, Alt+Tab e janela/fullscreen reais continuam pendentes. Os testes do HUD/configurações são regressões gerais, não validação visual do novo retículo. Não foram medidos FPS, sincronização entre peers nem todos os alvos/interações de gameplay. Demos legadas não armazenam deltas relativos; gravação/reprodução ficam fora da captura deste incremento. SDL1 conserva o input existente sem captura FPP. Colisão da câmera com arquitetura e revisão artística continuam fora desta entrega.

**Próxima ação:** teste interativo pelo iniciador habitual, com foco/cursor, interfaces, cliques em NPC/itens, diferentes escalas e retículo; registrar qualquer defeito antes de ampliar a entrega.

Serviços relativos seguem os contratos oficiais de [SDL2](https://wiki.libsdl.org/SDL2/SDL_SetRelativeMouseMode) e [SDL3](https://wiki.libsdl.org/SDL3/SDL_SetWindowRelativeMouseMode); não fazer uma segunda conversão de coordenadas nos eventos já convertidos pelo caminho de renderização.
