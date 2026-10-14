# Portas e arcos da Catedral — R10

Douglas forneceu `wooden+arch+door+3d+model.glb` e `stone+arch+3d+model (1).glb` em 12/10/2026. Os originais estão preservados em `local-assets/own-art/props/doors-user-r1`. O derivado GLB `wooden-arch-door-open-close-r1.glb` contém os clips Open/Close de 350 ms; o jogo apresenta o movimento pelo estado nativo, sem precisar de um conversor instalado.

## Geometria e seleção

A folha foi alinhada pelo eixo horizontal das dobradiças, pois o arquivo original estava girado na diagonal. As escalas são uniformes: 2,4 para madeira e 2,8 para pedra. Texturas de 1024×1024 e UVs foram preservados. O derivado usa 7.389 faces da folha e 6.876 do arco; uma face degenerada da folha não satisfazia a tolerância do importador. Nenhum master foi reduzido ou substituído.

O loader verifica os hashes dos dois D3DMESH1. Só quando ambos carregam são ocultadas a folha/moldura técnicas e a sobreposição CLX da célula da porta. Ausência ou rejeição mantém a apresentação nativa anterior. Os módulos existentes `technical.cathedral.door-leaf` e `technical.cathedral.door-frame` continuam sendo os donos semânticos; não há uma nova entidade de simulação.

## Estado e ciclo de vida

`PrepareDoorVisual` carrega a geometria e prepara dois buffers imutáveis. `PrepareDoorVisualStates` observa o snapshot validado, com estado por slot, célula, orientação e época de nível. Fechada corresponde a 0; aberta ou bloqueada corresponde a 1. Uma mudança inicia interpolação suave de 350 ms a partir da pose corrente, incluindo reversão. A folha gira 90° sobre a dobradiça nativa; o arco fica parado. O estado final não faz loop.

Um nível ou save já aberto começa na pose aberta final. Nova época limpa os estados de apresentação; `ResetTownViewResources` também descarta modelos e identidades. A GPU reenvia buffers apenas após ausência/evicção/reset de recursos. O pré-desenho prepara a textura indexada após a invalidação do cache; o desenho usa os recursos preparados. A instância conserva o pick do objeto nativo, a luz da célula e o recorte da câmera. CPU usa a mesma geometria transformada; GPU compartilha dois buffers entre todas as portas.

Não foram alterados `OperateDoor`, `SyncOpObject`, SOL, comandos, sons, visão, loot, RNG ou serialização. Colisão nativa muda no momento da operação; o giro é somente apresentação. A câmera conserva o contrato anterior de ocultar arquitetura em terceira pessoa.

## Evidência e limites

EXE `127ff633c9cb`, prévia local R10, 308 arquivos congelados. Gate PASS125319: portas reais Left/Right, operação nativa Open/Close, recusa de fechamento com ocupação privada, clique/pick da folha, pose intermediária/final, CPU/GPU, warm sem reupload e witness nativo por desenho. A fixture usa herói e perfil sintéticos, com helpers nativos de posição/luz; não é um teste de partida humana. R9 e os materiais/objetos/HUD anteriores foram retestados. Mesmo código com assets R9 sem os novos modelos passou PASS125186.

Recibo e capturas estão em `diagnostics/doors-integration-20261012-r1/DELIVERY.json`. O primeiro gate procurou portas fora do trecho selecionado e foi preservado como falha de fixture; o teste passou após posicionar o herói sintético junto a portas reais. O erro de include na compilação do gate também foi preservado.

C1 usa paleta e iluminação nativas, não PBR completo. A qualificação é Tristram/Catedral 1; demais andares, FPS interativo sustentado e aprovação visual humana permanecem pendentes. Esta entrega não atualizou a web nem publicou Git/Panvia. Arcos decorativos sem objeto de porta continuam no caminho existente.


## R15 — recuo das paredes, 12/10/2026

Defeito relatado: a alvenaria cobria as laterais do arco selecionado. O gerador agora reserva um vão visual de três células de largura (célula da porta e uma vizinha de cada lado), profundidade 3 e altura 2,8, incluindo encontros transversais em Left/Right. SubtractBox corta paredes e encontros e gera suas faces de término; o arco mantém escala, malha e texturas aprovadas. O vão permanece fechado/aberto/bloqueado, sem pop de parede. A revisão de assinatura da geometria sobe de 2 para 3; regiões reconstruídas preservam identidade de origem, picks e material.

O kit substitui também o acabamento CLX antigo na vizinhança imediata 3×3 da porta, condicionado ao carregamento dos modelos; especiais distantes e fallback CLX permanecem. O recuo técnico do gerador é aplicado também ao fallback procedural. Colisão/SOL, comandos nativos, dPiece, visão, mapa e saves não são editados pelo renderer. Gates verificam ausência de interseção positiva da alvenaria com o vão em cada estado e orientação, além de animação, clique, colisão nativa e CPU/GPU. Capturas de frente/topo e RGB pertencem ao recibo R15; aprovação visual humana continua separada.

A reserva também exclui ArchFrame dos mega-tiles 11/12 cujo centro pertence ao vão 3×3 de uma porta nativa: esse acabamento procedural duplicava o arco do kit e continuava visível após o corte de alvenaria. Arcos fora desse entorno permanecem. Capturas intermediárias e logs antes de cada refinamento são preservados em diagnostics/door-arch-clearance-20261012-r15.
