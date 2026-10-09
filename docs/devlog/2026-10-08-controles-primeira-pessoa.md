---
title: "Controles: movimento remapeável e câmera"
date: 2026-10-08
updated: 2026-10-09
description: "Primeira e terceira pessoa agora compartilham oito associações de movimento editáveis no menu principal, com preferências salvas. Veja os controles, os testes e a revisão interativa ainda pendente."
slug: controles-primeira-pessoa
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Comparação técnica histórica dos quatro modos de câmera em Tristram, usada como contexto; não mostra o remapeamento de teclas, os novos controles, a calibração ocular, a transição pela roda ou a colisão visual da câmera."
category: Protótipo
order: 18
status: published
---

**Atualização de 9 de outubro:** o movimento em primeira e terceira pessoa agora permite escolher uma tecla principal e outra alternativa para cada direção, pelo menu principal. As oito associações podem ser alteradas ou desassociadas, e ficam salvas. O [commit a4a87d20e](https://github.com/douglasopan/diablo-3d/commit/a4a87d20e58781395ed33bda6f9298c71aa589ad) foi instalado no mesmo **Iniciar-Tristram.cmd**, às **01:35, horário de Brasília**. A avaliação interativa com dispositivos físicos permanece pendente.

Na entrega de **8 de outubro**, a primeira pessoa de Tristram passou a combinar **mouse para olhar, WASD ou setas para caminhar e roda para aproximar da terceira pessoa até a primeira — ou voltar**. A proteção visual da câmera passou a considerar a arquitetura montada, preservando as regras de movimento do Diablo; a amostra técnica descrita abaixo não cobre todas as estruturas ou posições. Esse incremento do [commit 3863e4436](https://github.com/douglasopan/diablo-3d/commit/3863e4436a5cb6be09d476e8d382fc4e5bf54b3a) foi instalado às **19:31 daquele dia**.

Os controles iniciais de mouse e setas chegaram às **17:30** daquele dia, no [commit a227d4afc](https://github.com/douglasopan/diablo-3d/commit/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9). A calibração ocular posterior continua registrada abaixo.

A comparação de câmeras acima já havia sido publicada e serve somente como contexto. **Não há novas capturas de gameplay, do retículo ou do menu de remapeamento nesta publicação.** A implementação e os limites dos testes estão na [documentação da revisão atual](https://github.com/douglasopan/diablo-3d/blob/a4a87d20e58781395ed33bda6f9298c71aa589ad/docs/TRISTRAM-FIRST-PERSON-INPUT.md).

## Escolher as teclas de movimento

No **menu principal → Configurações → Mapeamento de Teclas**, há quatro direções de movimento, cada uma com associação principal e alternativa. Os padrões continuam sendo **W/S/A/D e ↑/↓/←/→**. É possível trocar ou remover cada associação; **Restaurar teclas de movimento** repõe somente esse grupo.

As mesmas preferências valem para primeira e terceira pessoa. O movimento continua relativo à câmera e usa as oito direções, a cadência e a colisão nativas. A tecla principal e a alternativa são independentes: segurar W e ↑ e soltar apenas uma mantém o avanço.

Esse contexto preserva os atalhos comuns do jogo: S pode continuar associado às magias fora do movimento da câmera. Teclas reservadas para câmera, pausa e captura de tela são protegidas; um conflito entre movimentos é recusado com identificação da associação existente. Se um padrão estiver indisponível, o reset informa isso sem tomar a tecla reservada.

As mudanças, incluindo uma alternativa explicitamente vazia, persistem no arquivo de preferências **INI**. Também foi corrigida a primeira tecla de movimento após usar gamepad: ela passa pelo contexto correto de teclado. Solturas, repetições e entradas antigas após mudar associação, foco, menu ou modo são tratadas para evitar movimentos presos ou acionamentos indevidos de atalhos.

## Como entrar e se movimentar

Em Tristram, **F4** alterna a visão original e o protótipo 3D. **K** percorre isométrica, órbita livre, terceira pessoa e primeira pessoa; esse atalho é remapeável. **Home** restaura a câmera original.

Na primeira pessoa, o movimento horizontal do mouse gira a câmera e o vertical muda a inclinação. Não é preciso segurar o botão central; a sensibilidade usa a preferência já salva. O controle só é ativado quando a plataforma confirma a captura relativa do mouse.

| Entrada padrão | Ação na primeira pessoa |
| --- | --- |
| W ou ↑ / S ou ↓ | Avançar / recuar em relação à câmera. |
| A ou ← / D ou → | Deslocar lateralmente. |
| Roda | Aproximar da terceira até a primeira pessoa; afastar para voltar. |
| Esc | Liberar o mouse e seguir a ação nativa. |
| Clique no mundo após a suspensão | Retomar a captura; esse primeiro clique é consumido. |

As combinações de WASD e setas são convertidas para as **oito direções nativas**, na mesma cadência de caminhada. A inclinação da câmera não inclina o deslocamento. Posição, velocidade, animação e colisão do personagem continuam sob as rotinas existentes do jogo. As teclas são independentes: segurar W e ↑ e soltar somente uma delas mantém o avanço.

## Abrir interfaces e retomar o mouse

Menus, inventário, outros painéis e perda de foco suspendem o controle de primeira pessoa e limpam as entradas que ele possui. Sair desse modo também libera o mouse.

**Fechar uma interface ou voltar à janela não recaptura o mouse automaticamente.** Um novo clique esquerdo ou direito dentro do mundo retoma o controle; esse clique e sua soltura não executam uma interação. Deltas antigos são descartados, e teclas de movimento que já estavam seguradas precisam ser soltas e pressionadas novamente para mover.

A órbita livre e a visão isométrica conservam seus controles. Ao assumir um controlador, o jogo mantém a rotina nativa de gamepad e suspende esse adaptador de teclado e mouse.

## Selecionar depois de girar a câmera

Durante a captura, a seleção usa o centro do viewport lógico do mundo, onde o código desenha o retículo. Girar a câmera ou usar a roda invalida a seleção anterior.

Um clique recebido antes do próximo desenho agora **aguarda uma seleção atualizada**, preservando a ordem entre movimentos do mouse, cliques e solturas. Isso evita usar o alvo anterior ao giro ou enviar uma caminhada indevida para o tile do herói. Olhar para o céu sem um alvo válido não produz um comando de chão.

A correção foi exercitada com seleção de chão pelo caminho CPU. Interações com NPCs, itens e combate ainda precisam de revisão em uma partida real, assim como a aparência do novo retículo nas diferentes escalas.

## Altura dos olhos

**Atualização de 8 de outubro, às 18:18:44 de Brasília:** o relato de câmera baixa diante dos NPCs levou a uma calibração independente. A altura dos olhos na primeira pessoa passou de **1,1 para 1,7 unidade**; o alvo da terceira pessoa permanece em **1,1**. A correção do [commit 4a276a10b](https://github.com/douglasopan/diablo-3d/commit/4a276a10b090c6bb46f8db5c59c0383bff3bcc84) já está instalada no mesmo iniciador.

Passaram **202 verificações de câmera** e novamente **1.652 verificações dos controles**. Uma comparação nativa CPU, em um ponto diante de Griswold e da ferraria, em 640×480, conservou a imagem indexada e a paleta da terceira pessoa byte a byte. Os **44 arquivos do perfil** preservaram bytes, tamanho e data nessa instalação.

**1,7 é uma calibração inicial**, ainda sujeita à avaliação na partida diante de outros moradores e com outras classes e poses. Não houve mudança de modelos, materiais ou iluminação, nem teste de mouse físico, execução GPU ou medição de FPS nesta correção. A [documentação da calibração](https://github.com/douglasopan/diablo-3d/blob/4a276a10b090c6bb46f8db5c59c0383bff3bcc84/docs/TRISTRAM-HORIZON-CAMERAS.md#calibração-da-altura-ocular--8-de-outubro) registra os limites da amostra.

## Roda entre as vistas e colisão da câmera

A roda preserva a direção e a inclinação ao alternar entre terceira e primeira pessoa. Distância e altura mudam gradualmente, mantendo a altura independente do **alvo da terceira pessoa em 1,1** e a do **olho da primeira pessoa em 1,7**. Um clique após a roda aguarda a seleção atualizada, assim como um clique após girar a câmera; as interfaces conservam seu uso normal da roda.

A colisão visual usa os triângulos da **arquitetura montada**, incluindo as aberturas existentes, para proteger o olho e o plano próximo da câmera durante o movimento. Diante de uma parede, a câmera encurta imediatamente a distância e a recupera gradualmente quando há espaço. **Isso não altera a colisão nem a navegação do personagem.** Atores, árvores e rochas ainda ficam fora dessa proteção.

## O que passou nos testes

Os resultados desta seção correspondem às entregas de **8 de outubro**. O build **Release/NONET x64** foi aprovado. O incremento de roda, WASD e colisão passou **253 verificações de câmera**, **1.239 de entrada pura**, **29 de colisão** e **2.777 do caminho produtivo**, além das regressões de **946 do HUD** e **59.452 das configurações**. A primeira instalação dos controles havia passado 673 verificações puras e 1.652 produtivas.

O diagnóstico CPU com o pacote selecionado percorreu **34 quadros**: 14 tiveram a câmera limitada pela arquitetura e nenhum ficou sem posição segura. O índice espacial BVH dos 82.086 triângulos foi construído uma vez; a entrada sem avanço de tempo manteve imagem e paleta exatas. Essa amostra não cobre todas as estruturas ou posições.

Os testes exercitaram movimento nas oito orientações, combinações de WASD e setas, comandos e cadência nativos, colisão, suspensão e retomada, descarte de deltas e cliques ordenados após girar a câmera. O estado do gerador de números aleatórios foi preservado.

O diagnóstico produtivo usa o handler real do jogo e comandos nativos, mas substitui os serviços de foco e captura do sistema e roda com janela oculta, GPU desligada e perfil temporário. **Isso não comprova o comportamento do mouse físico, Alt+Tab ou uma janela real.** As regressões de HUD e configurações também não são uma aprovação visual do retículo. As 946 verificações do HUD desta rodada de câmera **não cobriam o defeito das páginas Gameplay 1/2**. Uma entrega separada instalou posteriormente a [correção técnica do HUD](/devlog/hud-paginas-jogabilidade/); a confirmação na partida habitual pelo usuário continua pendente. Não houve execução GPU ou novo benchmark de FPS nesta rodada.

## Testes e instalação do remapeamento

Em **9 de outubro**, o build Release/NONET passou **5.341 verificações de entrada pura, 60.944 de configurações, 3.792 de runtime, 29 de colisão e 946 do HUD**. Um gate separado de câmera produziu **24 PNGs CPU/GPU offscreen**; essa prova finita de renderização não transforma os testes simulados de entrada em testes de dispositivos físicos.

O menu principal real passou **3.324 verificações**, em português e inglês, nas resoluções 640×480 e 1920×1080. Foram exercitados conflitos, captura de teclas, solturas e repetições, Escape, desassociação e restauração completa ou parcial. **Quatro pares de processos escritor/leitor** confirmaram a persistência pelo INI, sem regravar as preferências na leitura. Duas ações foram efetivamente editadas; os oito IDs e padrões foram conferidos. O idioma veio do INI, sem teste de troca pela interface.

As **20 capturas indexadas do menu** não incluem o fundo RGB final; duas amostras PT-BR foram inspecionadas. Instruções e descrições nativas longas podem recortar em 640×480. Essas imagens continuam privadas. Os testes de entrada e runtime substituem serviços físicos do SDL: **mouse, teclado, gamepad, foco e Alt+Tab físicos, FPS sustentado e aprovação artística integral permanecem fora desta validação**.

A instalação das **01:35** preservou conteúdo, tamanho e data dos **44 arquivos do perfil**. Os três aliases receberam o mesmo executável, com backups e **zero processos encerrados**. O preparo posterior do iniciador alterou somente o recibo esperado de runtime.

## Instalação e próxima avaliação

Na instalação anterior de **8 de outubro, às 19:31**, os **44 arquivos do perfil habitual** preservaram bytes, tamanho e data; os três aliases receberam o mesmo executável, sem encerrar processos. A preparação posterior do iniciador alterou somente o recibo esperado de runtime. A primeira instalação dos controles havia preservado 41 arquivos. Modelos, texturas, iluminação e o backend GPU não foram alterados por aquele incremento.

Após o lote anterior de melhorias na GPU, o autor relatou que “melhorou muito o desempenho do jogo!”. Esse retorno é uma **avaliação subjetiva daquela entrega anterior**; os controles de primeira pessoa não acrescentam uma medição de FPS nem um novo benchmark. A [correção do zoom e recuperação da GPU](/devlog/gpu-zoom-recuperacao/) permanece registrada separadamente.

O próximo passo é testar pelo iniciador habitual: alterar, desassociar e restaurar as teclas de movimento no menu principal, reiniciar para conferir a persistência e caminhar em primeira e terceira pessoa. A revisão também inclui roda entre as vistas, altura dos olhos diante dos moradores, câmera junto das paredes, captura e liberação do mouse, Alt+Tab, interfaces, retículo e cliques em NPCs e itens. Gravação/reprodução de demos legadas e captura em SDL1 continuam fora deste incremento.

O objetivo continua sendo **Diablo 1 inteiro em 3D**. Estes controles valem para o protótipo de Tristram; os demais níveis ainda usam a renderização original. A [documentação atual dos controles](https://github.com/douglasopan/diablo-3d/blob/a4a87d20e58781395ed33bda6f9298c71aa589ad/docs/TRISTRAM-FIRST-PERSON-INPUT.md) separa a entrega instalada e as próximas avaliações.
