# Casa do healer — R13

Douglas forneceu cabin 2.glb em12/10/2026 para a casa do healer. Usa o ID existente tristram.arch.house-pepin/source(50,76), sem substituir a cabine do spawn ou alterar casas vizinhas.

## Arte e encaixe

Master original preservado em local-assets/own-art/buildings/healer-house-user-r1/cabin-2-original.glb, SHA830e21336ff65c5befff898ddbee3347ad86b135191028d3691b704ce943d0d9. Três nós/meshes ativos no GLB são mantidos com seus transforms originais; materiais0/1 usam os mesmos pixels JPEG1024², combinados no derivado sem atlas/resample. UV/textura e master intactos. Escalauniforme8, yaw90: frente+Z do master vira+X junto a Pepin. Centro(50.4,0,79), origemD3d(46,74), bounds(45.9169,0,75.4183)..(54.3980,5.2567,82.7394). Família nativa de pintura e envelope(46,74)..(55,80) mantidos.

Derivado final1986tri de1989: duas faces abaixo do limite de normal e uma face quase nula que colapsa após adição da origem em float nativo foram excluídas somente do derivado. Não há LOD ou redução dos masters; não ampliar tolerâncias do importer. HashD3d2e12f58ca5c8bbaad833af5a60f348c97b49db4867b821a3991bb4fc98ed2574. Exportação inicial/falha parse/gate-r1 e reparo de precisão preservados.

## Autoridade e lifecycle

BuildScene monta a casa nativa de Pepin e chama LoadTownModelOverride depois de fixar o envelope de pintura. Carrega o caminho d3d-models/healer-house-user-r1.d3d por hash/revisão healer-house-user-r1-uniform8-yaw90. Falta/recusa mantém proxy anterior; política MeshyOFF intacta. GetTownScene/ResetTownScene e renderer/cache/luz existentes governam recursos, sem novo estado persistente. Capturas em Day/Evening/Night e GPU/CPU usam o material importado do mesmo modelo. Não adicionar cortes/interior específicos de outros masters. Porta permanece como fornecida; não se acrescentou animação/interior completo.

NPC Pepin, talk callback, posição, comandos, SOL/colisão, RNG e saves não mudam. Footprint visual do GLB não é colisão nova; zonas externas ao SOL original continuam sob autoridade nativa. Somente uma construção é substituída.

## Entrega e evidência

EXE27507ae55fb7, versão local versions/diablo3d-preview-27507ae55fb7-healer-house-r13.436arquivos/435assets, um arquivo novo. Perfil sintético separado Speed32/1280x720/terceira pessoa/entardecer; carregaTristram, visitar Pepin. Casa e baús R12/recursosR11 mantidos; tviewCPP/header conferidos contra reciboR12 sem alterações.

Gate --healer-house-only PASS125446: identidade/contagem/envelope,4ângulosGPU,3fases,CPUnoturna, Pepin visível e pick correto com talk handler, witness mapa/SOL/atores/RNG/reset, R12baús e armadilhas, objetos/portas/HUD/C1/saveLoad nativos. Teste de seleção/handler não é nova simulação do diálogo de Pepin. Mesmo código com assetsR12 sem modelo mantém proxy: PASS125431. VerifyOnly436; recibo diagnostics/healer-house-20261012-r13/DELIVERY.json, source-r13 e capturas run-246900231452000. Revisão humana/FPS sustentado/andares posteriores pendentes; sem instalação habitual, web, Git ou Panvia. README humano preservado.
