# Estudo — dois ou três itens distintos numa geração

Atualização: **8 de outubro de 2026**. Estudo `item.study.grouped-compact-01`. **Somente planejamento; separabilidade não demonstrada. Imagem criada, upload, geração, reserva e consumo: zero.**

O novo pedido humano considera **2–3 itens distintos → uma imagem → uma geração → separação local**. É uma exceção delimitada para estudar economia; não é multivisão do mesmo objeto, controle de lote ou autorização de execução. Substitui o desenho do experimento individual anterior, preservado como comparação. Não cria outra fila.

## Candidatos condicionais

Proposta: **Scroll + Healing**, com **Mana opcional**. Short Sword e lâminas finas ficam fora deste experimento. Nenhuma escolha ou referência está aprovada; todas as fontes abaixo ainda são pranchas RGB sem alpha.

| Papel | ID / fonte | SHA-256 | Inventário nativo |
| --- | --- | --- | --- |
| Proposto | `item.design.base.scroll`<br>[r1](../../assets/concept-art/items/r1/scroll-r1.png) | `efb23d5d5582b550de65231a3de659474923d63bc47f40e595b6eb60457ab28d` | 28×28 px, 1×1 célula; ICURS 1 |
| Proposto | `item.design.base.potion-of-healing`<br>[r1](../../assets/concept-art/items/r1/potion-of-healing-r1.png) | `fbfeaad3e56dc88ff64bf54d6238ed1390ad70362e1d121a7703b880b227f63e` | 28×28 px, 1×1 célula; ICURS 32 |
| Opcional | `item.design.base.potion-of-mana`<br>[r1](../../assets/concept-art/items/r1/potion-of-mana-r1.png) | `92a7527bfd54ef0844e52eeb56d4af8ceded077ef1af8d4e1ef16c22969cc177` | 28×28 px, 1×1 célula; ICURS 39 |

As duas propostas iniciais cobrem 26 registros base; com Mana, 28. Esses vínculos não criam 26/28 malhas. Os três candidatos têm procedência `image_gen.imagegen`, fontes e hashes preservados, sem master/ícone final instalado. A existência de modelos manuais/remotos próprios segue desconhecida. Great Sword e Shirotachi continuam excluídos de novos jobs.

## Composição proposta, ainda sem pixels produzidos

Uma única representação completa de cada item, com espaços vazios generosos, sem sobreposição, contato, corte, texto, moldura, miniatura, ângulos repetidos ou chão/cenário incorporado. Fundo limpo e câmera/luz coerentes. Preservar inscrições legítimas. Healing mantém líquido vermelho; Mana, azul-cobalto.

O manifesto reserva regiões normalizadas para duas ou três posições. IDs e a associação posição→item ficam em metadados externos. As regiões são um esquema de composição, não escala física medida. Aprovar dimensões relativas antes de preparar a imagem; 28 px de inventário não definem centímetros. Não deformar os objetos para preencher caixas.

Preservar pranchas originais e futuros inputs isolados individuais como backups. A imagem composta final, seu hash, escala e aprovação estão pendentes. Não usar uma prancha existente inteira nem executar geração de imagem paga nesta preparação.

## Comparação de créditos

Hipótese de geometria: **100 por geração**, sem cotação final da imagem conjunta. Separação local usa **zero créditos de serviço**, mas tem custo de tempo, ferramentas, computação e QA. Os valores adicionais relatados na UI estão abaixo, sem cotação confirmada para o input final. Textura20 não foi somada automaticamente.

| Objetos distintos | Trial único reconfirmado em cada caminho alternativo | Conjunto + separação local | Individual, uma geração por identidade | Economia bruta potencial |
| ---: | --- | ---: | ---: | ---: |
| 2 | Não | 100 | 200 | 100 |
| 2 | Sim, condicional | 0 | 100 | 100 |
| 3 | Não | 100 | 300 | 200 |
| 3 | Sim, condicional | 0 | 200 | 200 |

A coluna trial compara a **mesma oportunidade única** nos caminhos alternativos: não se aplica um trial por objeto nem economia promocional recorrente. Sua elegibilidade para o input conjunto e para os individuais é desconhecida; se diferir, refazer a comparação. Nenhum preço pós-input, economia líquida ou teto está confirmado.

**Comparação com reuso:** Healing e Mana compartilham a proposta de garrafa. Se um master/material compatível for aceito, a terceira identidade pode usar derivado de material sem uma terceira geração de geometria. Nesse caso, comparar o conjunto de três contra duas gerações mais reuso, e não presumir 300 de baseline. O reuso ainda não está aprovado.

Sem extras, a economia bruta hipotética é 100 contra duas gerações ou 200 contra três gerações independentes. Para três identidades com reuso Healing/Mana aceito e somente duas gerações de geometria, a margem volta a 100, descontando também qualquer trabalho/extra específico de material. A margem aplicável é sempre custo do baseline escolhido menos custo conjunto. Uma geração conjunta de 100 seguida da recuperação individual de um item por 100 elimina a economia contra dois individuais de 200; duas recuperações eliminam a economia contra três individuais de 300. São exemplos aritméticos: nenhuma recuperação/repetição é autorizada. Recalcular também tempo local e extras pagos antes de concluir vantagem.

## Extras do serviço — alternativa à separação local

O operador único relatou no modelo existente: **Dividir/Equilíbrio, 6–15 partes, 40 riscado/0 com Teste nova função ×1**; **SmartUV, 20 riscado/0 com Teste ×2**. São ações do serviço, diferentes da separação local de zero créditos. Elegibilidade final e combinação desses trials não foram confirmadas. Preencher Partes exige modelo segmentado e não revelou preço nessa etapa. Textura20 permanece adicional e não executada.

Como comparação aritmética sem trials, se os extras forem necessários somente no caminho conjunto: geometria 100 + Dividir 40 = **140**; acrescentar SmartUV 20 = **160**. Contra baseline 200 (dois objetos ou três identidades com reuso), sobram 60/40; contra três gerações independentes 300, sobram 160/140, antes do esforço local. Comparar somente extras adicionais relativos ao caminho individual/reuso.

O remesh básico Quad 10.000 mostrou **10**, com Smart P1.0 desligado. Ao consultar Smart, a UI informou incompatibilidade de Low Poly Smart com Malha Smart e desativou a opção. Portanto, 10 **não é cotação de retopologia Smart** nem alternativa automaticamente compatível com o método escolhido. Somá-lo aos 160 daria 170 apenas na aritmética, com margens 30/130; não há execução proposta ou aprovada. Nenhum desses exemplos soma Textura20, Preencher Partes ou trabalho local sem avaliação.

## Separação e QA exigidos

- **Identidade e partes:** Mapear cenas/nodes, instâncias, meshes, primitivas e componentes para cada ID. Vidro, líquido, rolha e cordão podem ser várias partes de um único frasco; componente conectado não é sinônimo de item.
- **Geometria separável:** Sem pontes, faces compartilhadas, interseções entre itens, contato fundido ou fundo incorporado. Extrair grupos por identidade sem cortar ou reconstruir superfícies para declarar sucesso.
- **Faces ocultas:** Revisar isoladamente em 360°: verso, base, espessura, gargalo, aberturas, espirais e amarrações. Não assumir faces geradas nem fechar aberturas legítimas automaticamente.
- **UV e materiais:** Preservar UVs, normais, texturas, fatores, alpha e samplers. Atlas/material compartilhado pode ser mantido; mistura de cor ou sombra entre itens exige revisão. Repack/rebake cria revisão derivada e novo QA.
- **Pivô e escala:** Registrar transforms e offsets de origem; remover apenas o posicionamento da composição. Preservar proporções/unidades e calibrar uniformemente uma vez, sem deformação XYZ.
- **Exportação e reabertura:** Manter GLB conjunto imutável. Exportar filhos autocontidos com hashes e linhagem; reabrir e comparar geometria, transforms, UVs, normais, materiais e imagens. A ferramenta de separação ainda não foi validada.
- **Detalhe e apresentações:** A contagem-alvo da saída conjunta não é orçamento de polígonos por item. Conferir detalhe próximo, ícone, ground/equipado, materiais e fallback a partir do mesmo master/material aceito.

Interromper promoção dos itens afetados se houver identidade ambígua, fusão, partes/superfícies necessárias ausentes, contaminação de materiais, perda na exportação, detalhe insuficiente ou reconstrução/retexturização/custo que anule o ganho. Aproveitamento parcial precisa de registro por ID; separar arquivos não concede aprovação artística. Nenhuma recuperação paga automática.

## Linhagem e estado atual

Registrar ID do estudo e IDs individuais → fontes aprovadas/hash → imagem composta/hash e mapping externo → tarefa compartilhada → GLB conjunto/hash → partes/transforms selecionados → GLBs filhos/hash e atlas/material → QA e aprovação por apresentação. Não presumir redução de memória: arquivos autocontidos podem duplicar texturas.

O operador único tentou exportar o manual Great Sword existente em GLB e, como recuperação, FBX nativo, sem cobrança adicional exibida. Os downloads não foram confirmados após timeout, sem arquivo no prefixo local auditado pelo operador; não há GLB/hash nesta frente. Na rotação visual, ele relatou cinco volumes, guardas/proporções incoerentes, miniatura menor e um nó na hierarquia. Não programou repetição ou rota oculta. Sua referência r3 foi correlacionada por hash, mas cinco silhuetas não comprovam componentes separáveis, UVs/materiais, pivôs ou faces ocultas. O arquivo não foi atribuído como master dos candidatos deste estudo. Itens conferiu o hash do recibo final do operador e os bytes/hashes dos 12 arquivos de evidência, sem copiar a mídia privada, inspecionar estas novas capturas ou acessar a conta; isso não substitui QA do modelo.

**Pendências:** aprovação dos candidatos/vistas e escala; exclusão de modelos manuais/remotos; análise do GLB por cópia; composição final/hash; cotação após input, trial e extras; teto humano. Nenhuma dessas pendências é resolvida pela escolha global de Malha Smart.

[Manifesto do estudo](grouped-items-study.json) · [Lista completa de itens](SMART-PRODUCTION-REVIEW.md) · [Contrato de produção](../ITEM-PRODUCTION-CONTRACT.md)

[Geometria por categoria e medidas pendentes](GEOMETRY-BUDGET-LINKAGE.md): guia cloud recebido como hipótese de triângulos/LOD, sem mínimo medido de vértices, alvo total escolhido ou ampliação do piloto.
