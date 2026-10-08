---
title: "GPU continua ativa ao afastar a câmera"
date: 2026-10-08
description: "Corrigido o limite que provocava queda para CPU ao mostrar mais objetos; a recuperação por capacidade agora acompanha mudanças de carga, com a proteção mantida."
slug: gpu-zoom-recuperacao
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Comparação técnica histórica dos quatro modos de câmera em Tristram, reutilizada como contexto; não é evidência nova da correção do zoom."
category: Ferramentas
order: 17
status: published
---

Depois da instalação das malhas residentes, o teste do autor revelou um problema: habilitar a GPU e afastar a câmera podia tornar o backend indisponível e derrubar o desempenho. A correção foi publicada no [commit 046a1850d](https://github.com/douglasopan/diablo-3d/commit/046a1850dd022a9a9b6a93d84cd381970bc82b89) e instalada no iniciador habitual em **8 de outubro de 2026, às 15:30, horário de Brasília**.

A imagem acima é uma comparação técnica de câmeras já publicada, usada apenas como contexto. **Não há novas capturas de gameplay desta correção.** A evidência está nos testes descritos a seguir e na [documentação fixada nesta revisão](https://github.com/douglasopan/diablo-3d/blob/046a1850dd022a9a9b6a93d84cd381970bc82b89/docs/GPU-RENDERER.md).

## De onde vinha a queda

O log da partida mostrou a troca para CPU exatamente ao alcançar **1.048.576 triângulos**, com a Radeon RX 570 identificada e sem erro de remoção do dispositivo. O limite aplicado ao stream temporário também contava desenhos de malhas já residentes na GPU, embora essas malhas não alocassem vértices nesse stream.

Ao afastar a câmera, mais objetos entravam no enquadramento e essa contagem compartilhada podia rejeitar um quadro que cabia no caminho residente. Depois da primeira falha, o bloqueio permanecia até desligar e ligar a opção da GPU.

## Limites separados, proteção mantida

A contagem foi corrigida. O teto continua valendo para o **stream projetado**, enquanto as malhas residentes conservam os limites reais de memória, índices e tamanho de buffers. O cache de geometria continua com seu orçamento independente de **256 MiB**; a correção não remove os limites de memória nem representa toda a memória disponível na placa.

As falhas também passaram a distinguir capacidade, entrada inválida, dispositivo e recurso não suportado, preservando a primeira causa. Se ocorrer uma falha real, o quadro inteiro ainda pode voltar à CPU com cor, profundidade e seleção coerentes.

## Quando a GPU pode voltar

Uma falha de capacidade pode agora recuperar automaticamente após uma mudança efetiva de câmera, posição, projeção ou painéis, cena, raster ou visibilidade. Há um **intervalo mínimo de um segundo entre tentativas após falhas**; mover a câmera continuamente não elimina esse intervalo, e a mesma carga rejeitada não é tentada a cada quadro.

Uma recuperação bem-sucedida limpa a mensagem. Erros de entrada ou dispositivo continuam com tratamento separado, podendo exigir intervenção. Desligar e ligar a opção permite tentar novamente e recriar recursos após falha de dispositivo. Não foi provocada perda física da placa durante a validação.

## O que o teste comprovou

A suíte sintética passou **1.843 verificações**. O diagnóstico com a cena real usou **Full HD, FOV 80, o pacote selecionado e a GPU de hardware Radeon RX 570**. Oito poses passaram por zoom até a distância **80**, giros e órbita livre, com verificações adicionais de retorno isométrico e reset dos buffers. O retorno isométrico conserva o caminho projetado; esta correção não habilita geometria residente nesse modo.

O maior quadro completo chegou a **1.185.392 triângulos**. Toda a sequência permaneceu no backend GPU, com **zero triângulos rasterizados pela CPU**. Isso se refere à rasterização nesses testes: preparação da cena, simulação e outras etapas ainda têm trabalho na CPU.

Repetir as poses preservou a cor completa, e **627 sondas de seleção e profundidade** passaram. O mapa completo de sombras, as preferências, a simulação e seu gerador de números aleatórios permaneceram intactos. Um teste separado excedeu um limite real de pixels, confirmou o fallback CPU integral e a recuperação automática após reduzir o viewport, **sem desligar e ligar a opção**.

Esse resultado comprova a permanência na GPU nas condições testadas. **Não mede FPS sustentado da partida nem comprova 60 FPS.** As [medições anteriores de geometria residente](/devlog/creditos-e-geometria-gpu/) permanecem como histórico daquela rodada; esta correção não acrescenta um novo benchmark de fluidez.

## Instalação e próximos passos

Nenhum modelo ou textura foi substituído. Os **40 arquivos do perfil habitual** conservaram seus bytes e datas na instalação; a preparação posterior alterou somente o recibo esperado de runtime. A entrada continua sendo o mesmo iniciador habitual.

A preparação na CPU e a leitura de volta da GPU continuam custosas. Orçamento de memória via DXGI e seleção explícita de adaptador ainda estão pendentes. O próximo passo é medir esses gargalos na partida real, preservando a qualidade dos assets e o fallback de segurança. O [guia de execução dessa revisão](https://github.com/douglasopan/diablo-3d/blob/046a1850dd022a9a9b6a93d84cd381970bc82b89/docs/PROJECT-EXECUTION.md) registra a fila.

O objetivo continua sendo reconstruir Diablo 1 inteiro em 3D. Tristram é o primeiro ambiente de validação; a correção não amplia o renderer para os demais níveis nem substitui a revisão artística dos modelos.
