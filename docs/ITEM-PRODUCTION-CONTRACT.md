# Contrato proposto de produção de itens

Atualização: **8 de outubro de 2026**. **Estado: proposta de produção, sem loader ou integração na partida.** Este documento complementa o [catálogo de itens](ITEM-ASSET-CATALOG.md), o [contrato de personagens](CHARACTERS-RIGS-ANIMATION.md), a [sincronização de animações](MODEL-ANIMATION-CONTRACT.md) e os contratos de [master/material](TOWN-MODEL-V2.md) e [LOD](TOWN-ASSET-LOD-SELECTOR.md). A etapa e a fila continuam em [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md).

## Identidade e dados mínimos

Cada entrega mantém um `productionId` comum à referência, conceito, master, apresentações e ícone. Os nomes de campos abaixo são descritivos, não uma API implementada.

| Grupo | Dados mínimos |
| --- | --- |
| Identidade | `productionId`, vínculo ao ID reservado no registry quando existir, família/variante, revisão e proveniência. |
| Fonte nativa | Perfil/tabela, índice e mapping ID da base, identidade única quando aplicável, cursor e variante dinâmica. Nome ou cursor compartilhado não bastam para identificar o item. |
| Arte/modelo | Arquivo, SHA-256, ferramenta/versão, referência, vistas, partes inferidas e dependências. Master original imutável; derivados conservam a linhagem. |
| Materiais | Revisão/hash, texturas e espaços de cor, fatores, alfa, samplers e recursos exigidos; perdas de conversão explícitas. |
| Usos | Âncora e transformação por apresentação; para equipado, rig/variante/revisão e vínculo ao socket fornecido pela frente de personagens. |
| Revisão | Estados e evidências separados para referência, conceito, master/material, chão, equipado e ícone. Geração não concede aceitação nem seleção. |

[assets/registry.json](../assets/registry.json) continua autoridade de reserva e aceitação, sob as [regras de aprovação](ASSET-APPROVAL.md). Manifestos de produção complementam o estudo; não constituem outro registro de reservas ou loader. Guardar os registros de arte separados dos arquivos regenerados pelo levantamento. Referências extraídas licenciadas permanecem privadas; previews públicos usam arte autoral com proveniência registrada.

## Convenção do piloto

### Entrada de referência no Studio — aprendizado de 8 de outubro

Malha Smart foi escolhida como método para itens que precisarem de nova produção. A escolha não concede autorização de geração, lote, upload, reserva ou gasto. O contexto/cotação e as exclusões manuais ficam na [lista de revisão](items-study/SMART-PRODUCTION-REVIEW.md); modelos bons existentes devem ser reaproveitados.

**Pranchas são material de revisão, não input único de produção.** Por padrão, cada imagem enviada deve isolar um objeto completo ou uma vista coerente dele, sem título, labels, moldura ou miniatura acrescentados pela prancha. Preservar inscrições nativas legítimas. Conferir e aprovar arquivo/hash e vista exatos antes de enviar; registrar o hash efetivamente recebido pelo provedor quando verificável. O novo estudo delimitado abaixo considera uma exceção de composição de objetos distintos; ele não aprova execução nem torna válida uma prancha de vistas repetidas.

O operador Studio único confirmou o segundo ícone/cubo, **Imagens multivisão para 3D**, com slots **Frente, Esquerda, Direita e Verso** do mesmo objeto. As imagens ficam separadas e coerentes; uma vista 3/4 não é um perfil Esquerda/Direita. O terceiro ícone/pilhas, **Imagens em lote para 3D**, aceita até dez imagens de objetos separados. É outro fluxo; sua disponibilidade não autoriza lote nem transforma uma prancha de várias representações em um objeto único.

O teste manual Great Sword `6a744913-ab20-42f4-9e99-0941217d9792` demonstrou o defeito: um input com cinco representações resultou em um modelo com cinco silhuetas de espada. O operador baixou `input.png` pelo botão de referência e conferiu **1.644.705 bytes**, SHA-256 `1390409d3e2f25e51a9f75e1a70cda7ad8f4ce5f1bdfd8443152516a553f8999`, idêntico à r3 local. Isso fecha a correlação de input relatada pelo operador, sem comprovar cinco jobs, componentes de mesh, master aceito, UVs ou materiais. A r3 conserva o defeito da guarda BACK; não usá-la para declarar vistas coerentes nem promovê-la automaticamente. Great Sword e seus reusos ficam excluídos de novas gerações até QA/reuso, preservando o candidato manual e a seleção anterior.

### Exceção em estudo — dois ou três itens distintos

O novo pedido humano, eventos `Sentinel_16926d8494ec819193ea3215f4053174` e `Sentinel_8feaae0b7c248191bf936c4a00266f04`, pede estudar **2–3 itens distintos em uma imagem, uma geração e separação local posterior**. O [estudo de composição](items-study/GROUPED-ITEMS-STUDY.md) registra Scroll + Healing, Mana opcional e Short Sword excluída deste experimento. É uma proposta documental, sem composição final, referências aprovadas, upload, geração, reserva ou teto.

Cada item apareceria uma vez, completo, sem contato, sobreposição ou oclusão por outro; câmera/luz e escala relativa precisam de revisão. IDs ficam em metadados externos com posição/região, nunca como labels acrescentados aos pixels. Esse experimento difere de multivisão do mesmo objeto e do controle de lote de gerações independentes. Uma contagem-alvo de polígonos da saída conjunta não se aplica integralmente a cada item.

Uma separação bem-sucedida exige mapear partes por identidade, revisar superfícies ocultas, UVs, materiais, atlas, pivôs e escala, exportar derivados rastreáveis e reabri-los sem perdas. Componentes conectados não equivalem a itens: vidro, líquido, rolha e cordão podem ser partes do mesmo frasco. Atlas compartilhado pode ser conservado com UVs intactas; repack/rebake constitui nova revisão e exige QA. Preservar a saída conjunta, originais e backups individuais. Fusão, faces necessárias ausentes, identidade/material trocado ou custo/esforço que anule o ganho interrompem a promoção; não autorizam recuperação paga automática.

O requisito posterior `Sentinel_cab58c0598d4819195d2878edfe1e535` acrescenta estudo de mínimo de vértices por categoria e maior geometria **total** no conjunto. O [vínculo de geometria](items-study/GEOMETRY-BUDGET-LINKAGE.md) prepara famílias/IDs/reusos e tamanhos conhecidos. O guia do pesquisador cloud chegou por relay com faixas experimentais em triângulos e LODs candidatos: não são mínimos medidos de vértices, alvos escolhidos ou aceitação de qualidade. Categoria aplicável, alvo total e teto ainda exigem confirmação. Não escolher contagens, dividir o total igualmente ou transformar 5.000/10.000 observados no formulário vazio em garantia por item. Mudança de alvo exige nova conferência de preço/configuração.

Registrar quads/polígonos da fonte, triângulos da triangulação efetiva, registros de atributos por primitiva, índices referenciados e posições geométricas distintas com critério explícito. São medidas diferentes; não converter vértices em triângulos por proporção fixa nem soldar seams para obter uma contagem. Antes/depois por identidade precisa de hash, seleção de partes/instâncias e contagens comparáveis, preservando o conjunto e o master de maior qualidade. LODs são derivados separados: qualidade próxima e erro projetado continuam gates; limites do parser não são piso artístico nem autorização de redução.

O direcionamento `Sentinel_84292341680c8191a8b69178e78b0d4e` explicita a sequência: original real identificado → imagem fiel/nítida via imagegen, luz neutra e sem sombras marcadas → Malha Smart → download e separação do GLB → QA → integração pelo principal. Preservar a procedência e os bytes originais. Preparar imagem individual ou composição deliberada de poucos itens distintos, sempre com IDs/hashes; não reenviar uma prancha multivista como grupo. Essa escolha de método não promove as pranchas atuais nem cria autorização de imagem paga, upload, geração ou gasto nesta preparação.

Adotar a convenção proposta de personagens: GLB glTF 2.0 autocontido, metros, Y para cima, frente +Z, sistema destro. Registrar dimensões e orientação efetivas. Preservar os transforms originais, UVs, normais e materiais; não corrigir proporções com escala independente XYZ. A conversão de unidades e qualquer calibração uniforme precisam ter responsável e lugar explícitos, sem aplicação duplicada.

Armas, escudos e peças rígidas permanecem separados do corpo. Vestimentas com skin declaram rig e bind pose compatíveis. Ajustes por classe são derivados identificados do desenho, sem criar outro herói ou rig. Masters e materiais preservados não equivalem a recursos suportados pelo renderer: o transporte estático v2 não aceita skin, e sua preservação de PBR não comprova renderização na partida.

Âncoras de grip, apoio e apresentação são metadados locais versionados. Seus nomes definitivos dependem do alinhamento com personagens; este documento não fixa nomes canônicos de sockets.

## Transformações por uso

Usar vetores-coluna homogêneos; o produto mais à direita é aplicado primeiro. Todas as matrizes são finitas e declaram os espaços de origem e destino. No espaço de autoria avaliado do item e do rig, comprimentos são expressos em metros. Preservar as transformações internas da fonte: uma escala de node existente não autoriza normalizar o arquivo novamente.

Para uma peça rígida equipada:

```text
M_item(t) = W_actor(t) · S(t) · C · U(s) · G⁻¹
p_world   = M_item(t) · p_item
```

- `p_item`: vértice no espaço local de autoria do master, em metros, após as transformações de fonte declaradas.
- `G`: frame local do grip → espaço local do item; transformação rígida invertível, com translação em metros.
- `U(s)`: escala uniforme positiva, adimensional, de calibração do tamanho do item em torno do grip; padrão `s = 1`. Não converte unidades nativas do mundo.
- `C`: correção rígida do frame do grip calibrado → frame do socket, com translação em metros; revisão específica da combinação item/rig quando necessária.
- `S(t)`: frame do socket → espaço de autoria avaliado do rig, ambos em metros. A frente de personagens fornece a matriz avaliada a partir da pose e do vínculo ao osso; nomes de ossos ou sockets não são presumidos aqui.
- `W_actor(t)`: espaço de autoria avaliado do rig → mundo nativo, incluindo posição/direção nativas e a escala uniforme comum de unidades de mundo por metro.

A conversão metro → mundo ocorre somente em `W_actor`. A calibração `s` ocorre somente em `U(s)`; se já foi incorporada a um derivado identificado, usar `s = 1`. `G`, `C` e `S` não acrescentam outra calibração de tamanho. Não repetir o fit do ator no item nem usar espelhamento ou deformação XYZ para encaixá-lo. O segundo grip de armas de duas mãos é uma restrição de revisão da pose, sem alterar alcance ou tempo nativos.

Para chão ou apresentação de ícone:

```text
M_use = F_use · A_use⁻¹
```

`A_use` leva o frame da âncora ao espaço local do item, em metros; `F_use` leva esse frame ao espaço da apresentação. Registrar orientação, posição, escala uniforme e unidades de destino de `F_use`. Qualquer calibração e conversão necessárias ficam nesse único transform de uso, sem reaplicar o encaixe equipado. A receita transforma a instância e preserva o master. Câmera/projeção do ícone são etapas posteriores e não alteram a geometria.

Peças com skin seguem as matrizes do rig compatível; não recebem novamente o encaixe rígido. Um bake estático para chão/ícone registra master, materiais, rig e pose de origem. Não considerar um GLB com skin automaticamente exportável pelo contrato estático.

## Gates de revisão

- **Master/material:** conceito correspondente, objeto completo em 360°, dimensões, superfícies inferidas, silhueta, UVs, normais, materiais e alfa revisados. LODs permanecem derivados identificados; erro geométrico, silhueta e materiais exigem evidência antes de seleção automática.
- **Chão:** apoio e orientação coerentes, queda/repouso vinculados ao estado e frame nativos, seleção e oclusão verificadas. Geometria visual não modifica posição, colisão, RNG ou interação.
- **Equipado:** identidade exata dos slots, grip/socket, escala, clipping e deformação revisados por rig/classe nas ações pertinentes, na câmera original e em 360°. Ler ambos os slots reais, incluindo Bard e duas mãos; categoria de animação não identifica a arma. Preservar comportamento nativo para equipamento utilizável ou bloqueado por requisitos.
- **Ícone:** render do mesmo master e materiais aprovados, com receita de câmera/luz/pose registrada e hashes de origem. Verificar transparência e legibilidade no tamanho efetivo. Preservar footprint e grade lógica de **28 px por célula**, mesmo com imagem HD. Hover, requisitos, qualidade, identificação, quantidade e cargas continuam estados separados. Inventário, cinto e cursor usam o mesmo vínculo de identidade; a prancha conceito não é o ícone final.

## Decisões pendentes e responsáveis

- **Personagens:** nomes e transforms concretos dos sockets por rig/revisão; convenção dos frames de grip; unidade do espaço avaliado; calibração de escala; compatibilidade de skin/bind e ajustes por classe; revisão de clipping, segundo grip e visibilidade em primeira pessoa. O DTO candidato de ator ainda não oferece uma lista exata de itens equipados ou um contrato implementado de sockets.
- **HUD/inventário:** footprint e dimensões lógicas por cursor real; padding/alfa, enquadramento e redução para leitura em 28 px; vínculo de revisão para inventário, cinto e cursor; preservação de contornos, requisitos, estados e handlers no [contrato existente](HUD-IMPLEMENTATION.md). A resolução HD não deve alterar silenciosamente a ocupação calculada a partir do tamanho nativo do sprite.
- **Integrador:** leitura somente visual dos itens e slots efetivos; resolução por perfil/base/único/variante; conversão comum metro → mundo; capacidades de materiais/skin, cache e fallback; invalidação por revisão/estado e eventual integração. Este documento não implementa nem autoriza uma alteração de runtime, build, perfil ou seleção de assets.

**Monk:** [`GetPlrAnimArmorId`](../Source/items.cpp) modifica atributos durante o cálculo nativo. O futuro adaptador visual não deve chamá-la para consultar aparência nem reproduzir seus efeitos. Deve ler os resultados efetivos e os itens equipados após o processamento nativo. A identidade visual exata é independente da categoria de animação nativa, cujas regras permanecem preservadas.
