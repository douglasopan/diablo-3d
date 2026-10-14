# Geometria — vínculo de categorias, identidades e medidas

Atualização: **8 de outubro de 2026**. Requisito `Sentinel_cab58c0598d4819195d2878edfe1e535`. **Vínculo preparado; nenhum alvo selecionado, contagem medida, download concluído, job ou gasto novo.**

Este derivado liga as 228 identidades e famílias da fila existente aos grupos de reuso propostos. Não cria prioridades, outra fila ou reservas. Apenas Scroll/Healing/Mana têm categoria experimental proposta nesta passagem; as outras identidades mantêm categoria pendente. Nenhum compartilhamento de geometria está aprovado.

O guia cloud chegou por resumo repassado pelo pai e pelo operador único. As faixas abaixo são **hipóteses de teste em triângulos exportados**, não mínimos medidos de vértices. O arquivo original/revisão/hash do guia não foi fornecido. Metadados completos e campos de medição estão no [manifesto](geometry-budget-linkage.json).

| Categoria do guia | Uso a 40–150 px: triângulos | Inspeção próxima a 250–700 px: triângulos |
| --- | ---: | ---: |
| Moeda | 32–160 | 150–800 |
| Gema | 24–160 | 150–800 |
| Pergaminho | 100–500 | 500–2000 |
| Poção | 300–900 | 1000–3000 |
| Arma | 400–1800 | 2000–8000 |
| Escudo | 300–1400 | 1000–5000 |
| Peça rígida | 500–2500 | Não fornecido |
| Armadura | Não fornecido | 2000–8000 |

Os intervalos em pixels também são propostas do guia, sem câmera/FOV/distância/viewport medidos nos candidatos. Mínimo de vértices continua **pendente**. Não derivar vértices por proporção fixa de triângulos.

## Candidatos e tamanhos disponíveis

| ID | Família / categoria proposta | Inventário / prancha | Reuso e medida 3D |
| --- | --- | --- | --- |
| `item.design.base.scroll` | `scrolls` / `scroll` | 28×28 px, 1 célula / 1536×1024 px | Conceito direto; reuso de malha não aprovado. Escala física/bounds/projeção/vértices pendentes. |
| `item.design.base.potion-of-healing` | `potions-elixirs` / `potion` | 28×28 px, 1 célula / 1536×1024 px | Conceito direto; reuso de malha não aprovado. Escala física/bounds/projeção/vértices pendentes. |
| `item.design.base.potion-of-mana` | `potions-elixirs` / `potion` | 28×28 px, 1 célula / 1536×1024 px | Conceito direto; reuso de malha não aprovado. Escala física/bounds/projeção/vértices pendentes. |

Scroll corresponde ao rolo amarrado, com espirais e fita; Healing/Mana, ao frasco de vidro, gargalo, rolha, cordão e líquidos vermelho/azul. Healing/Mana podem compartilhar master com material diferente se isso passar QA. As regiões da composição são reservas de enquadramento, sem definir escala física. Mana continua opcional; Short Sword está fora, e Great Sword/Shirotachi continuam excluídos de novos jobs.

## Geometria total — hipótese, sem alvo escolhido

| Composição proposta | Uso a 40–150 px | Inspeção a 250–700 px | Margem experimental 2× para master de inspeção |
| --- | ---: | ---: | ---: |
| Scroll + Healing | 400–1.400 triângulos | 1.500–5.000 triângulos | 3.000–10.000 triângulos |
| Mais Mana, opcional | 700–2.300 triângulos | 2.500–8.000 triângulos | 5.000–16.000 triângulos |

Essas somas não garantem distribuição, densidade, qualidade ou resultado do serviço. Reuso de uma garrafa pode alterar a contagem de recursos únicos. Não dividir igualmente o total. Aumentar o alvo total ainda exige escolher a métrica, configurar o input final e reconferir preço/teto; os controles 5.000/10.000 observados no formulário vazio não garantem faces por item. Os exemplos anteriores de 100/40/20 créditos continuam condicionais à configuração. O exemplo cloud de seis itens não amplia este piloto.

A aproximação em quads relatada pelo guia não é adotada como medição ou alvo: não dividir triângulos por dois sem topologia de autoria e triangulação verificadas. Quad na UI não comprova quad no GLB. Contagens do teste manual Great Sword são relatos da UI, sem transferir densidade aos candidatos.

## Medição e preservação

Registrar, por identidade e etapa: arquivo/hash, ferramenta/versão/método, seleção de nodes/instâncias/primitivas, quads/polígonos de autoria quando disponíveis, triângulos efetivos/degenerados, registros de vértices armazenados e referenciados, modo da primitiva e quantidade de entradas do índice. Registros referenciados distintos não são entradas do índice; índices ÷ 3 vale somente para primitivas indexadas TRIANGLES válidas. Posições coincidentes são diagnóstico com espaço/tolerância explícitos; não demonstram vértices soldados. Preservar splits de UV/normais/tangentes. Distinguir recursos únicos de instâncias/draws e de dados duplicados em filhos autocontidos.

Comparar seleção original → filho extraído → filho reaberto, com a mesma triangulação e domínio de contagem, transforms recompostos e atribuição exclusiva. Registrar faces sem identidade ou atribuídas repetidamente. Extração pura conserva os triângulos/superfícies; diferenças de registros de vértices precisam de explicação e QA. Mais geometria não comprova faces ocultas, silhueta ou material corretos.

Preservar conjunto e master imutáveis, UVs, normais, materiais/texturas/atlas, unidades, pivôs e linhagem. Componentes separados podem ser tampa/frasco/líquido do mesmo ID; fusão entre identidades não é resolvida automaticamente. Exportar filhos autocontidos e reabrir sem perdas. Arquivos e contagens reais ainda inexistem nesta entrega; complemento do operador tem estado próprio no manifesto.

LODs de 100/50/25% por item e avaliação de desvio em torno de 1 px são propostas do guia, sujeitas a silhueta, bordas, pontas, reflexos e materiais. Referência drawable aceita e master preservado são papéis distintos. O [seletor existente](../TOWN-ASSET-LOD-SELECTOR.md) exige erro conservador e tamanho projetados; não impõe orçamento artístico por categoria nem autoriza redução para satisfazer o parser. O erro aproximado/normalizado retornado pelo simplificador, possivelmente incluindo atributos, não preenche automaticamente o erro geométrico conservador do contrato nem torna `errorVerified=true`. Registrar métrica, unidade e referência; demonstrar o limite conservador e revisar geometria/material antes da seleção. Nenhum LOD foi produzido ou selecionado.

Fontes primárias para a semântica das ferramentas, sem atribuir-lhes as faixas numéricas do guia: [meshoptimizer — simplificação](https://meshoptimizer.org/#simplification), [Blender — glTF](https://docs.blender.org/manual/en/5.1/addons/import_export/scene_gltf2.html), [Blender — Separate](https://docs.blender.org/manual/en/5.1/modeling/meshes/editing/mesh/separate.html).

[Estudo agrupado](GROUPED-ITEMS-STUDY.md) · [Contrato de itens](../ITEM-PRODUCTION-CONTRACT.md)

**Complemento do operador conferido:** `tripo-geometry-budget-study.json`, SHA-256 `c859071199f655b66be98222bbe7ee67ba95620e65237a51d5d7f8875891151e`. Bytes/hash e linhagem com o recibo anterior foram verificados por Itens; nenhum recibo foi sobrescrito. O complemento distingue peça rígida de armadura; campos não fornecidos permanecem pendentes. Download e medições do modelo continuam sem confirmação.

**Método de referência posterior:** original real identificado → imagem fiel/nítida via imagegen, luz neutra e sem sombras marcadas → Smart Mesh → download/separação → QA → integração pelo principal. Preservar originais, usar imagem individual ou composição deliberada com IDs/hashes externos e não tratar prancha multivista como grupo. Nenhuma imagem nova foi preparada ou aprovada neste delta; alvo, cotação e teto continuam pendentes.
