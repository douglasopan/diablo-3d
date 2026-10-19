# Identidade visual do player ligada à simulação nativa

Incremento da fila Warrior light/SwordShield em Tristram e Catedral 1, 13/10/2026. Implementação no candidato `clean-base/devilutionx-c1-r1`; sprites e release R17 continuam selecionados. Este documento registra eventos reais, não a ativação do modelo 3D.

## Donos e eventos

`PlayerActorLifecycle` pertence à thread nativa e guarda época do nível, ativação do slot, revisão exata de equipamento e origem da morte. Os counters são monotônicos e separados de frames, relógio, recursos GPU e preferências. Os sete slots de equipamento têm comparação semântica exata; o fingerprint é somente evidência legível.

`player_actor_native_hooks.hpp` encontra slots por igualdade de endereço com elementos reais de `Players`. Jogadores temporários não recebem identidade runtime. Scopes observam a conclusão das funções nativas sem escrever em Player, RNG, mapa, comandos ou campos de save.

| Evento | Ligação produtiva | Resultado |
| --- | --- | --- |
| Começo/fim do carregamento | `LoadGameLevel`: Begin antes do loader; Complete e baseline dos slots ativos após cursor, luz/visão e prontidão | Época própria válida também em Town; a entrada nativa precisa concluir `_pLvlChanging`. |
| Criação, init, load e unpack | `CreatePlayer`, `InitPlayer`, `LoadPlayer`, `UnPackPlayer`, `UnPackNetPlayer` | Desativação antes de substituir o slot e ativação após o valor final. Unpacker falível só confirma após sucesso. |
| Entrada/saída nativa | `recv_plrinfo`, `OnPlayerJoinLevel`, `PlayerLeftMsg`, limpeza em `NetInit`/`NetClose` | Endereço reutilizado não reutiliza activation; pacotes parciais não autorizam apresentação. |
| Equipamento/atributos/identificação | `CalcPlrInv` e `CalcPlrItemVals` aninhados | Somente o fim externo publica equipamento e gráfico finais; leituras durante updates recusam. |
| Morte | `StartPlayerKill`, após early-return e antes de qualquer drop/gráfico | Testemunho real de equipamento retido ou todos os equipamentos retirados. Drop exige origem e retirada observadas. |
| Ressurreição | `ApplyResurrect`: Begin antes de HP/mode e End após inventário/stand | Exige conclusão viva (HP fixed-point pelo menos 64), publica activation nova e limpa o cadáver anterior. |
| Liberação do jogo | `FreeGameMem` antes de liberar referências | Invalida slots e época; counters não voltam a zero. |

Reset de recursos, F4, foco e recusa de frame não reinicializam este owner. O renderer só lê snapshots atuais; recusa preserva o output anterior e não o torna válido.

## Defeito de ressurreição reproduzido

Após drop completo, inventário e gráfico podem permanecer idênticos ao voltar para PM_STAND. Um commit de equipamento sozinho não detectava a saída da morte. O evento explícito de ressurreição resolve esse caso e invalida tokens anteriores mesmo quando equipmentRevision permanece igual. Um cadáver carregado não inventa sua origem de drop; a ressurreição real pode publicar o estado vivo sem fabricar essa origem.

## Evidência

- Smoke com Player real: 110 checks, zero falhas, incluindo drop/retained/cadáver carregado, vida fixed-point, recusas, updates aninhados e freshness. Recibo `diagnostics/player-actor-lifecycle-resurrection-20261013-r1/DELIVERY.json`.
- Gate nativo novo: **106.749 checks PASS** em `diagnostics/player-actor-native-hooks-20261013-r1`, execução pass4. Chamou NEW/NetInit/DoLoad/LoadGame, transições Town/C1, StartPlayerKill, ApplyResurrect, parser e validação de UnPackNetPlayer reais. Inclui snapshots antes/depois, equipamento inicial Short Sword1/Buckler2, invalidação de tokens e FreeGameMem.
- Código e gate: 775 arquivos selados, sem alteração durante a execução; assets: 437 arquivos da revisão R17. Executável privado próprio e perfil novo, Speed32. GPU hardware Radeon RX570, duas vistas de câmera; sem WARP/CPU raster fallback no probe. Isto não mede FPS sustentado nem desenha o Warrior 3D.
- Leituras de snapshots preservam testemunho de simulação/RNG/ocupação. Saves binários são comparados dentro de cada trecho nativo; load é outra fronteira. O fechamento nativo de menu chama SaveOptions e pode atualizar somente o INI da fixture isolada. Nenhum perfil humano foi usado.
- Falhas preparatórias preservadas: leitura antes do join nativo de prontidão; comparação de INI apesar de SaveOptions nativo; comparação de arquivos através de uma fronteira de load. Corrigida a fixture, sem relaxar os guards do owner ou alterar simulação.

## Continuação da mesma fila

Fechar a pegada da Short Sword real, compor os clips, qualificar transições/interrupções e integrar frame completo/material/paleta/picking aos dois caminhos de apresentação. O owner não autoriza revisão artística nem cobertura do piloto; a admissão existente continua exigindo evidência completa. Nenhuma build normal, seleção de asset ou publicação foi criada por este incremento.
