---
title: "Itens e novo HUD: arte em revisão para Diablo 3D"
date: 2026-10-08
description: "Nove conceitos de itens ligados ao catálogo e um novo HUD com seis peças autorais: as propostas visuais, os testes realizados e as próximas etapas."
slug: itens-e-novo-hud
image: /assets/concepts/items/r1/potion-of-healing-r1.webp
image_alt: "Prancha multivista candidata da Potion of Healing, criada para orientar a futura modelagem; não é um modelo 3D nem um ícone instalado."
category: Protótipo
order: 14
status: published
---

Uma espada precisa ser reconhecível no inventário, na mão do personagem e quando cai no chão. Essa coerência orienta a nova frente de itens do Diablo 3D: organizar as identidades do jogo, desenvolver os conceitos e, depois da revisão, construir o modelo que dará origem às diferentes apresentações de cada objeto.

O novo acabamento do HUD também avançou, com arte própria e uma composição que conserva as ações do jogo. Este registro reúne as nove primeiras pranchas de itens e o candidato de interface, distinguindo o que é conceito, prévia de editor e composição técnica de teste.

**O escopo inclui todos os itens do projeto, acompanhando o objetivo de reconstruir Diablo 1 inteiro em 3D.** Armas, escudos, armaduras, elmos, joias, poções, pergaminhos, ouro, livros e objetos de quest fazem parte do levantamento. Tristram continua sendo a primeira etapa do jogo; o catálogo de itens considera também o restante da campanha e distingue os dados de Diablo e Hellfire.

As nove imagens deste primeiro lote são **conceitos candidatos em revisão**. São pranchas geradas para estudar os objetos; ainda não são modelos 3D, ícones finais, arte aprovada ou recursos instalados no jogo. As faces ocultas propostas nas vistas precisam de avaliação, assim como a consistência entre elas.

## Um catálogo para saber o que estamos construindo

O [guia de produção dos itens](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/ITEM-ASSET-CATALOG.md) relaciona os dados públicos do projeto às famílias de desenho e às variantes. O levantamento registra:

- **168 registros base**, dos quais 166 têm nome e dois são espaços reservados vazios.
- **111 identidades únicas** reunidas entre as tabelas de Diablo e Hellfire. As tabelas se sobrepõem; somar suas linhas contaria vários itens duas vezes.
- **170 cursores de itens declarados**: 136 principais e 34 de Hellfire, associados às bases, aos únicos e às aparências dinâmicas.
- **228 unidades propostas de desenho, família ou variante**, para organizar a produção e o compartilhamento de recursos.

Essas 228 entradas **não exigem 228 malhas independentes**. Magias de pergaminhos podem compartilhar o objeto físico; livros podem usar variantes de capa; poções podem compartilhar o frasco quando isso preservar a identidade. Afixos, cargas e atributos sorteados também não multiplicam automaticamente os modelos.

Os números descrevem as fontes desta revisão, incluindo registros especiais e condições de disponibilidade. Eles não significam que todos os itens apareçam em qualquer partida, nem que seus conceitos ou modelos já estejam prontos. O [catálogo JSON](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/catalog.json), a [lista de produção em CSV](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/concept-slots.csv) e o [recibo de cobertura](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/coverage.json) permitem conferir os vínculos.

As referências locais já foram organizadas para orientar o primeiro lote, sem publicar as extrações licenciadas. As nove artes estão vinculadas a nove unidades do catálogo, e a galeria local e a integridade dos arquivos foram verificadas. As outras **219 unidades de produção** continuam pendentes; essa fila também conserva os casos de referência ainda não resolvida, como Lightforge. Ela não representa 219 modelos independentes nem uma coleção concluída.

## O mesmo objeto no inventário, no chão e equipado

Na apresentação nativa, vários equipamentos compartilham desenhos ou grupos de animação. O seletor de arma do personagem trabalha com categorias amplas, como espada, machado, arco, maça e cajado; ele não escolhe uma aparência individual para cada arma do inventário. Há também itens diferentes que compartilham o mesmo cursor. Essa estrutura ajuda a explicar por que a reconstrução precisa cuidar dos vínculos, além de produzir imagens bonitas.

A sequência de produção proposta é:

1. Identificar o item e sua referência, preservando as diferenças entre os perfis do jogo.
2. Criar e revisar o conceito, incluindo silhueta, materiais, escala e vistas coerentes.
3. Produzir um master 3D completo e revisá-lo em todos os ângulos.
4. Derivar desse mesmo master a apresentação no chão, o equipamento quando aplicável e o ícone renderizado do inventário.
5. Integrar e conferir o conjunto na partida, preservando slots, regras, animações e interação nativas.

O conceito abre essa sequência. Uma miniatura desenhada na prancha ainda não é o ícone final do jogo, e uma imagem convincente não comprova que o objeto foi modelado ou integrado. O master aceito deverá preservar a mesma identidade em todas essas apresentações.

## Primeiro lote: poções, ouro e pergaminho

As primeiras quatro pranchas estudam consumíveis e objetos familiares. Healing e Mana propõem consistência no desenho do frasco, mantendo a diferença entre os conteúdos. Ouro começa por uma apresentação da família, que ainda precisa cobrir as quantidades previstas. O pergaminho estuda o objeto físico; as variantes por magia serão tratadas separadamente.

Abra cada imagem para examinar a prancha completa. As legendas acompanham também a ampliação.

<div class="gallery-grid">
<figure class="gallery-item"><a href="/assets/concepts/items/r1/potion-of-healing-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/potion-of-healing-r1.webp" alt="Prancha de conceito da Potion of Healing, com várias vistas propostas do mesmo frasco."></a><figcaption><strong>Potion of Healing · r1</strong> Conceito candidato em revisão. Vistas propostas para a futura modelagem; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/potion-of-mana-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/potion-of-mana-r1.webp" alt="Prancha de conceito da Potion of Mana, estudando a consistência do frasco com a poção de cura."></a><figcaption><strong>Potion of Mana · r1</strong> Conceito candidato em revisão. A proposta compartilha o desenho do frasco de Healing; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/gold-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/gold-r1.webp" alt="Prancha de conceito de Gold, como estudo inicial da família de moedas."></a><figcaption><strong>Gold · r1</strong> Conceito candidato em revisão. Estudo inicial da família, sem concluir todas as quantidades de ouro; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/scroll-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/scroll-r1.webp" alt="Prancha de conceito de Scroll, com vistas propostas do pergaminho físico fechado."></a><figcaption><strong>Scroll · r1</strong> Conceito candidato em revisão. Objeto físico genérico, com variantes por magia ainda indefinidas; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
</div>

## Primeiro lote: cinco lâminas

Short Sword, Dagger, Falchion, Scimitar e Broad Sword iniciam o estudo das armas. Cada uma tem sua própria identidade no catálogo. A revisão precisa comparar as vistas, a silhueta e as proporções antes de transformar essas propostas em modelos ou decidir o que pode ser compartilhado.

<div class="gallery-grid">
<figure class="gallery-item"><a href="/assets/concepts/items/r1/short-sword-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/short-sword-r1.webp" alt="Prancha de conceito candidata da Short Sword para estudo do futuro modelo."></a><figcaption><strong>Short Sword · r1</strong> Conceito candidato em revisão; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/dagger-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/dagger-r1.webp" alt="Prancha de conceito candidata da Dagger para estudo do futuro modelo."></a><figcaption><strong>Dagger · r1</strong> Conceito candidato em revisão; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/falchion-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/falchion-r1.webp" alt="Prancha de conceito candidata da Falchion para estudo do futuro modelo."></a><figcaption><strong>Falchion · r1</strong> Conceito candidato em revisão; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/scimitar-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/scimitar-r1.webp" alt="Prancha de conceito candidata da Scimitar para estudo do futuro modelo."></a><figcaption><strong>Scimitar · r1</strong> Conceito candidato em revisão; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/broad-sword-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/broad-sword-r1.webp" alt="Prancha de conceito candidata da Broad Sword para estudo do futuro modelo."></a><figcaption><strong>Broad Sword · r1</strong> Conceito candidato em revisão; sem modelo 3D gerado, ícone final, aprovação artística ou instalação.</figcaption></figure>
</div>

## Um novo HUD com arte própria e controles preservados

O acabamento HD começou por **seis peças autorais**: estrutura central, botão sem legenda, placa escura, suporte esculpido dos globos e os líquidos vermelho e azul. Elas compõem uma interface candidata, com tamanho e posicionamento adaptados à resolução. O layout pode ser editado na ferramenta externa Godot; exportar a disposição é uma etapa explícita, separada de salvar a cena.

<figure><a href="/assets/concepts/hud/hud-native-16x9-v1.webp" data-lightbox="true"><img src="/assets/concepts/hud/hud-native-16x9-v1.webp" alt="Prancha de conceito 07 do HUD, com globos, cinto, informações, magia e os seis botões utilitários."></a><figcaption>Direção visual do HUD: conceito gerado para revisão. Esta prancha não é uma captura do jogo nem comprova a implementação de cada detalhe.</figcaption></figure>

O contrato preserva **13 áreas de interação** e seus comandos originais. Vida e mana, **oito espaços do cinto**, magia preparada, informações e avisos continuam vindo do jogo, assim como os estados de multijogador. Os seis botões utilitários mantêm as ações existentes; a nova arte não acrescenta funções. As legendas usam a fonte nativa no runtime, enquanto a prévia Godot usa fonte de sistema: o estudo do editor não comprova acabamento tipográfico final na partida.

## O que os testes do HUD mostram

A revisão final foi compilada e exercitada em **13 composições técnicas**, incluindo globos vazios, pela metade e cheios, botão pressionado, cinco linhas de informação, estados de multijogador e resoluções compacta, Full HD e ultrawide. A rodada registrou **278 verificações**, com as áreas de interação e os arquivos de arte preservados.

<figure><a href="/assets/captures/hud-hd-offscreen-fullhd.webp" data-lightbox="true"><img src="/assets/captures/hud-hd-offscreen-fullhd.webp" alt="Composição técnica offscreen em 1920 por 1080, com mundo de referência e candidato de HUD HD com vida e mana pela metade."></a><figcaption>Composição técnica offscreen da revisão instalada, com o código e os assets reais. Não é captura de partida; a avaliação artística final permanece em revisão.</figcaption></figure>

A revisão de otimização limita a preparação e o envio das camadas às regiões efetivamente usadas pela interface, incluindo a experiência. A comparação final preservou os pixels dos 15 arquivos de teste em relação à primeira rodada, inclusive no caso GPU com interface lógica compacta apresentada em Full HD. Essa verificação mostra a preservação desses resultados; não é uma medição de FPS sustentado.

**A nova versão do HUD foi instalada no iniciador habitual em 8 de outubro**, após as verificações, com os arquivos do perfil, as configurações, os saves e o launcher preservados. Conceito, prévia Godot, composição técnica e build instalada continuam sendo estados diferentes: a instalação foi confirmada por recibo, enquanto a imagem acima veio do teste offscreen. A avaliação artística final e a conferência em uso real com o responsável permanecem pendentes. O [estudo visual](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/hud-study/README.md) conserva as propostas. A [procedência das imagens deste registro](https://github.com/douglasopan/diablo-3d/blob/main/website/DEVLOG-MEDIA.md) identifica separadamente as artes geradas e a composição técnica.

## Acompanhar, revisar e ampliar

O [manifesto das artes](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/concept-art.json) registra a relação entre conceito e unidade de produção, revisão, arquivo e procedência. Os arquivos gerados ficam na [pasta pública de conceitos](https://github.com/douglasopan/diablo-3d/tree/f0102dc1b0c9075fa98906b3f38d82c09418644b/assets/concept-art/items/r1). O catálogo completo continua muito maior que este lote inicial: as próximas famílias serão tratadas com a mesma identificação, revisão e cuidado com o reuso.

Quem quiser colaborar pode ajudar a conferir proporções, legibilidade, materiais e consistência entre vistas. Uma comparação útil identifica o item e a revisão, mostra o problema e explica a mudança sugerida. Veja [como participar](/participar/) ou entre no [Discord do Diablo 3D](https://discord.gg/4YxQ7s69S), com espaços em português e inglês.

As imagens de itens mostram somente artes de conceito geradas. Referências licenciadas locais e sprites extraídos não são distribuídos; a imagem do HUD conserva o contexto da composição técnica e sua identificação. A frente própria de itens dá continuidade ao catálogo e aos próximos lotes, enquanto a integração do HUD e o trabalho de GPU e desempenho seguem com seus responsáveis. Para os nove itens desta rodada, o próximo passo é a revisão dos conceitos, antes da modelagem e da integração no jogo.
