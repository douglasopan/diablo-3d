# Engine25: brasas e bytecode de shaders

Incremento técnico isolado de16/10/2026, a partir da base entregue local22. Não está incorporado ao WIP principal, Catedral23 ou Tristram24. Source `22a64e1b6fdd8ecc5251c3b79d5105943bf68550edce5feb1069a6cc697b2c27`; EXE `8286ae82880599b18ed4011e7e8aab12d3022bd6f7e31838fc8a2239f67437f1`. Detalhes locais: `diagnostics/engine-fire-loading-local25-20261016-r1/HANDOFF.md`, diff, closure e `DELIVERY.json`.

## Donos e lifecycle

`town_fire_particles.hpp` é uma função pura de identidade/slot/ciclo/tempo visual absoluto, sem RNG nativo, estado acumulado, alocações ou dependência de FPS. `town_view.cpp` coleta somente fontes com chama efetiva em Tristram e braseiros nativos admitidos em C1. As luzes vermelhas arquitetônicas da igreja não emitem brasas.

O host recompõe fontes por frame; reset/recusa/fallback descartam os dados. Até32 emissores,128 partículas e65.536 pixels candidatos. Composição RGB após readback/atmosfera e antes do viewmodel CPU/resolve, com profundidade do mesmo frame, máscara FP e fog da partícula. Índice/depth/pick/SOL/simulação permanecem intactos. A telemetria publicada depende da superfície RGB válida e da identidade atual de picking. A preferência existente Smooth 3D Lighting governa o efeito; controle salvo independente ainda pendente.

`town_gpu.cpp` mantém dois blobs CPU de bytecode, com identidade exata por fonte/entry/profile/flags/macro e limite agregado8MiB. O worker original compila misses; o dono original atende SDL e cria recursos GPU. Hits fornecem referências COM independentes e criam shaders novos no dispositivo atual. Reset ainda libera dispositivo/contexto/caches/outputs; somente bytecode CPU e métricas cumulativas sobrevivem. Os blobs são liberados no encerramento normal do processo. `TownGpuStatus` recebeu métricas ao final; os consumidores do incremento foram recompilados.

## Evidência e limites

Guard original preservado. Contrato puro58.601; cache CPU42; hardware24; gate nativo88.886 verificações. Oito vistas reais C1 antes/depois de roundtrip com RGB ON/OFF/congelado; witness completo de mapa/SOL/ocupação/atores/luzes/exploração/RNG e índice/depth/pick preservados. Testes sintéticos cobrem oclusão, máscara FP e budgets. Instalação, reuso e recusa de cena alterada passaram.

Dois dispositivos hardware separados produziram os mesmos cinco buffers32×24. Begin frio21.100,056ms; recriação25,436ms com duas compilações totais e dois hits. Bytecode retido175.200B. Mede criação do backend; primeira compilação, outros loaders, uploads, readback e FPS não são acelerados por associação. A composição de brasas nas vistas CPU mediu aproximadamente0,006–0,007ms; não é FPS sustentado.

264 assets e defaults byte-exatos à local22, inclusive músicas/tags Douglas Pan; saves e preferências copiados somente para perfil separado. Nenhuma partida normal aberta ou executável habitual substituído.

Esta etapa **não entrega iluminação/sombras pontuais novas na Catedral**. C1 conserva dLight/LUT; Tristram conserva luz pontual interior/oclusão por cascas e sombra direcional existentes. O depth do efeito bloqueia câmera→partícula, não prova sombra fonte→superfície. A próxima etapa25B exige geometria/visibilidade de uma fonte C1 delimitada, portas/arcos/receivers, budget e invalidação próprios; não aplicar indiscriminadamente as cascas AABB de Town. PBR novo, gameplay GPU prolongado, FPS, entrada física e aceite artístico permanecem pendentes.
