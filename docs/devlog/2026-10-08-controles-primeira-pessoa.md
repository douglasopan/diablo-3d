---
title: "Primeira pessoa: olhar com o mouse, andar com as setas"
date: 2026-10-08
updated: 2026-10-08
description: "Os controles de primeira pessoa chegaram ao iniciador habitual: mouse para olhar, setas relativas à câmera e seleção central, com a caminhada nativa preservada."
slug: controles-primeira-pessoa
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Comparação técnica histórica dos quatro modos de câmera em Tristram, usada como contexto; não mostra os novos controles, a calibração ocular nem a validação visual do retículo."
category: Protótipo
order: 18
status: published
---

A câmera de primeira pessoa já existia em Tristram. Agora ela também tem controles próprios: **olhar com o mouse e caminhar com as setas em relação à direção da câmera**, mantendo as regras de movimento do Diablo. A entrega foi publicada no [commit a227d4afc](https://github.com/douglasopan/diablo-3d/commit/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9) e instalada pelo mesmo **Iniciar-Tristram.cmd**, em **8 de outubro de 2026, às 17:30, horário de Brasília**.

A comparação de câmeras acima já havia sido publicada e serve somente como contexto. **Não há novas capturas de gameplay ou do retículo nesta publicação.** A implementação e os limites dos testes estão na [documentação dessa revisão](https://github.com/douglasopan/diablo-3d/blob/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9/docs/TRISTRAM-FIRST-PERSON-INPUT.md).

## Como entrar e se movimentar

Em Tristram, **F4** alterna a visão original e o protótipo 3D. **K** percorre isométrica, órbita livre, terceira pessoa e primeira pessoa; esse atalho é remapeável. **Home** restaura a câmera original.

Na primeira pessoa, o movimento horizontal do mouse gira a câmera e o vertical muda a inclinação. Não é preciso segurar o botão central; a sensibilidade usa a preferência já salva. O controle só é ativado quando a plataforma confirma a captura relativa do mouse.

| Entrada | Ação na primeira pessoa |
| --- | --- |
| ↑ / ↓ | Avançar / recuar em relação à câmera. |
| ← / → | Deslocar lateralmente. |
| Esc | Liberar o mouse e seguir a ação nativa. |
| Clique no mundo após a suspensão | Retomar a captura; esse primeiro clique é consumido. |

As combinações de setas são convertidas para as **oito direções nativas**, na mesma cadência de caminhada. A inclinação da câmera não inclina o deslocamento. Posição, velocidade, animação e colisão continuam sob as rotinas existentes do jogo.

## Abrir interfaces e retomar o mouse

Menus, inventário, outros painéis e perda de foco suspendem o controle de primeira pessoa e limpam as entradas que ele possui. Sair desse modo também libera o mouse.

**Fechar uma interface ou voltar à janela não recaptura o mouse automaticamente.** Um novo clique esquerdo ou direito dentro do mundo retoma o controle; esse clique e sua soltura não executam uma interação. Deltas antigos são descartados, e setas que já estavam seguradas precisam ser soltas e pressionadas novamente para mover.

Os demais modos de câmera conservam seus controles. Ao assumir um controlador, o jogo mantém a rotina nativa de gamepad e suspende esse adaptador de teclado e mouse.

## Selecionar depois de girar a câmera

Durante a captura, a seleção usa o centro do viewport lógico do mundo, onde o código desenha o retículo. Girar a câmera invalida a seleção anterior.

Um clique recebido antes do próximo desenho agora **aguarda uma seleção atualizada**, preservando a ordem entre movimentos do mouse, cliques e solturas. Isso evita usar o alvo anterior ao giro ou enviar uma caminhada indevida para o tile do herói. Olhar para o céu sem um alvo válido não produz um comando de chão.

A correção foi exercitada com seleção de chão pelo caminho CPU. Interações com NPCs, itens e combate ainda precisam de revisão em uma partida real, assim como a aparência do novo retículo nas diferentes escalas.

## Altura dos olhos

**Atualização de 8 de outubro, às 18:18:44 de Brasília:** o relato de câmera baixa diante dos NPCs levou a uma calibração independente. A altura dos olhos na primeira pessoa passou de **1,1 para 1,7 unidade**; o alvo da terceira pessoa permanece em **1,1**. A correção do [commit 4a276a10b](https://github.com/douglasopan/diablo-3d/commit/4a276a10b090c6bb46f8db5c59c0383bff3bcc84) já está instalada no mesmo iniciador.

Passaram **202 verificações de câmera** e novamente **1.652 verificações dos controles**. Uma comparação nativa CPU, em um ponto diante de Griswold e da ferraria, em 640×480, conservou a imagem indexada e a paleta da terceira pessoa byte a byte. Os **44 arquivos do perfil** preservaram bytes, tamanho e data nessa instalação.

**1,7 é uma calibração inicial**, ainda sujeita à avaliação na partida diante de outros moradores e com outras classes e poses. Não houve mudança de modelos, materiais ou iluminação, nem teste de mouse físico, execução GPU ou medição de FPS nesta correção. A [documentação da calibração](https://github.com/douglasopan/diablo-3d/blob/4a276a10b090c6bb46f8db5c59c0383bff3bcc84/docs/TRISTRAM-HORIZON-CAMERAS.md#calibração-da-altura-ocular--8-de-outubro) registra os limites da amostra.

## O que passou nos testes

O build **Release/NONET x64** foi aprovado. A validação passou **673 verificações da política de entrada** e **1.652 verificações do caminho produtivo**, além das regressões de **946 verificações do HUD** e **59.452 das configurações**.

Os testes exercitaram movimento nas oito orientações, combinações de setas, comandos e cadência nativos, colisão, suspensão e retomada, descarte de deltas e cliques ordenados após girar a câmera. O estado do gerador de números aleatórios foi preservado.

O diagnóstico produtivo usa o handler real do jogo e comandos nativos, mas substitui os serviços de foco e captura do sistema e roda com janela oculta, GPU desligada e perfil temporário. **Isso não comprova o comportamento do mouse físico, Alt+Tab ou uma janela real.** As regressões de HUD e configurações também não são uma aprovação visual do retículo.

## Instalação e próxima avaliação

Na instalação inicial dos controles, os **41 arquivos do perfil habitual** preservaram bytes, tamanho e data. A preparação posterior do iniciador alterou somente o recibo esperado de runtime. Modelos, texturas, iluminação e o backend GPU não foram alterados por esta entrega.

Após o lote anterior de melhorias na GPU, o autor relatou que “melhorou muito o desempenho do jogo!”. Esse retorno é uma **avaliação subjetiva daquela entrega anterior**; os controles de primeira pessoa não acrescentam uma medição de FPS nem um novo benchmark. A [correção do zoom e recuperação da GPU](/devlog/gpu-zoom-recuperacao/) permanece registrada separadamente.

O próximo passo é testar pelo iniciador habitual: altura dos olhos diante dos moradores, captura e liberação do mouse, Alt+Tab, interfaces, retículo e cliques em NPCs e itens. Gravação/reprodução de demos legadas, captura em SDL1 e colisão da câmera com a arquitetura ficam fora deste incremento.

O objetivo continua sendo **Diablo 1 inteiro em 3D**. Estes controles valem para o protótipo de Tristram; os demais níveis ainda usam a renderização original. A [fila dessa revisão](https://github.com/douglasopan/diablo-3d/blob/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9/docs/PROJECT-EXECUTION.md) mantém a revisão dos modelos e as próximas dependências.
