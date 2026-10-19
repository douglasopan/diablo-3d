# Itens: conceitos, modelos e ícones coerentes

Atualização: **8 de outubro de 2026**. O usuário ampliou explicitamente o requisito: armas, armaduras, poções, moedas, pergaminhos e todos os demais itens precisam de referências ligadas ao objeto 3D, à aparência equipada e à imagem do inventário. O trabalho começa pelo inventário completo de dados e pelas artes de conceito, antes de produzir modelos em série. Este estudo é subordinado a [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md); não reabre a cabana nem substitui a frente do HUD.

O levantamento está em [JSON](items-study/catalog.json), [CSV de bases e únicos](items-study/catalog.csv), [lista de conceitos](items-study/concept-slots.csv) e [CSV de afixos](items-study/affixes.csv). Todas as linhas são rastreáveis aos TSV públicos e ao código. O levantamento estrutural não gera arte. O [manifesto de conceitos](items-study/concept-art.json) registra separadamente os candidatos, estudos incompletos e propostas explícitas de reuso, com seus `productionId`, prompts, referências por hash e imagens. A contagem atual vem desse manifesto e da galeria; vínculo a estudo ou arte compartilhada não equivale a referência individual pronta. Nenhum modelo ou ícone de runtime foi produzido ou instalado por esta etapa.

## Primeiras artes para revisão

Abra a [galeria de todos os itens](items-study/gallery.html) para procurar por nome, família, ID ou estado. O primeiro lote contém Short Sword, Dagger, Falchion, Scimitar, Broad Sword, Potion of Healing, Potion of Mana, Gold e Scroll. São nove referências individuais com várias vistas e uma proposta de thumbnail, geradas com a ferramenta embutida `image_gen.imagegen`; os PNGs foram preservados sem edição posterior. Os prompts integrais estão no manifesto. Esse handoff inicial deixou **219 unidades sem conceito**; os lotes seguintes são registrados separadamente e acrescentados ao overlay, preservando aquelas nove entradas e imagens.

Os thumbnails nas folhas são propostas visuais, não imagens prontas para instalar na grade. As faces ocultas e a espessura são inferidas; a modelagem ainda precisa reconciliar as vistas. Nenhuma geração é aprovação artística automática. Ícone final, objeto no chão e item equipado devem derivar do mesmo master aceito, conservando geometria, materiais e orientação.

As nove imagens estão em `assets/concept-art/items/r1/`. O responsável pelo site recebeu o pedido de incluí-las no devlog, com legendas de conceito em revisão. A frente própria de itens é o chat `01a11b58-80a9-7e63-973b-47d72752ccc7`; catálogo, conceitos e galeria ficam com ele após o handoff, enquanto integração e alterações da engine continuam com o principal.

## Cobertura e significado dos números

| Conteúdo | Cobertura da fonte atual |
| --- | --- |
| `itemdat.tsv` | 168 registros: 166 nomeados e dois espaços reservados vazios; 146 rótulos de nome distintos. |
| Únicos Diablo | 90 registros. |
| Únicos Hellfire | 110 registros; o arquivo substitui a tabela de únicos, não acrescenta 110 aos 90. |
| Identidades únicas reunidas | 111: 89 compartilhadas, Lightforge somente na tabela Diablo e 21 identidades somente na tabela Hellfire. `Bloodslayer`/`BloodSlayer` são a mesma identidade com grafia preservada por perfil. |
| Cursores de itens declarados | 170 enumerações `ICURS_*` numéricas, todas ligadas a base, único ou aparência dinâmica, incluindo os três fragmentos `TORN_NOTE_1/2/3`. O sentinela `ICURS_DEFAULT` não é uma imagem. |
| Aparências dinâmicas | Três quantidades de ouro, três troféus de orelha, três cores de livro e dez óleos nomeados. |
| Livros por magia | 23 registros elegíveis na tabela Diablo e 32 na Hellfire. São variantes de três capas, não 55 livros físicos diferentes. |
| Cargas de cajado | 26 magias na tabela Diablo e 36 na Hellfire; reaproveitam os cajados físicos. |
| Afixos Diablo | 83 prefixos e 95 sufixos. |
| Afixos Hellfire | 86 prefixos e 98 sufixos. Também são tabelas alternativas por perfil. |
| Unidades propostas de produção | 228 entradas de desenho/família/variante, todas inicialmente pendentes. **Não é uma cobrança de 228 modelos independentes.** Compartilhamento e variantes são explícitos. |

Os números incluem as tabelas públicas **desta revisão do projeto**, inclusive templates iniciais, itens de quest, registros não sorteáveis e Arena Potion. Não são uma afirmação de que cada linha era um drop comum do Diablo comercial ou está disponível em toda partida. O catálogo conserva `dropRate`, `miscId`, magia, classe, slot, requisitos e o resultado da função `IsItemAvailable` para o jogo completo. Modo shareware, opção de teste da Bard, single/multiplayer, progressão, quests e geração impõem filtros adicionais.

O [recibo de cobertura](items-study/coverage.json) confere **730 linhas** das sete tabelas de itens/únicos/afixos, todos os 170 cursores e os 279 vínculos de base/identidade única. Os TSV de magias também têm hash e seus derivados estão listados. Essa verificação estrutural não comprova fidelidade artística, rig, direitos de mídia nem carregamento no jogo.

## Como os vínculos funcionam

`baseItems` conserva o índice nativo e mapping ID, o nome bruto, aliases `IDI_*`, caminho/linha, cursor, regras de escopo e equipamento. Muitos registros não têm um `IDI_*` na primeira coluna do TSV; por isso o índice nativo faz parte da identidade de estudo. Reordenar a tabela exige uma migração explícita, nunca trocar o vínculo por nome parecido.

`uniqueItems` reúne a identidade semântica, mas conserva cada tabela/perfil, mapping ID, aliases `UITEM_*`, poderes, base de geração e regra de cursor. **O mesmo mapping ID pode significar outro item em outro perfil.** `runtimeAssetId` continua vazio: um identificador deste estudo não instala ou aprova um asset.

`nativeVisuals` liga os enum/cursores às bases, únicos, aparência dinâmica e animação nativa de queda/chão. O cursor interno recebe `+12` para o ID de inventário e `+11` para o índice combinado zero-based. O loader real resolve `objcurs`/`objcurs2`; dimensões não foram adivinhadas, pois dependem dos sprites instalados licenciados.

`productionUnits` define o `productionId` comum à referência, conceito, master 3D, ícone e usos equipado/chão. Uma família de pergaminhos mantém a geometria e registra a magia como variante; óleos compartilham frasco e usam rótulos/materiais; livros usam três capas; afixos e números sorteados não exigem nova malha. Os únicos têm uma unidade de identidade própria, mesmo quando o original herda o ícone da base. Isso permite decidir conscientemente quais terão desenho exclusivo e quais compartilharão geometria.

Este estudo **não cria um segundo sistema de reservas**. [assets/registry.json](../assets/registry.json) permanece a autoridade de reserva, autoria e aceitação. O pai existente é `tristram.item.ground-visuals`. A frente principal deve promover somente os IDs de produção delimitados, com reserva/owner/issue no registro central, antes de distribuir geração a colaboradores. `claimOwner`, `claimIssue`, aceitação e seleção continuam vazios/falsos neste levantamento. O contexto de inventário e equipamentos do jogo inteiro não fica restrito ao nome desse pai de cenário; a evolução dos IDs globais pertence ao responsável pelo registro.

## Inconsistências comprovadas e referências que faltam

O código em `GetPlrAnimWeaponId` seleciona apenas **Sword, Axe, Bow, Mace ou Staff**, com combinações de escudo. O item exato do inventário não participa desse seletor. `GetPlrAnimArmorId` distingue somente **Light, Medium ou Heavy** no peito. Capacete, anéis e amuleto não escolhem variantes individuais do sprite nessa função. A limitação estrutural é comprovada; não significa que já houve inspeção visual de cada arma.

Há compartilhamentos de ícone comprovados pelos dados: Hunter's Bow e Long Bow; Short Staff e Quarter Staff; Plate Mail e Field Plate. A regra futura é usar a mesma silhueta aceita em todas as apresentações. Qual forma deve ser preservada ou corrigida precisa de comparação visual licenciada do inventário e da animação do personagem, com estado real equipado e identificado. Não inventar uma correção usando apenas o nome do enum.

Os casos especiais estão em `issues`:

- **Lightforge:** o único Diablo ID 9 usa `LGTFORGE`, sem base correspondente no `itemdat.tsv` atual. `IDI_LGTFORGE` está preenchido com Bovine Plate/`BOVINE`; Hellfire substitui explicitamente o único ID 9 por Bovine Plate. Lightforge fica com referência não resolvida, sem vínculo silencioso à armadura bovina.
- **The Undead Crown:** a base usa `THE_UNDEAD_CROWN` 78, mas o único sobrescreve com `HELM_OF_SPIRITS` 77. A referência deve registrar o estado real do item, não somente a base inicial.
- **Único sem cursor explícito:** herda o cursor da base efetiva em runtime. `SPIKCLUB` e `GOTHSHIELD` correspondem a mais de uma base; todas são preservadas. A função `SpawnUnique` escolhe a primeira correspondência, enquanto outros caminhos começam de uma base já gerada.
- **Bard e duas mãos:** a futura ligação deve ler os dois slots reais e seus IDs. A categoria nativa não substitui a identificação de cada arma; alcance, dano e tempo do ataque continuam nativos.

Referências do inventário foram extraídas localmente, em leitura, da instalação licenciada: 168 sprites, ampliações e pranchas privadas, com 840 verificações de dimensões, transparência e hashes. Esses bytes não integram o catálogo público nem o devlog. Faltam 34 gráficos específicos de Hellfire no arquivo local disponível. Não há diagnóstico visual de cada lâmina equipada no herói; essa comparação ainda exige estado real identificado. As faces ocultas dos conceitos são reconstrução proposta.

## Produção em lotes sem duplicação

A [fila autoral reproduzível](items-study/production-queue.json) ordena as 228 unidades sem duplicação e consulta os conceitos reais no manifesto separado. É sequenciamento deste estudo; não concede reservas, autoria no registro central, aprovação ou prontidão para modelagem. O [contrato proposto de produção de itens](ITEM-PRODUCTION-CONTRACT.md) descreve identidade, materiais, grips, transformações e gates de chão, equipado e ícone, subordinados aos contratos existentes de personagens e LOD.

O snapshot de referências de 8 de outubro localiza 192 unidades diretamente e uma família por suas variantes (orelhas). Há **35 bloqueios**: 34 gráficos Hellfire sem `objcurs2` local e Lightforge sem fonte resolvida. Disponibilidade significa referência localizada, ainda sujeita à inspeção; não significa fidelidade aprovada. Shirotachi pode usar a referência existente de `GREAT_SWORD`; Lightforge continua separado de Bovine Plate.

O [segundo lote](items-study/batch-02-blades.json) reúne as sete lâminas-base restantes: Claymore, Blade, Sabre, Long Sword, Bastard Sword, Two-Handed Sword e Great Sword. Two-Handed Sword recebeu R2 por divergência no padrão do thumbnail. Great Sword permanece **estudo incompleto**: R1 conserva os componentes, mas BACK não representa corretamente a assimetria; as tentativas R2/R3 corrigiram apenas parte do alvo e introduziram perdas na guarda. Todos os arquivos e defeitos foram preservados; nenhuma revisão dessa unidade está pronta para produção do master.

Os lotes seguintes cobrem [seis escudos-base](items-study/batch-03-shields.json), [cinco identidades únicas das primeiras lâminas](items-study/batch-04-blade-uniques.json), [seis machados e The Butcher's Cleaver](items-study/batch-05-axes.json), [quatro poções de recuperação](items-study/batch-06-restoration.json) e três capas de livro com um frasco de óleo. Griswold's Edge e The Defender usam propostas explícitas de reuso das artes de Broad Sword e Sabre; não receberam imagens exclusivas. Kite Shield conserva R1 e a correção R2 da bossa indevida no verso da vista lateral, com R2 apresentada explicitamente para revisão, sem seleção de runtime. Os demais grupos preservam família/tipo e têm até sete unidades. Afixos, magias e variantes compartilháveis continuam separados da contagem de malhas.

A continuação por família está no manifesto cumulativo e na fila; a [entrega para o devlog](items-study/DEVLOG-HANDOFF.md) deriva contagens, cobertura, hashes e **todas as imagens geradas**, inclusive revisões defeituosas e corrigidas. O [índice JSON](items-study/devlog-handoff.json) conserva a associação de cada PNG aos manifestos e IDs, distingue apresentação principal de histórico e lista os bloqueios. É material entregue ao integrador; não comprova publicação no site.

Reaproveitamentos são propostas explícitas pelo cursor original, sem inventar forma por nome e sem aprovar compartilhamento de malha. Um estudo incompleto transmite seu gate aos reusos. The Undead Crown preserva o estudo da base78 e a proposta do override77, sujeitos à confirmação do estado real. Flail R2 corrige a inversão da corrente e a ferragem inferior; Short Staff R2 recupera as quatro faixas ausentes na lateral. As revisões anteriores permanecem no histórico. Arcos, armaduras e outros objetos com vistas ausentes, repetidas ou inconsistentes são marcados individualmente como estudos incompletos; quantidade de vínculos não significa prontidão para modelagem.

| Família | Unidades propostas | Critério de reuso |
| --- | ---: | --- |
| Lâminas | 32 | Bases físicas e identidades únicas; stats iniciais reaproveitam o desenho. |
| Machados | 16 | Cabeça, cabo, escala e pivô coerentes com o conceito. |
| Maças, martelos e flails | 17 | Flail exige articulação visual própria, sem alterar tempo nativo. |
| Arcos | 21 | Arco e corda; flecha de ataque é uma dependência visual do projétil, não item de inventário adicional. |
| Cajados | 14 | Magia/cargas não duplicam a malha. |
| Escudos | 12 | Attachment independente na mão correspondente. |
| Cabeça | 15 | Ajuste ao rig/classe, sem gerar um personagem para cada elmo. |
| Armaduras e vestimentas | 32 | Peças rígidas versus skin de roupa, ajuste por classe. |
| Joias | 21 | Ícones e malha revisáveis; aparência equipada depende da distância. |
| Poções/elixires | 13 | Frasco, conteúdo e material com identidade; inclui Arena Potion da fonte atual. |
| Óleos | 1 | Família de frasco com dez resultados nomeados e variantes de rótulo. |
| Ouro | 3 | Pequena/média/grande, mesmos materiais e conjunto de moedas. |
| Pergaminhos | 1 | Família física, magias ligadas como variantes. |
| Livros de magia | 3 | Vermelho/fogo, azul/raio, cinza/magia. |
| Runas | 5 | Formas/inscrições próprias Hellfire. |
| Troféus de orelha | 4 | Master da família e três variantes de classe; não quatro gerações cegas. |
| Quests e referência pendente | 18 | Objetos de quests separados; inclui Lightforge sem base resolvida. |

Sequência autorizada: referência com identidade → conceito candidato → comparação/aprovação do conceito → master 3D completo → revisão 360°/materiais → attachments/skin → ícone renderizado do mesmo master → integração e inspeção no jogo. O usuário pode revisar visualmente os candidatos; não selecionar o resultado mais novo automaticamente.

O lote inicial está registrado para as bases `item.design.base.short-sword`, `dagger`, `falchion`, `scimitar`, `broad-sword`, `potion-of-healing`, `potion-of-mana`, `gold` e `scroll`. Use o prefixo completo `item.design.base.` em cada uma. O JSON/CSV contém os demais nomes e vínculos; a lista não se limita às primeiras imagens.

**Preparação de inventário, novo pedido de 8 de outubro:** o [handoff local prep-r1](items-study/INVENTORY-PREP-HANDOFF.md) e seu [manifesto](items-study/inventory-prep-r1.json) identificam Short Sword, Scroll e Healing, seus 29 registros base e as dimensões nativas verificadas: espada 28×84 px/1×3 células; pergaminho e poção 28×28 px/1×1. A entrega contém metadados e receitas de isolamento, com zero ícones RGBA produzidos, zero modelos, zero instalação e aprovação específica ainda não evidenciada nas fontes auditadas. As pranchas opacas não são ícones finais. O lote 3D permanece suspenso; o preflight central de um único modelo existente não autoriza geração, reserva ou consumo nesta frente. O principal mantém core/build/instalação/Git; este pacote não altera conceitos selecionados, registry ou a fila geral.

**Malha Smart como padrão, steering posterior de 8 de outubro:** a [lista completa para revisão](items-study/SMART-PRODUCTION-REVIEW.md) e seu [manifesto](items-study/smart-production-review.json) registram a escolha humana de método para todos os itens que precisarem de nova produção. Não há lote ou gasto aprovado. A lista preserva as 228 identidades e a ordem da fila, explicita 58 propostas de reuso e 35 bloqueios de referência, e exclui o grupo Great Sword de novos jobs após o relato de um teste manual (`6a744913-ab20-42f4-9e99-0941217d9792`). Shirotachi fica bloqueado por dependência, sem afirmar modelo próprio. A prancha r3 foi identificada/hash conferido, mas não aprovada ou promovida; GLB/hash/UV/material/QA do manual continuam pendentes. O cenário de 134 grupos disponíveis restantes × 100 = 13.400 créditos é apenas hipótese aritmética baseada no preflight público antigo, não cotação autenticada ou teto. Scroll/Short Sword/Healing somariam 300 nessa hipótese e já estão incluídos no total. Itens não gera em paralelo ao operador Studio único; exclusões manuais/remotas desconhecidas bloqueiam submissão.

**Delta verificado pelo operador:** o download read-only da referência do teste manual confirmou `input.png` com 1.644.705 bytes e SHA `1390409d3e2f25e51a9f75e1a70cda7ad8f4ce5f1bdfd8443152516a553f8999`, igual à r3 local. O [contrato](ITEM-PRODUCTION-CONTRACT.md#entrada-de-referência-no-studio--aprendizado-de-8-de-outubro) registra a diferença entre slots Frente/Esquerda/Direita/Verso de multivisão e lote de objetos separados; 3/4 não substitui perfil lateral e a BACK defeituosa de r3 não passa o gate. A UI autenticada Pro/header 3.200 mostra um único P2 Teste×1 com 100 riscado/0 no formulário vazio. O pequeno lote pode ser estimado em 200 de geometria somente se esse único trial for reconfirmado para um dos três inputs aprovados; caso contrário, mantém a hipótese de 300. Não estender preço zero ao catálogo. Textura20 é uma ação adicional não executada, sem configuração/custo final cotados, e não foi somada ao orçamento. Referências e teto seguem pendentes; nenhum job novo foi autorizado.

**Novo experimento solicitado — dois ou três itens distintos:** o [estudo de agrupamento](items-study/GROUPED-ITEMS-STUDY.md) e seu [manifesto](items-study/grouped-items-study.json) substituem o desenho do experimento individual acima, preservado somente para comparação. A proposta considera Scroll + Healing, com Mana opcional, numa imagem/geração e separação local; Short Sword fica fora. As fontes originais permanecem intactas, sem composição criada, upload, geração, reserva ou gasto. A hipótese de 100 pelo conjunto contra 200/300 por duas/três gerações independentes ainda exige cotação; separação local usa zero créditos de serviço, com esforço e QA pendentes. Se Healing/Mana compartilharem geometria aceita, comparar três identidades contra duas gerações mais reuso. Nenhum trial recorrente, extra pago ou economia líquida foi confirmado. O GLB manual Great Sword ainda está com download pendente no recibo; cinco silhuetas não comprovam partes separáveis. A composição, escala, IDs por posição, exportação com linhagem e QA por item estão definidos no estudo, sem criar outra fila.

**Geometria por categoria, requisito posterior:** o [vínculo com o guia cloud](items-study/GEOMETRY-BUDGET-LINKAGE.md) e seu [manifesto](items-study/geometry-budget-linkage.json) preservam as 228 identidades/famílias/reusos existentes, com categoria experimental proposta somente para Scroll/Healing/Mana. O guia recebido fornece hipóteses em triângulos e LODs candidatos, sem mínimos medidos de vértices. A soma experimental para inspeção próxima é 1.500–5.000 triângulos para Scroll + Healing, ou 2.500–8.000 com Mana opcional; a margem 2× para master é uma proposta, sem alvo escolhido. O orçamento é total, sem divisão igual garantida. Pranchas/células não definem escala física ou tamanho projetado. Contagens reais antes/depois, pivôs, UVs/materiais, fonte/revisão do guia original, cotação do alvo final e QA continuam pendentes. O complemento do operador foi recebido e conferido por hash, com linhagem própria e recibo anterior intacto. A sequência posterior mantém original real identificado → imagem fiel/nítida com luz neutra via imagegen → Malha Smart → separação do GLB/QA; nenhum novo arquivo de imagem foi produzido neste delta.

Cada arte precisa de ID, revisão, arquivo/hash, ferramenta, prompt, fonte primária, vistas usadas e partes inferidas. Por padrão, uma prancha com muitos itens ajuda a avaliar linguagem visual, mas não substitui a referência isolada/multivista por objeto para gerar 3D. O estudo agrupado acima é uma exceção delimitada em planejamento, com uma representação por item distinto, sem aprovação de execução ou separabilidade demonstrada. Ícone com transparência, enquadramento e silhueta legível deve vir do master aceito. Estados identificado/não identificado, qualidade, hover/contorno, requisitos, quantidade e charges continuam separados da textura-base; preservar leitura e dimensões da grade nativa.

## Coordenação e próximos responsáveis

- **Frente de conceitos de itens / principal:** gerar e vincular imagens, registrar candidatos e as correções propostas; promover reservas no catálogo central. Nenhuma API paga ou modelo foi criado pelo levantamento.
- **Frente de personagens existente:** validar sockets de mão/escudo/cabeça, bind pose, skin de vestimenta, clipping e animações nativas. Não gerar outro herói para resolver a arma.
- **Frente de HUD/inventário:** consumir ícones aprovados sem alterar o contrato de slots, cursor, contornos, tooltip e itens reais. A arte da moldura do HUD é uma entrega separada.
- **Frente de renderer/LOD:** transportar materiais e meshes, preservar master próximo e usar derivados à distância, sem redução destrutiva. Não instalar todos os masters sem medir orçamento.
- **Comunidade:** reivindicar uma unidade reservada pelo owner/issue, entregar revisão e identidade, não duplicar uma tarefa em andamento.

Para reproduzir ou checar a lista sem abrir o jogo:

```text
python docs/items-study/build_catalog.py
python docs/items-study/build_catalog.py --check
```

O segundo comando não escreve arquivos: detecta mudanças das fontes ou divergência dos derivados. O gerador escreve somente os seus cinco arquivos derivados; não modifica Source, CMake, registro central, perfis, saves, modelos, sprites ou a partida aberta. As artes devem usar um manifesto separado ligado por `productionId`, para não serem perdidas numa regeneração dos dados.

Para reconciliar a fila após acrescentar candidatos no manifesto:

```text
python -B docs/items-study/build_production_queue.py
python -B docs/items-study/build_production_queue.py --check
python -B docs/items-study/validate_concept_art.py
```

O gerador da fila escreve somente `production-queue.json`; `--check` não escreve. Ele lê dados e hashes públicos, sem abrir referências privadas. Conceitos registrados não atualizam os campos `not-generated` do catálogo estrutural: a galeria combina catálogo e overlay deliberadamente.

Para atualizar o pacote de entrega do devlog depois da fila:

```text
python -B docs/items-study/build_devlog_handoff.py
python -B docs/items-study/build_devlog_handoff.py --check
```

Esse gerador escreve apenas `devlog-handoff.json` e `DEVLOG-HANDOFF.md`. A opção `--require-complete` exige zero unidades com referência disponível ainda pendentes e zero PNGs sem manifesto entregue. O índice preserva os gates; não aceita conceitos, reserva IDs ou publica conteúdo.
