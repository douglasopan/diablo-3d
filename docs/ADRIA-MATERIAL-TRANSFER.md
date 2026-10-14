# Experimento privado de transferência de material — Adria

Este incremento responde à perda de detalhe entre o master geométrico de `tristram.arch.house-adria` e o resultado texturizado da API. Não muda a revisão selecionada, o catálogo, a cena, o perfil, o executável ou a aceitação artística. O master permanece preservado. O candidato resultante é material de revisão e não está pronto para instalação.

## Fontes e hipótese

| Papel | Arquivo | Triângulos | SHA-256 |
| --- | --- | ---: | --- |
| Master geométrico | `pre_remeshed_model.glb` | 3.491.844 | `9d3a454628a37f3443c973fd12080f2a4e8278644ac50eb8326f8e493d4ee046` |
| Doador de UV/material | `model.glb` | 5.134 | `1a85276c7f6afd80ed6b757cfe5dc3a2289d4a5f3eadd57a9bae50c6f536d95a` |

Os dois arquivos pertencem à mesma tarefa local preservada. O primeiro contém posições/normais e não tem UV nem texturas. O segundo contém UV e material PBR, mas só conserva 0,14703% das faces. Não existe um master denso texturizado pronto nessa cadeia.

A hipótese testada é que os arquivos compartilham coordenadas glTF suficientes para projetar UVs do resultado final sobre a geometria densa. A transformação comum é a identidade; não há ajuste independente de escala, deformação por eixo, ICP ou alinhamento de imagens. A textura transferida continua limitada à informação existente no doador de 5.134 faces.

## Implementação executável

[`tools/transfer_town_material.py`](../tools/transfer_town_material.py) aceita dois GLBs estáticos locais. Requer NumPy/SciPy e reutiliza a leitura offline de `inspect_glb.py`. Não realiza downloads nem chamadas de API.

1. Lê o mundo glTF, posições, normais e UVs explícitos. Recusa skins, animações, morph targets, features não suportadas e ausência de atributos necessários.
2. Procura a superfície mais próxima de cada vértice do master e calcula coordenadas baricêntricas no triângulo doador. A busca usa árvores de centroides em grupos por raio; enumera todos os candidatos que podem vencer o limite atual. A escolha não é uma aproximação pelo centroide mais próximo.
3. Interpola UV no triângulo escolhido e mede distância e ângulo entre a normal do master e a normal interpolada do doador. Os limites configuráveis sinalizam risco: **não descartam faces nem provam correspondência semântica**.
4. Identifica ilhas de UV por continuidade de posição, UV e material nos lados compartilhados. Quando os três cantos de uma face densa chegam a ilhas diferentes, escolhe um triângulo doador na posição do centro da face, projeta os três cantos nele e duplica somente registros de vértice. Isso evita interpolar através de ilhas sem relação no atlas, mas desloca a costura para as arestas existentes e pode comprimir UVs.
5. Exporta GLB novo com todas as faces e normais do master. Copia os descritores de material e os bytes das imagens embutidas. Uma variante opcional omite somente `normalTexture`, permitindo avaliar o detalhe geométrico sem reaplicar o normal map do remesh.

Exemplo de auditoria, executado a partir do repositório:

```powershell
python tools/transfer_town_material.py `
  ../models/meshy/tristram-20261008/adria-model-r1/downloads/model.glb `
  ../models/meshy/tristram-20261008/adria-model-r1/downloads/pre_remeshed_model.glb `
  ../diagnostics/asset-lod-20261008/adria-transfer/amostra-nova `
  --sample-vertices 10000
```

Para processar o master inteiro e escrever candidatos privados, escolha outro diretório novo e substitua a última opção por `--export --no-normal-variant`. `--max-distance-fraction` controla a distância dividida pela diagonal do master (padrão 0,005); `--max-normal-angle` controla o ângulo sinalizado (padrão 60 graus). `--batch-size` limita lotes de consulta.

O programa recusa substituir evidências com os mesmos nomes. Não grava os arquivos de entrada. `mapping.npz` identifica cada vértice consultado, triângulo doador, pesos baricêntricos, UV, distância, ângulo e ilha de UV. `report.json` contém hashes, limites, distribuições, preservação e os arquivos candidatos. Essas saídas, GLBs, imagens e capturas são privadas e ficam fora do repositório.

## Validação e resultado

Os 13 testes sintéticos em [`tools/test_transfer_town_material.py`](../tools/test_transfer_town_material.py) passaram. Incluem solução analítica no interior/lados/pontos degenerados, busca acelerada comparada à busca exaustiva, triângulo grande cujo centroide está distante, continuidade de ilhas, transferência completa através de uma costura, preservação de posições/normais não unitárias e imagens, variante sem normal map, sinalização de normal oposta, ausência de UV, normal inválida e proteção contra sobrescrita.

A amostra inicial determinística de 10.000 vértices terminou em 2,55 segundos. A distância mediana foi 0,1051% da diagonal, p95 0,8697% e máximo 6,0361%. Foram sinalizados 833 vértices acima de 0,5% de distância, 4.559 acima de 60 graus de desacordo de normal e 4.796 acima de pelo menos um limite. O doador contém 2.663 ilhas UV pelo critério registrado. A amostra é por ID de vértice e **não representa uma medida de área de superfície**.

O processamento completo dos **1.743.862 vértices e 3.491.844 triângulos** terminou em **299,61 segundos**. Avaliou 233.366.826 pares ponto/triângulo na projeção inicial. Distância mediana: **0,1046%** da diagonal; p95: **0,8482%**; máximo: **6,0762%**. Foram sinalizados **136.060 vértices (7,80%)** acima de 0,5% da diagonal, **796.871 (45,70%)** acima de 60 graus e **837.543 (48,03%)** acima de pelo menos um limite. O p50 de desacordo de normal foi 53,07 graus. Esses percentuais contam vértices, incluindo faces internas/ocultas, e não provam o percentual de área visível com defeito.

**590.760 faces (16,918%)** atravessavam ilhas UV diferentes; o tratamento de costuras duplicou 1.772.280 registros de vértice, produzindo 3.516.142 registros por candidato. **762.887 cantos** foram projetados em uma aresta/vértice do triângulo doador, evidenciando compressão local de UV. O master não foi reduzido ou sobrescrito.

| Candidato privado | Bytes | SHA-256 |
| --- | ---: | --- |
| `full-r1/candidate-pbr.glb` | 179.052.292 | `dce8313486fb8d0f6301e049b78f370ce6f9daea4cb069011baa1715046acfce` |
| `full-r1/candidate-no-normal.glb` | 179.052.264 | `eba18de32874b938d7a4143eba05944f51b9468592de708be9e4695d7b59a39f` |

A leitura independente dos arquivos gravados comparou **10.475.532 cantos por candidato** com os accessors originais do master: zero diferenças de posição e zero diferenças de normal. A quantidade/ordem de faces foi conservada. Os três payloads de imagem são byte-exatos ao doador; os descritores de material do PBR também são exatos. A variante sem normal map omite somente a referência `normalTexture` e conserva os payloads. Os dois arquivos originais conservam os hashes anteriores.

Evidência privada: `diagnostics/asset-lod-20261008/adria-transfer/full-r1/report.json` e `full-r1/mapping.npz`; as verificações independentes ficam em `diagnostics/asset-lod-20261008/adria-transfer/preservation-validation.json` e `diagnostics/asset-lod-20261008/adria-transfer/risk-regions.json`. Este último localiza os vinte piores pares de distância e confere a orientação normal/face em ambos os inputs. O doador tem zero faces com normal média invertida; o master tem 156 entre 3.491.844. Assim, uma inversão global simples das normais não explica os 796.871 vértices em desacordo.

A revisão Godot comparou os dois candidatos completos com o GLB final da API, com câmera/luz sincronizadas e escala comum. Foram geradas **sete capturas por candidato**: quatro órbitas, aproximação geral e duas aproximações das prateleiras/frascos. Evidência privada em `diagnostics/asset-lod-20261008/godot-review/adria-transfer-{pbr,no-normal}*.png` e seus manifestos de hashes. Os modelos mantêm a aparência geral reconhecível do doador; as curvas dos frascos e a geometria fina voltam a aparecer no master. Há regiões lisas/cinzas em quinas/prateleiras e diferenças de atribuição na superfície. Parte das áreas cinzas também existe no doador; não é correto atribuir toda área sem detalhe à transferência.

A comparação das variantes mostra alteração forte dos reflexos/contornos em alguns frascos ao reaplicar o normal map do remesh. Nas aproximações continuam visíveis **remendos/compressões de UV e variações de cor nas prateleiras/frascos**. A cópia do mapa é tecnicamente exata, mas **não comprova o bake no espaço tangente da malha densa**. A variante sem normal map muda os highlights e permite inspecionar o volume recuperado; ela não elimina os problemas de correspondência.

O resultado é uma recuperação **parcial para autoria/revisão**, com geometria densa preservada e material reconhecível, e evidência concreta do que ainda falta: correspondência de superfícies, continuidade de costuras e bake de normal map. **A preservação geométrica passou; a aparência não atingiu qualidade comparável comprovada para instalação.** A revisão visual permanece separada da preservação técnica; não há aceitação artística e não houve instalação.

## O que continua faltando

- Correspondência entre superfícies finas, lados opostos e regiões em que o remesh perdeu volume. A menor distância não garante que se escolheu a mesma tábua, frasco, parede ou lado.
- Costuras posicionadas com continuidade visual. A correção atual impede travessia de ilhas no interior de uma face, mas não recorta faces por fronteiras das ilhas nem faz rebake do atlas.
- Bake de normal map no espaço tangente do master. Copiar bytes PBR preserva os arquivos, porém o novo UV/geometria pode mudar a base tangente e somar detalhes já presentes geometricamente.
- Revisão controlada das quatro faces, teto, base, abertura e detalhes próximos no Godot. Prévia não comprova fidelidade na partida, orçamento de GPU ou aprovação artística.
- Transporte e seleção de LOD no runtime. Este programa não ultrapassa os limites do formato D3DMESH1 nem liga automaticamente o master ao jogo. O contrato v2 e o backend são frentes próprias, conforme [GPU-RENDERER.md](GPU-RENDERER.md).

Se a projeção local não atingir qualidade comparável, a próxima dependência é corrigir correspondências/costuras e rebake de UV/material em cópias de autoria. Isso reutiliza os masters e mapas preservados; não autoriza redução destrutiva, nova geração paga ou substituição da cabana aprovada.
