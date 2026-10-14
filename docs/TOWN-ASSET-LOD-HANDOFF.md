# Entrega isolada: masters, materiais e LOD

Frente `01a11af5-6ed5-7663-9553-9ef0fe2e34ee`, 8 de outubro de 2026. Motivo: requisito explícito de preservar modelos bonitos perto da câmera e auditoria de Adria que separou redução remesh de perda de material no transporte. A entrega usa a fila existente; não reabre a cabana congelada nem promove os outros objetos. O principal permanece responsável por guia operacional, build compartilhado, integração, instalação e Git. Este documento é um recibo de entrega, não outra fila de produção.

## Resultado executável

| Parte | Implementação | Evidência e limite |
| --- | --- | --- |
| Transporte D3DMESH2 | `Source/engine/render/town_model_v2.hpp/.cpp`, `tools/export_town_model_v2.py`, `tools/validate_town_asset_lods.py` | 42 verificações C++ e 20 testes Python, incluindo parser C++ nativo. Packet API Adria de 25.359.632 bytes lido pelo parser real com 5.134 faces e três imagens PBR originais. Jogo não consome v2. |
| Seleção de detalhe | `Source/engine/render/town_asset_lod.hpp/.cpp`, `tools/town_asset_lod_smoke.cpp` | 6.913 verificações por configuração, debug e release MSVC. Erro e diâmetro, histerese, identidade e residência separados. Não é chamado pela partida. |
| Prova de material denso | `tools/transfer_town_material.py`, `tools/test_transfer_town_material.py` | 13 testes; dois GLBs privados com todas as 3.491.844 faces e imagens originais. Leitura independente compara 10.475.532 cantos por candidato sem diferença de XYZ/normal. Aparência ainda não tem comparabilidade comprovada. |
| Revisão Godot | `editor/godot/review/lod_review.tscn`, novos scripts em `editor/godot/addons/asset_lod_review/` | 84 verificações headless, 22 do manifesto conjunto real e 28 capturas privadas, com câmera comum e hashes reconferidos. GLBs originais são importados pelo Godot; o viewer não desenha o packet v2 nem executa recortes da engine. |

Contratos responsáveis: [transporte/manifesto](TOWN-MODEL-V2.md), [seletor](TOWN-ASSET-LOD-SELECTOR.md), [Godot](TOWN-ASSET-LOD-REVIEW.md) e [prova Adria](ADRIA-MATERIAL-TRANSFER.md). Teste do transporte em `tools/town_model_v2_smoke.cpp`, runner isolado `tools/run_town_model_v2_smoke.ps1` e suíte `tools/test_town_model_v2.py`. Dependências reaproveitadas: PicoSHA2 já no repositório; Python NumPy/Pillow e SciPy para transferência; Godot local para revisão. Nenhuma dependência nova foi instalada.

## Prova de Adria e decisão

O master geométrico sem UV (`9d3a4546…`) e donor API (`1a85276c…`) ficaram intactos. A transferência baricêntrica para a superfície mais próxima gerou os candidatos PBR (`dce83134…`) e sem normal map (`eba18de3…`). São fontes de **inspeção**, não drawables aprovados ou substitutos da revisão instalada.

Na projeção completa, 7,80% dos vértices excederam distância de 0,5% da diagonal e 45,70% excederam 60° de desacordo de normal; a união atingiu 48,03%. Esses números contam vértices e não representam porcentagem de área visível. 16,918% das faces atravessavam ilhas UV e precisaram de registros separados; 762.887 cantos foram clampados à aresta/vértice do donor. Não se removeu geometria para esconder esses casos.

As vistas Godot mostram detalhe geométrico mais rico nos frascos, tábuas e bordas. Também mostram regiões de UV comprimido, remendos e diferenças de cor/reflexo nas prateleiras/frascos. Remover a referência ao normal map altera highlights, sem corrigir as correspondências. Parte das regiões lisas/cinzas também existe no donor; não se atribui toda diferença à transferência. **Preservação geométrica passou; recuperação visual comparável ainda não está comprovada.** A dependência concreta é correspondência entre superfícies/ilhas e rebake de material/normal no master em cópias de autoria.

Evidências privadas, relativas ao workspace pai:

- `diagnostics/asset-lod-20261008/adria-transfer/full-r1/`: `report.json`, candidatos e `mapping.npz`. As verificações independentes estão um nível acima, em `diagnostics/asset-lod-20261008/adria-transfer/preservation-validation.json` e `diagnostics/asset-lod-20261008/adria-transfer/risk-regions.json`.
- `diagnostics/asset-lod-20261008/godot-review/`: GLBs baseline copiados, manifestos privados, 28 PNGs e JSONs de evidência da câmera/hash. O conjunto `adria-v2-handoff*.png` abre o manifesto real v2 com original/transferência para inspeção e referência API.
- `diagnostics/asset-lod-20261008/model-v2-core/`: compilação, 42 checks e parser CLI.
- `diagnostics/asset-lod-20261008/lod-core/`: 6.913 checks debug/release e recibo de compilação.
- `diagnostics/asset-lod-20261008/v2-package/adria-review.json`: original geométrico e transferência somente para inspeção; API final como referência drawable0. `validation.json` comprova identidade/estrutura e parser C++ do packet, mantendo `approvedForGame=false`.

## Diagnóstico adicional registrado em 9 de outubro de 2026

A auditoria privada da frente de Configurações, `diagnostics/mesh-fidelity-review-20261009/20261009T045501362Z/audit.json`, foi conferida pelo principal e novamente por leitura nesta frente. SHA-256: `6baf085a8185122f093cb1a0f902a1f653376256b5f0a149c2d0842269859aef`. Ela passa a integrar a evidência do defeito/requisito da próxima entrega já reservada neste handoff, sem abrir outra fila ou implementação.

O relatório confirma Adria **3.491.844 → 5.134 faces** no remesh da API e poço **748.858 → 2.992** na derivação local. Os 12 vínculos selecionados usam derivados D3DMESH1, com hashes selecionados e comprimento binário conferidos. Adria selecionada: SHA `ba4b553f3627bc46f4b85b06eb072963f95f79e513f635807176765bf954b845`; poço selecionado: SHA `f8f25dbfb49dd7a82ce4d7795bd71919b4ead773ce22165de37bc8bb59e83563`. A geometria já está reduzida nesses arquivos; **não existe seleção por distância instalada que recupere o master quando a câmera se aproxima**. O contrato v2 e o seletor entregues acima continuam isolados.

O defeito registrado é a perda de detalhe próximo, somada às limitações do D3DMESH1 para normais suaves/PBR. A próxima entrega da mesma reserva precisa conservar os masters imutáveis e comprovar UV/material, silhueta e aberturas utilizáveis no detalhe próximo antes de promover derivados, mantendo redução distante explícita e integração coordenada com o principal. A contagem de faces não fornece um limite conservador de erro visual. O master geométrico de Adria continua sem UV; o experimento materializado ainda não demonstra qualidade comparável. Não instalar nenhuma dessas fontes densas por consequência deste diagnóstico.

Limite da evidência: não houve nova comparação visual sincronizada do poço, restauração de qualidade, render da partida ou medição de FPS nesta passagem. Este registro não altera seleção, conversor, renderer, código, build ou perfil; não executa redução, geração/API, instalação ou promoção artística. Os gates e responsáveis da fronteira abaixo permanecem os mesmos. O estado operacional já registrado pelo principal está em [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md).

## Fronteira de integração para o principal

1. Adicionar os novos módulos aos targets somente no cache/build coordenado. Os testes desta entrega compilam em diagnósticos privados; nada foi instalado ou commitado por esta frente.
2. Adaptar manifesto privado e capacidades explícitas. Conferir ID/variante/revisão/hash, âncora float32 comum, fit uniforme e pipeline de aberturas. D3DMESH1 continua separado. Não converter silenciosamente v2 em albedo2048/flat normals ou master sem UV.
3. O contrato de buffers indexados GPU em desenvolvimento (`town_gpu_mesh.hpp`) ainda declara iluminação flat e normais iguais por triângulo. **Não marcar v2 smooth/PBR como compatível com esse backend automaticamente.** A preservação no transporte não implementa o shading. Alpha/sampler/textura e capacidade da pipeline também precisam de gates.
4. Medir um limite conservador de erro dos derivados artísticos, preservar aberturas e material, calcular projeção máxima sobre bounds/near plane e chamar o seletor com estado por instância. O módulo separa nível desejado da proposta residente; fallback inadequado exige política explícita. Nenhum erro foi inventado a partir do número de faces.
5. Somente depois comparar perto/longe/primeira pessoa e seleção na partida. A recuperação material de Adria precisa resolver os defeitos acima antes de promover um near denso. O packet API atual só prova transporte, mantendo o detalhe baixo conhecido.

Foram preservados os arquivos concorrentes. Esta frente não editou `town_view.*`, `town_gpu.*`, `town_scene.*`, `options.*`, `diablo.cpp`, CMake/build compartilhado, catálogos/inspector existentes ou o guia operacional. Não houve POST Meshy/Tripo, compra, geração nova, publicação ou substituição da cabana. Os arquivos privados permanecem fora do Git.

## Texto para o registro operacional pelo principal

Frente masters/LOD entregou módulos isolados D3DMESH2 + manifesto privado, exportação/validação, seletor por erro/tamanho com histerese e revisão Godot independente. Passaram 42 verificações do parser, 20 testes Python do transporte/manifesto, 6.913 do seletor por configuração e 13 da transferência. Adria preservou 3.491.844 faces e todos os XYZ/normais/imagens nos candidatos privados, mas correspondências/costuras e rebake continuam impedindo afirmar qualidade comparável. O jogo segue usando sua revisão/formato anteriores; integração de build, capabilities, shading PBR/smooth, cache e seleção LOD na partida permanecem pendentes. Detalhe em `TOWN-ASSET-LOD-HANDOFF.md`; evidência privada em `diagnostics/asset-lod-20261008/`. Nenhum modelo foi instalado, aceito ou publicado por esta entrega.
