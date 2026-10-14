# Malha Smart — lista completa para revisão e orçamento condicional

Atualização: **8 de outubro de 2026**. **Método escolhido: Malha Smart. Gerações/consumo autorizados por este pacote: zero.**

A direção humana foi repassada no evento `Sentinel_d13a92ff08748191968cf63ddc3307c2`: “vamos fazer todos os itens com essa malha smart com certeza”. Isso escolhe o método para itens que realmente precisarem de nova produção, preservando modelos manuais bons e reusos aprovados. Não aprova arte, lote, orçamento, GLBs ou instalação.

Este documento deriva a lista e a ordem da fila existente. Não cria outra fila de produção ou registro de reservas. O [manifesto completo](smart-production-review.json) mantém IDs, variantes, proveniência/hashes, referências, exclusões, vínculos master/material/LOD/ícone e QA por ativo. A autoridade de reserva/aceitação continua em `assets/registry.json`; o principal mantém core/build/instalação/Git.

São **228 identidades**, **193 ligadas a conceitos**, **35 bloqueadas por referência** e **58 propostas explícitas de reuso**. O grafo propõe 170 grupos, 135 com referência. Esses grupos ainda não são meshes ou jobs aprovados; uma família pode compartilhar mais, exigir separação ou manter material/variante próprio.

Após excluir o grupo manual Great Sword, restam **134 grupos com referência** e **169 no catálogo inteiro** para cenários futuros. Existem **227 identidades com exclusão manual/remota desconhecida**. Desconhecido bloqueia submissão. Entradas individuais prontas/aprovadas: zero; masters validados: zero; 22 identidades conservam gates de estudo incompleto.

## Proposta individual anterior — preservada como comparação

O [estudo atual de agrupamento](GROUPED-ITEMS-STUDY.md) considera Scroll + Healing, com Mana opcional, numa imagem/geração e separação local. Short Sword fica fora desse experimento. A proposta individual abaixo está preservada como histórico/comparação, sem execução em paralelo.

Os três candidatos individuais anteriores têm o piloto Scroll **incluído uma única vez** no total. Nenhuma referência individual foi aprovada. Seus valores não são o orçamento automático do novo estudo agrupado.

| Ordem proposta | Identidade | Fonte candidata / SHA-256 | Entrada individual |
| --- | --- | --- | --- |
| 1 | Scroll physical family (spell-specific markings) — `item.design.base.scroll` | [r1](../../assets/concept-art/items/r1/scroll-r1.png); `efb23d5d5582b550de65231a3de659474923d63bc47f40e595b6eb60457ab28d` | Pendente; prancha sem aprovação específica. |
| 2 | Short Sword — `item.design.base.short-sword` | [r1](../../assets/concept-art/items/r1/short-sword-r1.png); `d944a5855290a5fc604ce5ff3970c7c0e908eeda531a43515fd02dfc5c1ab059` | Pendente; prancha sem aprovação específica. |
| 3 | Potion of Healing — `item.design.base.potion-of-healing` | [r1](../../assets/concept-art/items/r1/potion-of-healing-r1.png); `fbfeaad3e56dc88ff64bf54d6238ed1390ad70362e1d121a7703b880b227f63e` | Pendente; prancha sem aprovação específica. |

As vistas propostas estão no manifesto. O [handoff de inventário](INVENTORY-PREP-HANDOFF.md) registra espada 28×84 px/1×3 células e Scroll/Healing 28×28 px/1×1, além das receitas de ícone. Elas não são inputs 3D preparados. Healing exige revisão de vidro/líquido; Short Sword exige revisão de lâmina fina, guarda e grip. Outros candidatos permanecem na ordem original da fila.

## Exclusão do teste manual Great Sword

O humano/operador relatou o [teste manual `6a744913-ab20-42f4-9e99-0941217d9792`](https://studio.tripo3d.ai/pt/workspace/generate/6a744913-ab20-42f4-9e99-0941217d9792): **uma imagem produziu um modelo com cinco silhuetas de espada**, sem comprovar cinco jobs ou cinco componentes de mesh. UI reportada: Quad, 5.402 faces e 5.165 vértices. O humano relata Malha Smart no teste; engine/custo históricos, GLB/hash, UVs, materiais e QA permanecem pendentes.

A pasta r3 indicada contém um único PNG: [Great Sword r3](../../assets/concept-art/items/r3/great-sword-r3.png), SHA-256 `1390409d3e2f25e51a9f75e1a70cda7ad8f4ce5f1bdfd8443152516a553f8999`. Bytes e imagem locais foram conferidos. O operador confirmou a correlação exata: input.png baixado pelo botão de referência tem 1.644.705 bytes e o mesmo SHA-256 da r3 local. Itens conferiu os bytes locais e registrou a evidência do operador, sem acessar a conta. A fonte é uma prancha gerada por `image_gen.imagegen`; a r3 permanece uma correção defeituosa arquivada, com ramo superior da guarda BACK ausente. A seleção anterior não foi promovida ou substituída.

**Great Sword e o reuso Shirotachi têm zero novos jobs propostos até QA/reuso.** Somente Great Sword tem um candidato manual reportado; a existência de um modelo próprio Shirotachi continua desconhecida. Preservar o candidato manual; não regenerar automaticamente nem confundi-lo com o UUID de 08-03. O resultado concreto reforça o gate padrão: uma imagem de entrada deve conter um único objeto completo, sem título, várias vistas e miniatura no mesmo canvas. O novo estudo agrupado é uma exceção delimitada para itens distintos, ainda sem separabilidade demonstrada ou execução autorizada.

O operador também reportou quatro `TripoDemoModel` e o ativo `ee4a954e-6107-4677-8452-8de8e83751da`, com data exibida 08-03. Identidade, procedência, origem manual e qualidade são desconhecidas. Não foram ligados a itens nem usados para declarar ausência de duplicatas. Itens não consultou a conta ou o ledger remoto em paralelo.

## Orçamento

O operador único reportou sessão autenticada e header de **3.200 créditos**. É saldo observado por ele, não teto de gasto. O menu autenticado mostra Pro segundo o operador. O formulário vazio P2.0/Quad/5.000/uma saída mostra 100 riscado e 0 com P2 Teste ×1: um trial, não preço universal zero. A elegibilidade para a referência final, custo após input e destino privado ainda precisam de reconferência. O formulário novo H3.1/55 não identifica engine ou custo do teste manual histórico.

Os cenários abaixo preservam a proposta individual anterior e a projeção do catálogo; não são o orçamento do novo experimento agrupado. A hipótese usa **100 créditos por uma saída**, apenas do preflight público deslogado P2.0/Quad/controle 5.000. Não é cotação autenticada nem preço universal. O cenário alternativo de 200 considera somente um trial de 0 para um dos três ativos, se reconfirmado após input aprovado. Os demais cenários mantêm 100 como hipótese. O botão Textura20 observado é uma ação adicional não executada: não foi somado, pois configuração/custo final seguem pendentes. Os cenários não incluem variantes, tentativas automáticas, extras de textura/pós-processamento, preparação paga ou compra adicional; nenhum total é autorizado.

| Cenário condicional | Saídas hipotéticas | Créditos pela hipótese de 100 |
| --- | ---: | ---: |
| Piloto único Scroll, se elegível | 1 | 100 |
| Três candidatos, incluindo o piloto uma vez | 3 | 300 |
| Os mesmos três candidatos, se um único trial de 0 for reconfirmado | 3 | 200 |
| Grupos com referência após excluir o manual; reusos ainda condicionais | 134 | 13.400 |
| Todos os grupos após excluir o manual; inclui fontes bloqueadas | 169 | 16.900 |
| Uma saída por identidade com referência restante, sem aprovar reuso | 191 | 19.100 |
| Uma saída por identidade restante, sem aprovar reuso | 226 | 22.600 |

O cenário de 13.400 já inclui os três candidatos iniciais: **300 + 13.100 = 13.400**, se todos os gates e reusos forem aprovados. Não somar novamente o piloto ou o pequeno lote ao total agrupado. Dagger foi uma alternativa técnica não selecionada na proposta individual anterior. Ela não integra o novo estudo agrupado.

Os reusos exigem atenção particular em Undead Crown/Helm of Spirits (base78/override77), Gnarled Root (Club/Spiked Club) e Ear (família de três classes). O inspector público contém 130 registros sem mapping explícito de item; isso não comprova ausência de um modelo manual ou remoto.

Total autenticado confirmado: **pendente**. Teto humano aprovado: **pendente**. Jobs elegíveis/autorizados agora: **zero**. As contagens agrupadas dependem de aprovação dos reusos e da exclusão manual/remota; não são limites financeiros inferior/superior. Não consumir os 3.200 créditos por inferência da escolha de método.

## Entrada correta no Studio

O operador confirmou **Imagens multivisão para 3D** no segundo ícone/cubo: campos Frente, Esquerda, Direita e Verso descrevem o mesmo objeto. Cada imagem deve isolar uma vista completa e coerente. Não colocar a prancha inteira num slot; uma vista 3/4 não substitui um perfil Esquerda/Direita. A r3 de Great Sword conserva defeito na guarda BACK e não está apta a formar um conjunto coerente.

O terceiro ícone/pilhas é **Imagens em lote para 3D**, até dez imagens de objetos separados. Ele não equivale a multivisão, não divide uma prancha com cinco desenhos em um objeto único e não concede autorização para lote. Por padrão, um único objeto por imagem também vale para a entrada de vista única. A exceção proposta no estudo agrupado usa uma representação de cada item distinto; não legitima várias vistas/miniaturas de uma mesma espada no mesmo canvas. Preservar inscrições legítimas do item, removendo apenas títulos/labels/molduras/miniaturas da prancha.

## Gates e QA por ativo

- **input:** Arquivo/hash e aprovação exatos; por padrão, um objeto completo por imagem, sem título, moldura, labels ou miniatura da prancha. A exceção documental grouped-compact-01 estuda 2–3 itens distintos sem autorizar execução. Preservar inscrições legítimas. Multivisão Frente/Esquerda/Direita/Verso é do mesmo objeto; 3/4 não é perfil lateral. O controle de lote é outro fluxo.
- **geometry:** Revisão em 360°: contagem de objetos/partes, silhueta, peças finas, espessuras, aberturas, normais, topologia e duplicatas desconectadas. Quad ou número de polígonos não comprova qualidade.
- **materials:** Preservar UVs, texturas, espaços de cor, fatores e alpha do master; distinguir cor do material de luz gravada. Conferir suporte real do renderer e preservar dados de origem não suportados.
- **identity:** Preservar IDs de produção/base/único/dinâmico, cursor efetivo, magia, quantidade e variantes de classe. Cursor ou imagem compartilhados não aprovam malha compartilhada.
- **attachment:** GLB/glTF2, metros/Y para cima/+Z e transforms declarados; calibração uniforme uma vez. Conferir grip, encaixe, rig, skin e tempo nativo. Roupas não podem trazer anatomia inventada fundida.
- **lod:** Preservar master imutável. Derivados com hash, âncora comum e revisão de silhueta, peças finas, UV/material e qualidade próxima. Erro geométrico precisa de evidência; sem seleção automática com errorVerified=false.
- **icon:** RGBA final do mesmo master/material aceito, sem títulos, labels ou texto acrescentado pela prancha, fundo ou sombra aderida; preservar inscrições nativas legítimas. Conferir células nativas de 28 px, legibilidade, contorno/hover e fundos claros/escuros da interface.
- **runtime:** Principal integra/instala e registra testes reais de inventário, loja, cinto, chão e equipado quando aplicáveis. Preservar nomes, stats, slots, animação, picking, simulação e fallback original. Dimensões de chão não são inferidas do ícone.

Cada linha do manifesto inclui os checks da família e entregáveis separados. Armas finas, vidro/material, flail/corrente, arco/corda, roupas/skin/anatomia, contagem de moedas e variantes de orelha exigem revisão própria. Uma etiqueta Quad ou um número de polígonos não libera qualidade. Fallback e todos os dados/tempos nativos permanecem obrigatórios.

## Lista completa na ordem da fila existente

A coluna Grupo representa reuso **proposto**, com nomes curtos abaixo apenas para leitura. M = grupo manual excluído até QA; ? = existência manual/remota desconhecida. Todos os inputs individuais e custos por ativo continuam pendentes. Fontes com link são pranchas candidatas; o hash completo está por linha no manifesto.

| Seq. | Nome / ID | Família | Referência / arte candidata | Grupo proposto | Manual |
| ---: | --- | --- | --- | --- | --- |
| 1 | Short Sword<br>`item.design.base.short-sword` | blades | [r1](../../assets/concept-art/items/r1/short-sword-r1.png) | `base.short-sword` | ? |
| 2 | Dagger<br>`item.design.base.dagger` | blades | [r1](../../assets/concept-art/items/r1/dagger-r1.png) | `base.dagger` | ? |
| 3 | Falchion<br>`item.design.base.falchion` | blades | [r1](../../assets/concept-art/items/r1/falchion-r1.png) | `base.falchion` | ? |
| 4 | Scimitar<br>`item.design.base.scimitar` | blades | [r1](../../assets/concept-art/items/r1/scimitar-r1.png) | `base.scimitar` | ? |
| 5 | Broad Sword<br>`item.design.base.broad-sword` | blades | [r1](../../assets/concept-art/items/r1/broad-sword-r1.png) | `base.broad-sword` | ? |
| 6 | Potion of Healing<br>`item.design.base.potion-of-healing` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-healing-r1.png) | `base.potion-of-healing` | ? |
| 7 | Potion of Mana<br>`item.design.base.potion-of-mana` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-mana-r1.png) | `base.potion-of-mana` | ? |
| 8 | Gold<br>`item.design.base.gold` | gold | [r1](../../assets/concept-art/items/r1/gold-r1.png) | `base.gold` | ? |
| 9 | Scroll physical family (spell-specific markings)<br>`item.design.base.scroll` | scrolls | [r1](../../assets/concept-art/items/r1/scroll-r1.png) | `base.scroll` | ? |
| 10 | Claymore<br>`item.design.base.claymore` | blades | [r1](../../assets/concept-art/items/r1/claymore-r1.png) | `base.claymore` | ? |
| 11 | Blade<br>`item.design.base.blade` | blades | [r1](../../assets/concept-art/items/r1/blade-r1.png) | `base.blade` | ? |
| 12 | Sabre<br>`item.design.base.sabre` | blades | [r1](../../assets/concept-art/items/r1/sabre-r1.png) | `base.sabre` | ? |
| 13 | Long Sword<br>`item.design.base.long-sword` | blades | [r1](../../assets/concept-art/items/r1/long-sword-r1.png) | `base.long-sword` | ? |
| 14 | Bastard Sword<br>`item.design.base.bastard-sword` | blades | [r1](../../assets/concept-art/items/r1/bastard-sword-r1.png) | `base.bastard-sword` | ? |
| 15 | Two-Handed Sword<br>`item.design.base.two-handed-sword` | blades | [r2](../../assets/concept-art/items/r2/two-handed-sword-r2.png) | `base.two-handed-sword` | ? |
| 16 | Great Sword<br>`item.design.base.great-sword` | blades | [r1](../../assets/concept-art/items/r1/great-sword-r1.png); estudo incompleto | `base.great-sword` | M — zero novos jobs |
| 17 | Buckler<br>`item.design.base.buckler` | shields | [r1](../../assets/concept-art/items/r1/buckler-r1.png) | `base.buckler` | ? |
| 18 | Small Shield<br>`item.design.base.small-shield` | shields | [r1](../../assets/concept-art/items/r1/small-shield-r1.png) | `base.small-shield` | ? |
| 19 | Large Shield<br>`item.design.base.large-shield` | shields | [r1](../../assets/concept-art/items/r1/large-shield-r1.png) | `base.large-shield` | ? |
| 20 | Kite Shield<br>`item.design.base.kite-shield` | shields | [r2](../../assets/concept-art/items/r2/kite-shield-r2.png) | `base.kite-shield` | ? |
| 21 | Tower Shield<br>`item.design.base.tower-shield` | shields | [r1](../../assets/concept-art/items/r1/tower-shield-r1.png) | `base.tower-shield` | ? |
| 22 | Gothic Shield<br>`item.design.base.gothic-shield` | shields | [r1](../../assets/concept-art/items/r1/gothic-shield-r1.png) | `base.gothic-shield` | ? |
| 23 | Griswold's Edge<br>`item.design.unique.griswold-s-edge` | blades | [r1](../../assets/concept-art/items/r1/broad-sword-r1.png) | `base.broad-sword` | ? |
| 24 | Gonnagal's Dirk<br>`item.design.unique.gonnagal-s-dirk` | blades | [r1](../../assets/concept-art/items/r1/gonnagal-s-dirk-r1.png) | `unique.gonnagal-s-dirk` | ? |
| 25 | Gryphon's Claw<br>`item.design.unique.gryphon-s-claw` | blades | [r1](../../assets/concept-art/items/r1/gryphon-s-claw-r1.png) | `unique.gryphon-s-claw` | ? |
| 26 | Black Razor<br>`item.design.unique.black-razor` | blades | [r1](../../assets/concept-art/items/r1/black-razor-r1.png) | `unique.black-razor` | ? |
| 27 | The Defender<br>`item.design.unique.the-defender` | blades | [r1](../../assets/concept-art/items/r1/sabre-r1.png) | `base.sabre` | ? |
| 28 | Small Axe<br>`item.design.base.small-axe` | axes | [r1](../../assets/concept-art/items/r1/small-axe-r1.png) | `base.small-axe` | ? |
| 29 | Axe<br>`item.design.base.axe` | axes | [r1](../../assets/concept-art/items/r1/axe-r1.png) | `base.axe` | ? |
| 30 | Large Axe<br>`item.design.base.large-axe` | axes | [r1](../../assets/concept-art/items/r1/large-axe-r1.png) | `base.large-axe` | ? |
| 31 | Broad Axe<br>`item.design.base.broad-axe` | axes | [r1](../../assets/concept-art/items/r1/broad-axe-r1.png) | `base.broad-axe` | ? |
| 32 | Battle Axe<br>`item.design.base.battle-axe` | axes | [r1](../../assets/concept-art/items/r1/battle-axe-r1.png) | `base.battle-axe` | ? |
| 33 | Great Axe<br>`item.design.base.great-axe` | axes | [r1](../../assets/concept-art/items/r1/great-axe-r1.png) | `base.great-axe` | ? |
| 34 | The Butcher's Cleaver<br>`item.design.unique.the-butcher-s-cleaver` | axes | [r1](../../assets/concept-art/items/r1/the-butcher-s-cleaver-r1.png) | `unique.the-butcher-s-cleaver` | ? |
| 35 | Potion of Full Healing<br>`item.design.base.potion-of-full-healing` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-full-healing-r1.png) | `base.potion-of-full-healing` | ? |
| 36 | Potion of Full Mana<br>`item.design.base.potion-of-full-mana` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-full-mana-r1.png) | `base.potion-of-full-mana` | ? |
| 37 | Potion of Rejuvenation<br>`item.design.base.potion-of-rejuvenation` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-rejuvenation-r1.png) | `base.potion-of-rejuvenation` | ? |
| 38 | Potion of Full Rejuvenation<br>`item.design.base.potion-of-full-rejuvenation` | potions-elixirs | [r1](../../assets/concept-art/items/r1/potion-of-full-rejuvenation-r1.png) | `base.potion-of-full-rejuvenation` | ? |
| 39 | Spellbook blue (Lightning)<br>`item.design.base.spellbook-blue` | spellbooks | [r1](../../assets/concept-art/items/r1/spellbook-blue-r1.png) | `base.spellbook-blue` | ? |
| 40 | Spellbook Fire<br>`item.design.base.spellbook-red` | spellbooks | [r1](../../assets/concept-art/items/r1/spellbook-red-r1.png) | `base.spellbook-red` | ? |
| 41 | Spellbook Magic<br>`item.design.base.spellbook-grey` | spellbooks | [r1](../../assets/concept-art/items/r1/spellbook-grey-r1.png) | `base.spellbook-grey` | ? |
| 42 | Oil flask physical family (label variants)<br>`item.design.base.oil-flask` | oils | [r1](../../assets/concept-art/items/r1/oil-flask-r1.png) | `base.oil-flask` | ? |
| 43 | Club<br>`item.design.base.club` | blunt-weapons | [r1](../../assets/concept-art/items/r1/club-r1.png) | `base.club` | ? |
| 44 | Flail<br>`item.design.base.flail` | blunt-weapons | [r2](../../assets/concept-art/items/r2/flail-r2.png) | `base.flail` | ? |
| 45 | Mace<br>`item.design.base.mace` | blunt-weapons | [r1](../../assets/concept-art/items/r1/mace-r1.png) | `base.mace` | ? |
| 46 | Maul<br>`item.design.base.maul` | blunt-weapons | [r1](../../assets/concept-art/items/r1/maul-r1.png) | `base.maul` | ? |
| 47 | Morning Star<br>`item.design.base.morning-star` | blunt-weapons | [r1](../../assets/concept-art/items/r1/morning-star-r1.png) | `base.morning-star` | ? |
| 48 | Spiked Club<br>`item.design.base.spiked-club` | blunt-weapons | [r1](../../assets/concept-art/items/r1/spiked-club-r1.png) | `base.spiked-club` | ? |
| 49 | War Hammer<br>`item.design.base.war-hammer` | blunt-weapons | [r1](../../assets/concept-art/items/r1/war-hammer-r1.png) | `base.war-hammer` | ? |
| 50 | Baranar's Star<br>`item.design.unique.baranar-s-star` | blunt-weapons | [r1](../../assets/concept-art/items/r1/morning-star-r1.png) | `base.morning-star` | ? |
| 51 | Civerb's Cudgel<br>`item.design.unique.civerb-s-cudgel` | blunt-weapons | [r1](../../assets/concept-art/items/r1/mace-r1.png) | `base.mace` | ? |
| 52 | Crackrust<br>`item.design.unique.crackrust` | blunt-weapons | [r1](../../assets/concept-art/items/r1/mace-r1.png) | `base.mace` | ? |
| 53 | Dreamflange<br>`item.design.unique.dreamflange` | blunt-weapons | [r1](../../assets/concept-art/items/r1/mace-r1.png) | `base.mace` | ? |
| 54 | Gnarled Root<br>`item.design.unique.gnarled-root` | blunt-weapons | [r1](../../assets/concept-art/items/r1/club-r1.png) | `base.club` | ? |
| 55 | Hammer of Jholm<br>`item.design.unique.hammer-of-jholm` | blunt-weapons | [r1](../../assets/concept-art/items/r1/maul-r1.png) | `base.maul` | ? |
| 56 | Schaefer's Hammer<br>`item.design.unique.schaefer-s-hammer` | blunt-weapons | [r1](../../assets/concept-art/items/r1/war-hammer-r1.png) | `base.war-hammer` | ? |
| 57 | The Celestial Star<br>`item.design.unique.the-celestial-star` | blunt-weapons | [r1](../../assets/concept-art/items/r2/flail-r2.png) | `base.flail` | ? |
| 58 | The Cranium Basher<br>`item.design.unique.the-cranium-basher` | blunt-weapons | [r1](../../assets/concept-art/items/r1/maul-r1.png) | `base.maul` | ? |
| 59 | Composite Bow<br>`item.design.base.composite-bow` | bows | [r1](../../assets/concept-art/items/r1/composite-bow-r1.png); estudo incompleto | `base.composite-bow` | ? |
| 60 | Hunter's Bow<br>`item.design.base.hunter-s-bow` | bows | [r1](../../assets/concept-art/items/r1/hunter-s-bow-r1.png); estudo incompleto | `base.hunter-s-bow` | ? |
| 61 | Long Battle Bow<br>`item.design.base.long-battle-bow` | bows | [r1](../../assets/concept-art/items/r1/long-battle-bow-r1.png) | `base.long-battle-bow` | ? |
| 62 | Long Bow<br>`item.design.base.long-bow` | bows | [r1](../../assets/concept-art/items/r1/hunter-s-bow-r1.png); estudo incompleto | `base.hunter-s-bow` | ? |
| 63 | Long War Bow<br>`item.design.base.long-war-bow` | bows | [r1](../../assets/concept-art/items/r1/long-war-bow-r1.png) | `base.long-war-bow` | ? |
| 64 | Short Battle Bow<br>`item.design.base.short-battle-bow` | bows | [r1](../../assets/concept-art/items/r1/short-battle-bow-r1.png); estudo incompleto | `base.short-battle-bow` | ? |
| 65 | Short Bow<br>`item.design.base.short-bow` | bows | [r1](../../assets/concept-art/items/r1/short-bow-r1.png); estudo incompleto | `base.short-bow` | ? |
| 66 | Short War Bow<br>`item.design.base.short-war-bow` | bows | [r1](../../assets/concept-art/items/r1/short-war-bow-r1.png) | `base.short-war-bow` | ? |
| 67 | Bow of the Dead<br>`item.design.unique.bow-of-the-dead` | bows | [r1](../../assets/concept-art/items/r1/deadly-hunter-r1.png) | `unique.deadly-hunter` | ? |
| 68 | Deadly Hunter<br>`item.design.unique.deadly-hunter` | bows | [r1](../../assets/concept-art/items/r1/deadly-hunter-r1.png) | `unique.deadly-hunter` | ? |
| 69 | Eaglehorn<br>`item.design.unique.eaglehorn` | bows | [r1](../../assets/concept-art/items/r1/deadly-hunter-r1.png) | `unique.deadly-hunter` | ? |
| 70 | Flamedart<br>`item.design.unique.flamedart` | bows | [r1](../../assets/concept-art/items/r1/hunter-s-bow-r1.png); estudo incompleto | `base.hunter-s-bow` | ? |
| 71 | Fleshstinger<br>`item.design.unique.fleshstinger` | bows | [r1](../../assets/concept-art/items/r1/hunter-s-bow-r1.png); estudo incompleto | `base.hunter-s-bow` | ? |
| 72 | The Blackoak Bow<br>`item.design.unique.the-blackoak-bow` | bows | [r1](../../assets/concept-art/items/r1/hunter-s-bow-r1.png); estudo incompleto | `base.hunter-s-bow` | ? |
| 73 | The Celestial Bow<br>`item.design.unique.the-celestial-bow` | bows | [r1](../../assets/concept-art/items/r1/composite-bow-r1.png); estudo incompleto | `base.composite-bow` | ? |
| 74 | The Needler<br>`item.design.unique.the-needler` | bows | [r1](../../assets/concept-art/items/r1/the-needler-r1.png); estudo incompleto | `unique.the-needler` | ? |
| 75 | The Rift Bow<br>`item.design.unique.the-rift-bow` | bows | [r1](../../assets/concept-art/items/r1/short-bow-r1.png); estudo incompleto | `base.short-bow` | ? |
| 76 | Windforce<br>`item.design.unique.windforce` | bows | [r1](../../assets/concept-art/items/r1/windforce-r1.png) | `unique.windforce` | ? |
| 77 | Composite Staff<br>`item.design.base.composite-staff` | staves | [r1](../../assets/concept-art/items/r1/composite-staff-r1.png) | `base.composite-staff` | ? |
| 78 | Long Staff<br>`item.design.base.long-staff` | staves | [r1](../../assets/concept-art/items/r1/long-staff-r1.png) | `base.long-staff` | ? |
| 79 | Quarter Staff<br>`item.design.base.quarter-staff` | staves | [r1](../../assets/concept-art/items/r2/short-staff-r2.png) | `base.short-staff` | ? |
| 80 | Short Staff<br>`item.design.base.short-staff` | staves | [r2](../../assets/concept-art/items/r2/short-staff-r2.png) | `base.short-staff` | ? |
| 81 | War Staff<br>`item.design.base.war-staff` | staves | [r1](../../assets/concept-art/items/r1/war-staff-r1.png) | `base.war-staff` | ? |
| 82 | Gleamsong<br>`item.design.unique.gleamsong` | staves | [r1](../../assets/concept-art/items/r2/short-staff-r2.png) | `base.short-staff` | ? |
| 83 | Immolator<br>`item.design.unique.immolator` | staves | [r1](../../assets/concept-art/items/r1/long-staff-r1.png) | `base.long-staff` | ? |
| 84 | Mindcry<br>`item.design.unique.mindcry` | staves | [r1](../../assets/concept-art/items/r2/short-staff-r2.png) | `base.short-staff` | ? |
| 85 | Naj's Puzzler<br>`item.design.unique.naj-s-puzzler` | staves | [r1](../../assets/concept-art/items/r1/long-staff-r1.png) | `base.long-staff` | ? |
| 86 | Rod of Onan<br>`item.design.unique.rod-of-onan` | staves | [r1](../../assets/concept-art/items/r1/war-staff-r1.png) | `base.war-staff` | ? |
| 87 | Staff of Shadows<br>`item.design.unique.staff-of-shadows` | staves | [r1](../../assets/concept-art/items/r1/long-staff-r1.png) | `base.long-staff` | ? |
| 88 | Storm Spire<br>`item.design.unique.storm-spire` | staves | [r1](../../assets/concept-art/items/r1/war-staff-r1.png) | `base.war-staff` | ? |
| 89 | The Protector<br>`item.design.unique.the-protector` | staves | [r1](../../assets/concept-art/items/r1/the-protector-r1.png) | `unique.the-protector` | ? |
| 90 | Thundercall<br>`item.design.unique.thundercall` | staves | [r1](../../assets/concept-art/items/r1/composite-staff-r1.png) | `base.composite-staff` | ? |
| 91 | Cap<br>`item.design.base.cap` | headgear | [r1](../../assets/concept-art/items/r1/cap-r1.png) | `base.cap` | ? |
| 92 | Crown<br>`item.design.base.crown` | headgear | [r1](../../assets/concept-art/items/r1/crown-r1.png) | `base.crown` | ? |
| 93 | Full Helm<br>`item.design.base.full-helm` | headgear | [r1](../../assets/concept-art/items/r1/full-helm-r1.png) | `base.full-helm` | ? |
| 94 | Great Helm<br>`item.design.base.great-helm` | headgear | [r1](../../assets/concept-art/items/r1/great-helm-r1.png) | `base.great-helm` | ? |
| 95 | Helm<br>`item.design.base.helm` | headgear | [r1](../../assets/concept-art/items/r1/helm-r1.png) | `base.helm` | ? |
| 96 | Skull Cap<br>`item.design.base.skull-cap` | headgear | [r1](../../assets/concept-art/items/r1/skull-cap-r1.png) | `base.skull-cap` | ? |
| 97 | Fool's Crest<br>`item.design.unique.fool-s-crest` | headgear | [r1](../../assets/concept-art/items/r1/fool-s-crest-r1.png) | `unique.fool-s-crest` | ? |
| 98 | Gotterdamerung<br>`item.design.unique.gotterdamerung` | headgear | [r1](../../assets/concept-art/items/r1/gotterdamerung-r1.png) | `unique.gotterdamerung` | ? |
| 99 | Harlequin Crest<br>`item.design.unique.harlequin-crest` | headgear | [r1](../../assets/concept-art/items/r1/harlequin-crest-r1.png) | `unique.harlequin-crest` | ? |
| 100 | Helm of Spirits<br>`item.design.unique.helm-of-spirits` | headgear | [r1](../../assets/concept-art/items/r1/helm-of-spirits-r1.png) | `unique.helm-of-spirits` | ? |
| 101 | OverLord's Helm<br>`item.design.unique.overlord-s-helm` | headgear | [r1](../../assets/concept-art/items/r1/overlord-s-helm-r1.png) | `unique.overlord-s-helm` | ? |
| 102 | Royal Circlet<br>`item.design.unique.royal-circlet` | headgear | [r1](../../assets/concept-art/items/r1/royal-circlet-r1.png) | `unique.royal-circlet` | ? |
| 103 | The Undead Crown<br>`item.design.unique.the-undead-crown` | headgear | [r1](../../assets/concept-art/items/r1/helm-of-spirits-r1.png) | `unique.helm-of-spirits` | ? |
| 104 | Thinking Cap<br>`item.design.unique.thinking-cap` | headgear | [r1](../../assets/concept-art/items/r1/thinking-cap-r1.png) | `unique.thinking-cap` | ? |
| 105 | Veil of Steel<br>`item.design.unique.veil-of-steel` | headgear | [r1](../../assets/concept-art/items/r1/gotterdamerung-r1.png) | `unique.gotterdamerung` | ? |
| 106 | Cape<br>`item.design.base.cape` | body-armor | [r1](../../assets/concept-art/items/r1/cape-r1.png) | `base.cape` | ? |
| 107 | Cloak<br>`item.design.base.cloak` | body-armor | [r1](../../assets/concept-art/items/r1/cloak-r1.png) | `base.cloak` | ? |
| 108 | Rags<br>`item.design.base.rags` | body-armor | [r1](../../assets/concept-art/items/r1/rags-r1.png) | `base.rags` | ? |
| 109 | Robe<br>`item.design.base.robe` | body-armor | [r1](../../assets/concept-art/items/r1/robe-r1.png) | `base.robe` | ? |
| 110 | Hard Leather Armor<br>`item.design.base.hard-leather-armor` | body-armor | [r1](../../assets/concept-art/items/r1/hard-leather-armor-r1.png) | `base.hard-leather-armor` | ? |
| 111 | Leather Armor<br>`item.design.base.leather-armor` | body-armor | [r1](../../assets/concept-art/items/r1/leather-armor-r1.png) | `base.leather-armor` | ? |
| 112 | Quilted Armor<br>`item.design.base.quilted-armor` | body-armor | [r1](../../assets/concept-art/items/r1/quilted-armor-r1.png) | `base.quilted-armor` | ? |
| 113 | Studded Leather Armor<br>`item.design.base.studded-leather-armor` | body-armor | [r1](../../assets/concept-art/items/r1/studded-leather-armor-r1.png) | `base.studded-leather-armor` | ? |
| 114 | Breast Plate<br>`item.design.base.breast-plate` | body-armor | [r1](../../assets/concept-art/items/r1/breast-plate-r1.png) | `base.breast-plate` | ? |
| 115 | Chain Mail<br>`item.design.base.chain-mail` | body-armor | [r1](../../assets/concept-art/items/r1/chain-mail-r1.png); estudo incompleto | `base.chain-mail` | ? |
| 116 | Field Plate<br>`item.design.base.field-plate` | body-armor | [r1](../../assets/concept-art/items/r1/field-plate-r1.png) | `base.field-plate` | ? |
| 117 | Full Plate Mail<br>`item.design.base.full-plate-mail` | body-armor | [r1](../../assets/concept-art/items/r1/full-plate-mail-r1.png); estudo incompleto | `base.full-plate-mail` | ? |
| 118 | Gothic Plate<br>`item.design.base.gothic-plate` | body-armor | [r1](../../assets/concept-art/items/r1/gothic-plate-r1.png); estudo incompleto | `base.gothic-plate` | ? |
| 119 | Plate Mail<br>`item.design.base.plate-mail` | body-armor | [r1](../../assets/concept-art/items/r1/field-plate-r1.png) | `base.field-plate` | ? |
| 120 | Ring Mail<br>`item.design.base.ring-mail` | body-armor | [r1](../../assets/concept-art/items/r1/ring-mail-r1.png); estudo incompleto | `base.ring-mail` | ? |
| 121 | Scale Mail<br>`item.design.base.scale-mail` | body-armor | [r1](../../assets/concept-art/items/r1/scale-mail-r1.png) | `base.scale-mail` | ? |
| 122 | Splint Mail<br>`item.design.base.splint-mail` | body-armor | [r1](../../assets/concept-art/items/r1/splint-mail-r1.png); estudo incompleto | `base.splint-mail` | ? |
| 123 | Arkaine's Valor<br>`item.design.unique.arkaine-s-valor` | body-armor | [r1](../../assets/concept-art/items/r1/arkaine-s-valor-r1.png) | `unique.arkaine-s-valor` | ? |
| 124 | Demonspike Coat<br>`item.design.unique.demonspike-coat` | body-armor | [r1](../../assets/concept-art/items/r1/full-plate-mail-r1.png); estudo incompleto | `base.full-plate-mail` | ? |
| 125 | Leather of Aut<br>`item.design.unique.leather-of-aut` | body-armor | [r1](../../assets/concept-art/items/r1/leather-armor-r1.png) | `base.leather-armor` | ? |
| 126 | Naj's Light Plate<br>`item.design.unique.naj-s-light-plate` | body-armor | [r1](../../assets/concept-art/items/r1/naj-s-light-plate-r1.png) | `unique.naj-s-light-plate` | ? |
| 127 | Nightscape<br>`item.design.unique.nightscape` | body-armor | [r1](../../assets/concept-art/items/r1/nightscape-r1.png) | `unique.nightscape` | ? |
| 128 | Scavenger Carapace<br>`item.design.unique.scavenger-carapace` | body-armor | [r1](../../assets/concept-art/items/r1/breast-plate-r1.png) | `base.breast-plate` | ? |
| 129 | Sparking Mail<br>`item.design.unique.sparking-mail` | body-armor | [r1](../../assets/concept-art/items/r1/chain-mail-r1.png); estudo incompleto | `base.chain-mail` | ? |
| 130 | The Gladiator's Bane<br>`item.design.unique.the-gladiator-s-bane` | body-armor | [r1](../../assets/concept-art/items/r1/studded-leather-armor-r1.png) | `base.studded-leather-armor` | ? |
| 131 | The Rainbow Cloak<br>`item.design.unique.the-rainbow-cloak` | body-armor | [r1](../../assets/concept-art/items/r1/nightscape-r1.png) | `unique.nightscape` | ? |
| 132 | Torn Flesh of Souls<br>`item.design.unique.torn-flesh-of-souls` | body-armor | [r1](../../assets/concept-art/items/r1/torn-flesh-of-souls-r1.png) | `unique.torn-flesh-of-souls` | ? |
| 133 | Wisdom's Wrap<br>`item.design.unique.wisdom-s-wrap` | body-armor | [r1](../../assets/concept-art/items/r1/nightscape-r1.png) | `unique.nightscape` | ? |
| 134 | Amulet<br>`item.design.base.amulet` | jewelry | [r1](../../assets/concept-art/items/r1/amulet-r1.png) | `base.amulet` | ? |
| 135 | Ring<br>`item.design.base.ring` | jewelry | [r1](../../assets/concept-art/items/r1/ring-r1.png) | `base.ring` | ? |
| 136 | Bramble<br>`item.design.unique.bramble` | jewelry | [r1](../../assets/concept-art/items/r1/bramble-r1.png) | `unique.bramble` | ? |
| 137 | Constricting Ring<br>`item.design.unique.constricting-ring` | jewelry | [r1](../../assets/concept-art/items/r1/constricting-ring-r1.png) | `unique.constricting-ring` | ? |
| 138 | Empyrean Band<br>`item.design.unique.empyrean-band` | jewelry | [r1](../../assets/concept-art/items/r1/empyrean-band-r1.png) | `unique.empyrean-band` | ? |
| 139 | Optic Amulet<br>`item.design.unique.optic-amulet` | jewelry | [r1](../../assets/concept-art/items/r1/optic-amulet-r1.png) | `unique.optic-amulet` | ? |
| 140 | Ring of Engagement<br>`item.design.unique.ring-of-engagement` | jewelry | [r1](../../assets/concept-art/items/r1/ring-of-engagement-r1.png) | `unique.ring-of-engagement` | ? |
| 141 | Ring of Regha<br>`item.design.unique.ring-of-regha` | jewelry | [r1](../../assets/concept-art/items/r1/ring-of-regha-r1.png) | `unique.ring-of-regha` | ? |
| 142 | Ring of Truth<br>`item.design.unique.ring-of-truth` | jewelry | [r1](../../assets/concept-art/items/r1/ring-of-truth-r1.png) | `unique.ring-of-truth` | ? |
| 143 | The Bleeder<br>`item.design.unique.the-bleeder` | jewelry | [r1](../../assets/concept-art/items/r1/the-bleeder-r1.png) | `unique.the-bleeder` | ? |
| 144 | Arena Potion<br>`item.design.base.arena-potion` | potions-elixirs | [r1](../../assets/concept-art/items/r1/arena-potion-r1.png) | `base.arena-potion` | ? |
| 145 | Elixir of Dexterity<br>`item.design.base.elixir-of-dexterity` | potions-elixirs | [r1](../../assets/concept-art/items/r1/elixir-of-dexterity-r1.png) | `base.elixir-of-dexterity` | ? |
| 146 | Elixir of Magic<br>`item.design.base.elixir-of-magic` | potions-elixirs | [r1](../../assets/concept-art/items/r1/elixir-of-magic-r1.png) | `base.elixir-of-magic` | ? |
| 147 | Elixir of Strength<br>`item.design.base.elixir-of-strength` | potions-elixirs | [r1](../../assets/concept-art/items/r1/elixir-of-strength-r1.png) | `base.elixir-of-strength` | ? |
| 148 | Elixir of Vitality<br>`item.design.base.elixir-of-vitality` | potions-elixirs | [r1](../../assets/concept-art/items/r1/elixir-of-vitality-r1.png) | `base.elixir-of-vitality` | ? |
| 149 | Golden Elixir<br>`item.design.base.golden-elixir` | potions-elixirs | [r1](../../assets/concept-art/items/r1/golden-elixir-r1.png) | `base.golden-elixir` | ? |
| 150 | Spectral Elixir<br>`item.design.base.spectral-elixir` | potions-elixirs | [r1](../../assets/concept-art/items/r1/spectral-elixir-r1.png) | `base.spectral-elixir` | ? |
| 151 | Anvil of Fury<br>`item.design.base.anvil-of-fury` | quest-objects | [r1](../../assets/concept-art/items/r1/anvil-of-fury-r1.png) | `base.anvil-of-fury` | ? |
| 152 | Black Mushroom<br>`item.design.base.black-mushroom` | quest-objects | [r1](../../assets/concept-art/items/r1/black-mushroom-r1.png) | `base.black-mushroom` | ? |
| 153 | Blood Stone<br>`item.design.base.blood-stone` | quest-objects | [r1](../../assets/concept-art/items/r1/blood-stone-r1.png) | `base.blood-stone` | ? |
| 154 | Brain<br>`item.design.base.brain` | quest-objects | [r1](../../assets/concept-art/items/r1/brain-r1.png) | `base.brain` | ? |
| 155 | Cathedral Map<br>`item.design.base.cathedral-map` | quest-objects | [r1](../../assets/concept-art/items/r1/cathedral-map-r1.png) | `base.cathedral-map` | ? |
| 156 | Fungal Tome<br>`item.design.base.fungal-tome` | quest-objects | [r1](../../assets/concept-art/items/r1/fungal-tome-r1.png) | `base.fungal-tome` | ? |
| 157 | Magic Rock<br>`item.design.base.magic-rock` | quest-objects | [r1](../../assets/concept-art/items/r1/magic-rock-r1.png) | `base.magic-rock` | ? |
| 158 | Staff of Lazarus<br>`item.design.base.staff-of-lazarus` | quest-objects | [r1](../../assets/concept-art/items/r1/staff-of-lazarus-r1.png) | `base.staff-of-lazarus` | ? |
| 159 | Tavern Sign<br>`item.design.base.tavern-sign` | quest-objects | [r1](../../assets/concept-art/items/r1/tavern-sign-r1.png) | `base.tavern-sign` | ? |
| 160 | Aguinara's Hatchet<br>`item.design.unique.aguinara-s-hatchet` | axes | [r1](../../assets/concept-art/items/r1/small-axe-r1.png) | `base.small-axe` | ? |
| 161 | Bloodslayer<br>`item.design.unique.bloodslayer` | axes | [r1](../../assets/concept-art/items/r1/axe-r1.png) | `base.axe` | ? |
| 162 | Hellslayer<br>`item.design.unique.hellslayer` | axes | [r1](../../assets/concept-art/items/r1/battle-axe-r1.png) | `base.battle-axe` | ? |
| 163 | Messerschmidt's Reaver<br>`item.design.unique.messerschmidt-s-reaver` | axes | [r1](../../assets/concept-art/items/r1/messerschmidt-s-reaver-r1.png) | `unique.messerschmidt-s-reaver` | ? |
| 164 | Sharp Beak<br>`item.design.unique.sharp-beak` | axes | [r1](../../assets/concept-art/items/r1/great-axe-r1.png) | `base.great-axe` | ? |
| 165 | Stonecleaver<br>`item.design.unique.stonecleaver` | axes | [r1](../../assets/concept-art/items/r1/stonecleaver-r1.png) | `unique.stonecleaver` | ? |
| 166 | The Celestial Axe<br>`item.design.unique.the-celestial-axe` | axes | [r1](../../assets/concept-art/items/r1/battle-axe-r1.png) | `base.battle-axe` | ? |
| 167 | The Mangler<br>`item.design.unique.the-mangler` | axes | [r1](../../assets/concept-art/items/r1/axe-r1.png) | `base.axe` | ? |
| 168 | Wicked Axe<br>`item.design.unique.wicked-axe` | axes | [r1](../../assets/concept-art/items/r1/great-axe-r1.png) | `base.great-axe` | ? |
| 169 | Doombringer<br>`item.design.unique.doombringer` | blades | [r1](../../assets/concept-art/items/r1/bastard-sword-r1.png) | `base.bastard-sword` | ? |
| 170 | Gibbous Moon<br>`item.design.unique.gibbous-moon` | blades | [r1](../../assets/concept-art/items/r1/broad-sword-r1.png) | `base.broad-sword` | ? |
| 171 | Ice Shank<br>`item.design.unique.ice-shank` | blades | [r1](../../assets/concept-art/items/r1/long-sword-r1.png) | `base.long-sword` | ? |
| 172 | Inferno<br>`item.design.unique.inferno` | blades | [r1](../../assets/concept-art/items/r1/long-sword-r1.png) | `base.long-sword` | ? |
| 173 | Lightsabre<br>`item.design.unique.lightsabre` | blades | [r1](../../assets/concept-art/items/r1/sabre-r1.png) | `base.sabre` | ? |
| 174 | Shadowhawk<br>`item.design.unique.shadowhawk` | blades | [r1](../../assets/concept-art/items/r1/broad-sword-r1.png) | `base.broad-sword` | ? |
| 175 | Shirotachi<br>`item.design.unique.shirotachi` | blades | [r1](../../assets/concept-art/items/r1/great-sword-r1.png); estudo incompleto | `base.great-sword` | ? — excluído pela raiz manual |
| 176 | The Bonesaw<br>`item.design.unique.the-bonesaw` | blades | [r1](../../assets/concept-art/items/r1/claymore-r1.png) | `base.claymore` | ? |
| 177 | The Executioner's Blade<br>`item.design.unique.the-executioner-s-blade` | blades | [r1](../../assets/concept-art/items/r1/the-executioner-s-blade-r1.png) | `unique.the-executioner-s-blade` | ? |
| 178 | The Falcon's Talon<br>`item.design.unique.the-falcon-s-talon` | blades | [r1](../../assets/concept-art/items/r1/gryphon-s-claw-r1.png) | `unique.gryphon-s-claw` | ? |
| 179 | The Grandfather<br>`item.design.unique.the-grandfather` | blades | [r1](../../assets/concept-art/items/r1/the-grandfather-r1.png) | `unique.the-grandfather` | ? |
| 180 | The Grizzly<br>`item.design.unique.the-grizzly` | blades | [r1](../../assets/concept-art/items/r1/the-grizzly-r1.png) | `unique.the-grizzly` | ? |
| 181 | Wizardspike<br>`item.design.unique.wizardspike` | blades | [r1](../../assets/concept-art/items/r1/wizardspike-r1.png) | `unique.wizardspike` | ? |
| 182 | Blackoak Shield<br>`item.design.unique.blackoak-shield` | shields | [r1](../../assets/concept-art/items/r1/blackoak-shield-r1.png) | `unique.blackoak-shield` | ? |
| 183 | Dragon's Breach<br>`item.design.unique.dragon-s-breach` | shields | [r1](../../assets/concept-art/items/r1/dragon-s-breach-r1.png) | `unique.dragon-s-breach` | ? |
| 184 | Holy Defender<br>`item.design.unique.holy-defender` | shields | [r1](../../assets/concept-art/items/r1/blackoak-shield-r1.png) | `unique.blackoak-shield` | ? |
| 185 | Split Skull Shield<br>`item.design.unique.split-skull-shield` | shields | [r1](../../assets/concept-art/items/r1/split-skull-shield-r1.png) | `unique.split-skull-shield` | ? |
| 186 | Stormshield<br>`item.design.unique.stormshield` | shields | [r1](../../assets/concept-art/items/r1/gothic-shield-r1.png) | `base.gothic-shield` | ? |
| 187 | The Deflector<br>`item.design.unique.the-deflector` | shields | [r1](../../assets/concept-art/items/r1/buckler-r1.png) | `base.buckler` | ? |
| 188 | Gold pile large<br>`item.design.dynamic.gold-large` | gold | [r1](../../assets/concept-art/items/r1/gold-large-r1.png); estudo incompleto | `dynamic.gold-large` | ? |
| 189 | Gold pile medium<br>`item.design.dynamic.gold-medium` | gold | [r1](../../assets/concept-art/items/r1/gold-medium-r1.png); estudo incompleto | `dynamic.gold-medium` | ? |
| 190 | Ear<br>`item.design.base.ear` | ears | [r1](../../assets/concept-art/items/r1/ear-warrior-r1.png) | `dynamic.ear-warrior` | ? |
| 191 | Ear trophy rogue<br>`item.design.dynamic.ear-rogue` | ears | [r1](../../assets/concept-art/items/r1/ear-rogue-r1.png) | `dynamic.ear-rogue` | ? |
| 192 | Ear trophy sorcerer<br>`item.design.dynamic.ear-sorcerer` | ears | [r1](../../assets/concept-art/items/r1/ear-sorcerer-r1.png) | `dynamic.ear-sorcerer` | ? |
| 193 | Ear trophy warrior<br>`item.design.dynamic.ear-warrior` | ears | [r1](../../assets/concept-art/items/r1/ear-warrior-r1.png) | `dynamic.ear-warrior` | ? |
| None | Thunderclap<br>`item.design.unique.thunderclap` | blunt-weapons | missing-hellfire-objcurs2 | `unique.thunderclap` | ? |
| None | Blitzen<br>`item.design.unique.blitzen` | bows | missing-hellfire-objcurs2 | `unique.blitzen` | ? |
| None | Flambeau<br>`item.design.unique.flambeau` | bows | missing-hellfire-objcurs2 | `unique.flambeau` | ? |
| None | Gnat Sting<br>`item.design.unique.gnat-sting` | bows | missing-hellfire-objcurs2 | `unique.gnat-sting` | ? |
| None | Armor of Gloom<br>`item.design.unique.armor-of-gloom` | body-armor | missing-hellfire-objcurs2 | `unique.armor-of-gloom` | ? |
| None | Bone Chain Armor<br>`item.design.unique.bone-chain-armor` | body-armor | missing-hellfire-objcurs2 | `unique.bone-chain-armor` | ? |
| None | Bovine Plate<br>`item.design.unique.bovine-plate` | body-armor | missing-hellfire-objcurs2 | `unique.bovine-plate` | ? |
| None | Demon Plate Armor<br>`item.design.unique.demon-plate-armor` | body-armor | missing-hellfire-objcurs2 | `unique.demon-plate-armor` | ? |
| None | Auric Amulet<br>`item.design.base.auric-amulet` | jewelry | missing-hellfire-objcurs2 | `base.auric-amulet` | ? |
| None | Acolyte's Amulet<br>`item.design.unique.acolyte-s-amulet` | jewelry | missing-hellfire-objcurs2 | `unique.acolyte-s-amulet` | ? |
| None | Amulet of Warding<br>`item.design.unique.amulet-of-warding` | jewelry | missing-hellfire-objcurs2 | `unique.amulet-of-warding` | ? |
| None | Giant's Knuckle<br>`item.design.unique.giant-s-knuckle` | jewelry | missing-hellfire-objcurs2 | `unique.giant-s-knuckle` | ? |
| None | Gladiator's Ring<br>`item.design.unique.gladiator-s-ring` | jewelry | missing-hellfire-objcurs2 | `unique.gladiator-s-ring` | ? |
| None | Karik's Ring<br>`item.design.unique.karik-s-ring` | jewelry | missing-hellfire-objcurs2 | `unique.karik-s-ring` | ? |
| None | Mercurial Ring<br>`item.design.unique.mercurial-ring` | jewelry | missing-hellfire-objcurs2 | `unique.mercurial-ring` | ? |
| None | Ring of Magma<br>`item.design.unique.ring-of-magma` | jewelry | missing-hellfire-objcurs2 | `unique.ring-of-magma` | ? |
| None | Ring of the Mystics<br>`item.design.unique.ring-of-the-mystics` | jewelry | missing-hellfire-objcurs2 | `unique.ring-of-the-mystics` | ? |
| None | Ring of Thunder<br>`item.design.unique.ring-of-thunder` | jewelry | missing-hellfire-objcurs2 | `unique.ring-of-thunder` | ? |
| None | Xorine's Ring<br>`item.design.unique.xorine-s-ring` | jewelry | missing-hellfire-objcurs2 | `unique.xorine-s-ring` | ? |
| None | Brown Suit<br>`item.design.base.brown-suit` | quest-objects | missing-hellfire-objcurs2 | `base.brown-suit` | ? |
| None | Grey Suit<br>`item.design.base.grey-suit` | quest-objects | missing-hellfire-objcurs2 | `base.grey-suit` | ? |
| None | Reconstructed Note<br>`item.design.base.reconstructed-note` | quest-objects | missing-hellfire-objcurs2 | `base.reconstructed-note` | ? |
| None | Rune Bomb<br>`item.design.base.rune-bomb` | quest-objects | missing-hellfire-objcurs2 | `base.rune-bomb` | ? |
| None | Theodore<br>`item.design.base.theodore` | quest-objects | missing-hellfire-objcurs2 | `base.theodore` | ? |
| None | Torn Note 1<br>`item.design.base.torn-note-1` | quest-objects | missing-hellfire-objcurs2 | `base.torn-note-1` | ? |
| None | Torn Note 2<br>`item.design.base.torn-note-2` | quest-objects | missing-hellfire-objcurs2 | `base.torn-note-2` | ? |
| None | Torn Note 3<br>`item.design.base.torn-note-3` | quest-objects | missing-hellfire-objcurs2 | `base.torn-note-3` | ? |
| None | Lightforge<br>`item.design.unique.lightforge` | quest-objects | unresolved-lightforge-source | `unique.lightforge` | ? |
| None | Diamondedge<br>`item.design.unique.diamondedge` | blades | missing-hellfire-objcurs2 | `unique.diamondedge` | ? |
| None | Eater of Souls<br>`item.design.unique.eater-of-souls` | blades | missing-hellfire-objcurs2 | `unique.eater-of-souls` | ? |
| None | Greater Rune of Fire<br>`item.design.base.greater-rune-of-fire` | runes | missing-hellfire-objcurs2 | `base.greater-rune-of-fire` | ? |
| None | Greater Rune of Lightning<br>`item.design.base.greater-rune-of-lightning` | runes | missing-hellfire-objcurs2 | `base.greater-rune-of-lightning` | ? |
| None | Rune of Fire<br>`item.design.base.rune-of-fire` | runes | missing-hellfire-objcurs2 | `base.rune-of-fire` | ? |
| None | Rune of Lightning<br>`item.design.base.rune-of-lightning` | runes | missing-hellfire-objcurs2 | `base.rune-of-lightning` | ? |
| None | Rune of Stone<br>`item.design.base.rune-of-stone` | runes | missing-hellfire-objcurs2 | `base.rune-of-stone` | ? |

## Reprodução e limites

`python -B docs/items-study/build_smart_production_review.py --check` confere os cinco derivados sem escrever. O gerador lê apenas arquivos públicos locais e verifica os PNGs autorais por hash; não abre conta, mídia de modelo, credenciais ou referências licenciadas privadas. O contexto é um recibo de decisões/relatos, não autorização de execução. Nenhum upload, geração, reserva, API, instalação ou teste da partida foi realizado por este pacote.
