# Godot: contrato do editor e do jogo

Entrega inicial autorizada em 8 de outubro de 2026: Godot como ferramenta externa de inspeção e autoria de arquitetura estática de Tristram. O jogo continua no DevilutionX. Este contrato complementa o guia PROJECT-EXECUTION; não é outro catálogo de assets.

## Coordenadas e identidade

Uma unidade Godot equivale a uma célula nativa: X=x, Y=height, Z=y do mapa. Cada edifício conserva seu `nativeMin`/`nativeMax`, `instanceId`, `assetId` e `variantId` provenientes de assets/registry.json. A camada de colisão é uma referência bloqueada; o editor não escreve na simulação. Geometria pode ser alterada dentro desse vínculo. Reposicionar visualmente um edifício não desloca sua colisão e exige aviso explícito no editor.

O snapshot local `local/town-snapshot.json` tem `format: d3d.town-snapshot`, `schemaVersion: 1`, `edition: retail`, `gridSize: [112,112]`, `collision: [[x,z],...]`, `models: [...]`. Cada model tem `instanceId`, `assetId`, `variantId`, `kind` (inteiro), `nativeMin: [x,z]`, `nativeMax: [x,z]`, `sha256`, `revision`, `external`, `texture` (caminho relativo PNG, ou vazio), `triangles: [[x,y,z,u,v, x,y,z,u,v, x,y,z,u,v, material,role,detail], ...]`. Triângulos são os efetivamente montados pelo jogo, incluindo recortes e interior; não são o PNG de prévia do Meshy. As coordenadas do snapshot são absolutas. Modelos, texturas e dados extraídos ficam em `local/`, ignorado pelo Git.

## Pacote exportado

O editor salva a cena Godot em `local/tristram.tscn` e exporta um pacote local para `local/export/`. O pacote contém `d3d-maps/tristram.ini`, modelos em `d3d-models/editor/<instanceId>.d3d` e recibo JSON. A versão inicial usa D3DMESH1, descrito em town_model_import.hpp: malha estática, uma textura RGB atlas, UV 0..1, até 20.000 triângulos/2.048 pixels por lado. Materiais avançados, animação e transparência não suportados devem causar erro de exportação, nunca ser descartados silenciosamente. Uma edição em malha pode incorporar transformações nos vértices; os dados nativos permanecem fixos.

```ini
[Scene]
format=d3d.town-map
schemaVersion=1
edition=retail
instance=well

[well]
assetId=tristram.arch.well
variantId=clean-water
nativeMin=60,70
nativeMax=61,71
model=d3d-models/editor/well.d3d
sha256=<64 caracteres hexadecimais>
revision=<revisao explicita>
interior=none
```

`instance` é uma chave repetível. Somente instâncias nativas conhecidas podem ser substituídas; IDs duplicados, vínculo diferente, caminhos fora de `d3d-models/editor/`, números inválidos e hash divergente são recusados. Manifesto ausente mantém o cenário habitual. Falha de um override mantém a baseline daquela instância e registra o motivo. O loader verifica o hash dos bytes realmente decodificados. Não há seleção por data de arquivo.

`interior=none` informa que a geometria exportada é completa; o jogo não cria novamente os recortes medidos da cabana. O editor inicial mantém a cabana com suas luzes nativas como **herdada**, sem override nesta versão. Luzes/fogo da cabana ainda dependem do adjunto do jogo: a exportação inicial recusa substituir uma instância com fontes de fogo, em vez de perder esse comportamento. Esta limitação será removida por uma evolução versionada do formato, antes de promover autoria completa de interiores.

## Instâncias de arquitetura (v1)

| instanceId | assetId | variantId | nativeMin | nativeMax |
| --- | --- | --- | --- | --- |
| tavern-main | tristram.arch.tavern | main-with-wing | 46,54 | 53,63 |
| tavern-wing | tristram.arch.tavern | main-with-wing | 53,56 | 56,61 |
| smithy-house | tristram.arch.smithy | main-with-open-bay | 60,56 | 71,60 |
| smithy-forge | tristram.arch.smithy | main-with-open-bay | 60,60 | 63,63 |
| house-gillian | tristram.arch.house-common | gillian | 36,64 | 42,68 |
| house-pepin | tristram.arch.house-pepin | pepin-hip-roof | 46,74 | 55,80 |
| house-adria | tristram.arch.house-adria | adria-open-shed | 74,16 | 79,21 |
| house-north | tristram.arch.house-common | north | 46,40 | 54,46 |
| house-southeast | tristram.arch.house-common | farnham | 67,78 | 72,82 |
| cabin-west | tristram.arch.cabin | west-short | 26,48 | 30,52 |
| cabin-east | tristram.arch.cabin | east-long | 70,66 | 74,72 |
| well | tristram.arch.well | clean-water / poisoned-water | 60,70 | 61,71 |
| cathedral | tristram.arch.cathedral-exterior | exterior-with-bell-tower | 15,14 | 29,28 |
| catacombs-entrance | tristram.arch.catacombs-entrance | closed-warp / open-warp | 48,18 | 50,21 |

Os IDs foram conferidos contra o catálogo. Taverna e ferraria têm dois grupos vinculados à mesma variante de conjunto. Água e entrada refletem o estado nativo: uma exportação de outra variante é recusada nesse estado. Isso não significa nova reserva ou aceitação artística. A revisão visual feita no Godot é auxiliar. A paleta e a iluminação do jogo ainda precisam de comparação na câmera original forçando geometria e em 360 graus. Home usa o backend original e não é prova de fidelidade da malha.

## Referência do terreno

`ground` é uma extensão opcional do snapshot: `textures: [{key,path,width,height}]`, `tiles: [[x,z,key],...]`, `source` e `exportLocked: true`. As texturas RGBA vêm de SceneGround/FallbackGround do renderizador real, incluindo a limpeza já implementada de sombras pintadas. O chão sob células de cenário sólido usa referência de grama; não é um novo sistema de terreno. Cada quad usa os mesmos cantos inversamente projetados do jogo: `(x-1.46875,0,z-0.46875)`, `(x-0.46875,0,z-1.46875)`, `(x+0.53125,0,z-0.46875)`, `(x-0.46875,0,z+0.53125)`, com UV `(0,0),(1,0),(1,1),(0,1)`. Essa camada é bloqueada e excluída da exportação de arquitetura.

## Migração do jogo inteiro

O projeto Godot entregue é um editor, não uma versão jogável portada. Uma migração completa precisa tratar leitura dos arquivos locais, mapa procedural vivo, combate, inventário, quests, saves, rede, interface, áudio, tempo de animação e todas as plataformas suportadas. Importar os GLBs cobre apenas parte da apresentação.

O caminho de menor retrabalho a avaliar é conservar inicialmente a simulação C++ e expor um contrato de leitura/comandos para um cliente Godot, possivelmente por GDExtension. Isso exige separar o ciclo de vida da simulação das dependências SDL, da UI e do renderer; ainda não existe essa separação geral. Antes de decidir pela migração, um piloto deverá carregar uma partida real, movimentar/combater, salvar/reabrir e sincronizar um andar procedural. Só depois dessa evidência se estima o restante. A autoria em unidades nativas, IDs existentes e GLB preservados permite reaproveitar os assets caso essa decisão seja tomada.

## Responsabilidades nesta entrega

- Chat do editor: `editor/godot/**` e guia de uso do editor; sem staging/commit enquanto a integração ocorre.
- Ponte C++: `town_editor_map.*`, importador e pontos de integração em town_scene/CMake; preservar alterações anteriores de aberturas.
- Chat do jogo: snapshot diagnóstico, preparação local/launchers, build, integração, evidências e guia operacional.

Não publicar modelos locais, extrações, saves, credenciais ou executáveis. A aplicação de pacotes é explícita, com backup/recibo em perfil de revisão; não sobrescreve uma partida aberta.
