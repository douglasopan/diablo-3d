# Revisão independente do firstwalk1

**Resultado: candidato rejeitado para animação compatível e gameplay.** A transformação produz movimento real da fonte; a reprovação concreta é contato com o chão e prontidão nativa, não uma ausência de keys. Após essa rejeição, a raiz autorizou 64 renders DIAGNÓSTICOS para mostrar o movimento e defeito com status `diagnostic-reject`/`runtimeEligible=false`; continuam zero clips validados para uso nativo. A execução dos renders pertence ao produtor e os counts/hashes finais ao manifesto da raiz.

Candidato `D:\Diablo 1 3D\assets\own-art\heroes\local-r1\warrior\candidate-firstwalk1\warrior-r2-firstwalk1.glb` — SHA `ad2b74aa5444281f9c80dd9a1c74ef6e08c695ee18b54e1b42672952edb32272`. Todos os inputs e método permaneceram iguais. O parser desta revisão é independente e não importa retarget, hero_pipeline ou inspect_glb.

A cadeia real da coluna foi respeitada: Hips→Spine02→Spine01→Spine na fonte, Hips→Spine→Spine1→Spine2 no Warrior. O método transfere deltas de rotação de world bind e resolve rotations locais sob os pais animados. IBMs, geometria, pesos, hierarchy, materiais e translations/scales do alvo permanecem preservados; apenas hipY varia. Não há cópia cega de quat local ou nome numérico.

A escala 0.01 do Armature Meshy foi incluída no world antes de transportar a variação vertical do quadril. Ratio de pernas independente=0.607708812729; erro máximo hipY=3.98e-08; driftXZ=0. Erro máximo de rotação world nos 32 keys=4.08e-07. Os 23 tracks reabertos geram oito skins distintas; não são deslocamento2D ou stills repetidos.

Oito phases reproduzem o mínimoY=-0.017225675; 288 amostras densas encontramY=-0.031217045, no instante0.343098976s, vertex72, joint dominante `mixamorig:RightToeBase`. Falha a tolerância−0.002. Esses números são unidades do Warrior normalizado, sem calibração em metros; rotular0.002 como2mm ainda não é comprovado.

Fonte32 keys de0.033333335a1.066666722s; saída desloca a origem para0 e mantém span1.033333387s. Oito phases não são oito slots nativos sincronizados. A extensão nominal nativa8×1tick a20ticks/s é0.4s; ratio=2.583333. Entrada/distribuição/encadeamento e phase wrap continuam sob a simulação nativa. Distância máxima de vértice entre endpoints=0.000000567; fechamento de loop/foot locking não foi aprovado.

Os 64 renders diagnósticos explicitamente autorizados podem mostrar este movimento e sua falha. Para futura animação compatível/gameplay: produzir outro candidato com correção explícita de pose/contato ou IK, comprimentos das pernas e footpoint fixos preservados; medir sole trajectory, stance/sliding e seam densamente antes de qualificar/promover/distribuir como animação compatível. Não levantar/recentralizar imagens por frame para ocultar o defeito. Este agente de revisão não editou método, modelos ou outputs do produtor e não renderizou frames. O adendo é textual, sem refazer as verificações numéricas; os inputs congelados permanecem a evidência contínua.

**Gameplay somente sprites para todos os heróis/NPCs, inclusive Warrior/Ogden**, até rig e animações corretos validados. Estrutura que passa não equivale a caminhada correta, cobertura completa ou aceite artístico.

14 verificações passaram; um critério de chão falhou como defeito reproduzido. Outputs: JSON completo e este relatório; nenhuma alteração de Source, registry, build ou instalação.
