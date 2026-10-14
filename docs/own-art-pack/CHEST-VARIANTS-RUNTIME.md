# Outros baús GLB — R12

Pedido humano de12/10/2026: animar a tampa dos dois GLBs adicionais e usar nos outros tipos. O baú R6 permanece nos tipos Chest1/TChest1. Clone1 reforçado usa Chest2/TChest2; wooden treasure chest usa Chest3/TChest3. Somente apresentação3D mudou: loot, armadilhas, colisão, sons, comandos, save e RNG continuam nativos.

## Arte e animação

Originais preservados em local-assets/own-art/props/chest-reinforced-user-r1/original.glb e chest-wooden-user-r1/original.glb. Derivados open-hold-final.glb acrescentam hinge/clip Open_Hold_Final, sem redução ou alteração da geometria/UV/texturas/materiais originais. Reforçado82partes/3687tri,45na tampa,hinge(0,.278,-.248); madeira45partes/4086tri,23na tampa,hinge(0,.548,-.400). Escalauniforme1; nenhuma face degenerada excluída.

Tampa e suas ferragens giram juntas105graus negativos emX durante600ms, curva smoothstep, corpo parado. GLB não define política de loop: runtime mantém o final. Exportador conserva master separado; render de diagnóstico fechado/aberto conferido, incluindo revisão da classificação dos cintos da tampa do reforçado antes da integração.

## Donos e lifecycle

PrepareChestVariants importa as duas listas finitas pinadas por hash. DrawChestVisual escolhe o modelo pelo tipo nativo; se faltar uma variante, usa o modelo R6 anterior, mantendo fallback. Chaves de textura de cada variante são separadas das portas/barril/caixão/R6. ChestVisualProgress já existente observa slot/tile/type/frame: fechado0, transição animada e aberto1 mantido; primeiro desenho de save aberto entra no final. ResetTownViewResources libera as variantes e o estado visual existente. Não grava esquema novo no save e não consome RNG. Materiais seguem o caminho paleta/luz nativa de C1, não PBR pleno.

## Build e evidência

Prévia local versions/diablo3d-preview-e3f8a6edd8a4-chest-variants-r12, EXEe3f8a6edd8a4,435arquivos/434assets (127 novos derivados). Perfil sintético Speed32/1280x720/terceira pessoa, carrega Tristram; entrar na Catedral. R11 igreja/dia-noite, portas, HUD, demais props e baú original preservados.

Gate --chest-variants-only PASS125431: os dois modelos realmente carregados, Chest2/3 e TChest2/3 fechados/início/meio/final/hold GPU, CPU aberto, witness nativo restaurado/RNG, R11/R10/R7/saveLoad. Mesmo código com assetsR11 sem variantes: --doors-only PASS125330. Testes de apresentação substituem temporariamente o tipo/frame/posição de um objeto privado, depois restauram e comparam o witness; não são teste novo da simulação de dano/loot, cujo código não mudou. Capturas de runtime conferidas. GLBs validados para timestamps/quaternions/hinge/ângulo e filhos completos. VerifyOnly435, recibo diagnostics/chest-variants-20261012-r12/DELIVERY.json e snapshot source-r12. Revisão humana/FPS sustentado continuam pendentes. Sem publicação, instalação habitual, web ou Panvia; README humano intacto.
