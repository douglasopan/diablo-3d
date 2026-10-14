# Transporte estático v2 e famílias de LOD

Incremento isolado de 8 de outubro de 2026, motivado pelo defeito documentado na auditoria de Adria: o master de 3.491.844 faces tem normais, mas não UVs/material; a saída texturizada tem 5.134 faces; D3DMESH1 não transporta normais suaves nem PBR. O objetivo continua sendo qualidade perto da câmera e derivados leves à distância, preservando as revisões. A [fila vigente](PROJECT-EXECUTION.md), os IDs do registro e as regras de [aprovação](ASSET-APPROVAL.md) permanecem autoritativos.

## Implementado e limites

- `town_model_v2.*`: parser transacional, indexado e independente de cena/renderizador, com SHA-256 obrigatório, limites, normais/tangentes/UV/cor, múltiplas primitivas/materiais e imagens PNG/JPEG originais.
- `export_town_model_v2.py`: exportação offline de GLB estático autocontido, sem simplificar, redimensionar imagens ou deformar para limites de substitutos. Transformações originais dos nodes são aplicadas; a âncora comum é metadado, não recentragem automática. O exportador não sobrescreve packet nem sidecar.
- `validate_town_asset_lods.py`: valida manifesto privado, identidades, referência geométrica, âncora, fit uniforme e compatibilidade de aberturas. Pode chamar o parser C++ real. Não mede qualidade visual, não transforma alegações de erro em medidas e não instala assets.
- [Seletor](TOWN-ASSET-LOD-SELECTOR.md) por erro e diâmetro na tela com histerese e estado por instância; [revisão Godot](TOWN-ASSET-LOD-REVIEW.md) independente do inspector existente.

O jogo atual **não carrega D3DMESH2 nem chama esse seletor**. Nenhum limite do backend foi removido. PBR é preservado no transporte; sua renderização na engine, buffers estáticos, tangentes MikkTSpace, cache, orçamento e integração CPU/GPU continuam pendentes. O Godot importa os GLBs originais e não lê o packet v2; renderizar um GLB no Godot não comprova aparência do packet no jogo. Nenhum objeto ganhou aceitação artística ou seleção nova.

## Manifesto privado de revisão

O manifesto associa arquivos de uma revisão; não é catálogo concorrente, modpack público ou instalador. Caminhos podem ser relativos ao manifesto ou absolutos locais; os arquivos GLB devem ser autocontidos. SHA-256 é hexadecimal minúsculo dos bytes exatos. Use o ID/variante já registrados. `master-geometry` sempre é somente inspeção, mesmo depois de um experimento de transferência de UV. A promoção de um master materializado a `drawable-lod` é uma decisão explícita posterior.

```json
{
  "schemaVersion": 2,
  "assetId": "tristram.arch.house-adria",
  "variant": "adria-open-shed",
  "revision": "revisao-explicita",
  "anchor": {"position": [0, 0, 0], "upAxis": "Y"},
  "openings": {
    "pipelineId": "pipeline-identificada",
    "compatibility": "native-unchanged"
  },
  "fit": {"uniformScale": 1, "yawRadians": 0, "translation": [0, 0, 0]},
  "geometricReferenceSha256": "SHA256_DO_DRAWABLE_0",
  "sources": [
    {"role": "master-geometry", "path": "master.glb", "sha256": "SHA256_MASTER"},
    {
      "role": "drawable-lod", "path": "near.glb", "sha256": "SHA256_DO_DRAWABLE_0",
      "lodIndex": 0, "geometricError": 0, "errorVerified": true,
      "maxDiameterPixels": 0,
      "runtimePath": "near.d3d2", "runtimeSha256": "SHA256_PACKET"
    },
    {
      "role": "drawable-lod", "path": "far.glb", "sha256": "SHA256_FAR",
      "lodIndex": 1, "geometricError": 0.01, "errorVerified": false,
      "maxDiameterPixels": 120, "openingEquivalenceVerified": false
    }
  ]
}
```

Os SHA simbólicos no exemplo devem ser substituídos por hashes reais; o validador os rejeita. A âncora JSON é canonicalizada em float32 para comparar com o packet; o sidecar exportado registra esses valores canônicos. LODs de desenho são contíguos a partir de zero, com erro não decrescente e diâmetros máximos coarse não crescentes. `geometricError` é um **limite conservador de desvio da superfície em unidades do objeto**, relativo ao GLB identificado em `geometricReferenceSha256`, antes do fit uniforme. Não derive erro de contagem de faces, distância média ou uma amostra de vértices. LOD0 tem erro zero relativo a si próprio. Isso **não** afirma que ele equivale ao master geométrico mais denso. A referência muda explicitamente quando um near melhor for aprovado.

`maxDiameterPixels=0`/ausente é ilimitado somente no LOD0; o adapter deve convertê-lo para `+infinity` no seletor C++. Coarse exige valor positivo. `errorVerified=false` ou `openingEquivalenceVerified=false` impede elegibilidade automática. A evidência da medição e a revisão de silhueta, materiais, UVs, normais, aberturas e seleção devem acompanhar qualquer aprovação desses flags; o validador verifica a declaração, não certifica sua veracidade. Amostras do experimento de Adria não constituem um limite conservador completo.

`openings.compatibility` admite `native-unchanged`, `external-code-required` e `baked-reviewed`. O pipeline é identidade do conjunto de aberturas/interior necessário; não implica que um executável arbitrário o implemente. Um SHA-256 opcional identifica um artefato da pipeline. Pipeline externa exige capacidade declarada do executável antes de desenhar; baked exige revisão das aberturas reais. Âncora, proporções e pipeline são comuns à família. Nenhum fit individual de LOD nem escala independente XYZ é admitido. Colisão e IDs de interação continuam nativos.

## D3DMESH2: contrato binário experimental fixo

Números little-endian, float32 IEEE754, sem padding de structs. O arquivo inteiro deve corresponder ao SHA esperado antes do parsing. A revisão v2 é fixa; outro layout exige novo magic/versão, nunca interpretação permissiva de bytes antigos.

| Ordem | Dados |
| --- | --- |
| Header | 8 bytes ASCII `D3DMESH2`; 6 uint32: tangentMode, vertexCount, indexCount, primitiveCount, materialCount, textureCount |
| Espaço | 9 float32: anchor XYZ, boundsMin XYZ, boundsMax XYZ; uint32 openingsMode |
| Identidade | Quatro strings: assetId, variant, revision, openingsPipeline; cada uma uint32 byteLength + ASCII imprimível de 1..256 bytes; 32 bytes sourceGLB SHA-256 |
| Vértices | vertexCount × 16 float32: position XYZ, normal XYZ, rawUV XY, tangent XYZW, color RGBA |
| Índices | indexCount × uint32, triângulos; preserva winding após transforms espelhados |
| Primitivas | primitiveCount × 3 uint32: firstIndex, indexCount, materialIndex; partição contígua completa |
| Materiais | materialCount × 196 bytes conforme abaixo |
| Texturas | textureCount registros de header + payload conforme abaixo; fim exato do arquivo |

Material: 12 float32 (`baseColor RGBA`, `emissive RGB`, metallic, roughness, alphaCutoff, normalScale, occlusionStrength), uint32 alphaMode (0 OPAQUE/1 MASK/2 BLEND), uint32 doubleSided (0/1), cinco textureInfos. Cada textureInfo tem int32 textureIndex (-1 ausente), uint32 texCoord (somente zero), cinco float32 (`offset XY`, `scale XY`, rotation radianos). Slots: baseColor, metallicRoughness, normal, occlusion, emissive. Texturas baseColor/emissive usam sRGB; MR/normal/occlusion usam dados lineares. Metallic está em B, roughness G e occlusion R. Alpha e culling permanecem explícitos.

Textura: oito uint32 (mime, width, height, wrapS, wrapT, minFilter, magFilter, encodedByteCount), 32 bytes SHA-256 do payload, bytes originais. Mime: 0 PNG, 1 JPEG. Samplers conservam valores glTF. O transporte não descarta alpha, não multiplica fatores no albedo nem recompõe mapas PBR. O parser confere hashes, assinaturas e dimensões PNG/JPEG; o decoder do consumidor ainda deve rejeitar imagens malformadas e conferir o tamanho decodificado antes de upload. Não há decoder, mipmaps ou orçamento global de imagens neste incremento.

Limites do parser: arquivo até 512 MiB; 3..8.000.000 vértices; 3..24.000.000 índices divisíveis por três; 1..4.096 primitivas; 1..256 materiais; 0..64 texturas, até 8192 por eixo e 64 MiB codificados por textura. Atributos, âncora e transforms são finitos e de magnitude até 1.000.000; cores/fatores glTF nos seus intervalos; normais e tangentes unitárias/ortogonais com tolerância 0,003 no quadrado/componente. Bounds coincidem exatamente com o mínimo/máximo dos float32 exportados. Índices inválidos, faces degeneradas e bytes excedentes são rejeitados. Esses limites são gates de transporte para revisão, **não** orçamento ou promessa de desempenho do renderer.

UVs podem repetir fora de 0..1; cada slot conserva seu transform `KHR_texture_transform`, aplicado ao UV bruto. Outros conjuntos UV, material extensions, compressed textures, morphs, skins/animações e atributos adicionais são recusados para impedir perda silenciosa. Fonte original sempre fica preservada. `tangentMode=1` conserva frames fornecidos (transformados corretamente); `2` indica tangentes calculadas a partir de UVs e normais, incluindo frames estáveis onde UV é degenerado. Esse método não é MikkTSpace e não comprova fidelidade de normal maps. O modo deve permanecer visível na revisão.

## Compatibilidade e fronteira de integração

`ParseTownModelV2` nunca lê D3DMESH1. O loader v1 existente continua intacto. V2 requer capacidades de indexação, material/alpha/sampler, normais/UV e pipeline de aberturas; não fazer downgrade automático para RGB2048, flat normals ou blend opaco. Em recurso não suportado ou packet inválido, manter o asset selecionado anterior e registrar o motivo. O parser deixa o objeto de saída intacto quando falha.

O principal ainda precisa adicionar os módulos ao build e fazer um adapter de manifesto/capacidades, identidade por revisão e estado LOD por instância. O backend GPU é outra frente; o contrato deverá ser consumido explicitamente por ela. Política de residência fornece ao seletor disponibilidade separada da qualidade: fallback inadequado é diagnosticado e o chamador decide adiar/usar a revisão anterior. Não trocar a referência artística para cumprir orçamento, não usar master sem UV como drawable e não remover objetos para declarar gate concluído.

## Executar a validação isolada

```powershell
./tools/run_town_model_v2_smoke.ps1 -OutputDirectory <pasta-diagnostica-privada>
$env:TOWN_MODEL_V2_VALIDATOR = '<pasta-diagnostica-privada>/town_model_v2_smoke.exe'
python tools/test_town_model_v2.py -v
python tools/export_town_model_v2.py <fonte.glb> --output <novo.d3d2> --asset-id <id-existente> --variant <variante> --revision <revisao>
python tools/validate_town_asset_lods.py <manifesto-privado.json> --runtime-validator $env:TOWN_MODEL_V2_VALIDATOR
```

MSVC/SDK para o parser; Python com NumPy/Pillow para as ferramentas. O script compila somente em pasta diagnóstica, sem CMake/cache compartilhado. Passaram 42 verificações C++ sintéticas e 20 testes Python de transform espelhado, normals/tangents, UV repetido/transform, RGBA/PBR byte-exato, samplers, JPEG/PNG, múltiplos materiais, hash/âncora decimal, master preservado, rejeição de perdas e parser nativo. A cópia API final de Adria foi transportada sem reduzir as 5.134 faces: 10.404 vértices, três payloads originais (4096², 4096², 2048²) e 25.359.632 bytes. Isso valida transporte do modelo API e não recupera o detalhe perdido. [Prova de transferência](ADRIA-MATERIAL-TRANSFER.md) registra o resultado denso e seus defeitos separadamente.
