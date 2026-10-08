---
title: "Todas as telas, uma mesma atmosfera: o estudo de interfaces R2"
date: 2026-10-08
description: "Sessenta conceitos novos, três peças de interface e protótipos editáveis para dar continuidade visual às telas de Diablo 3D, com referências e limites identificados."
slug: interfaces-diablo-r2
image: /assets/studies/hud-r2/previews/p03-combined.webp
image_alt: "Conceito candidato de personagem e inventário para o estudo R2; ilustração em revisão, não captura do jogo."
category: Protótipo
order: 15
status: published
---

Abrir o inventário, conversar com um morador ou ajustar o áudio também faz parte da atmosfera de Diablo. O estudo R2 amplia a direção visual do projeto para essas situações, com propostas organizadas por tela, referências disponíveis e três exemplos editáveis no Godot. As imagens apresentadas aqui são conceitos candidatos; os novos painéis ainda não foram integrados ao jogo.

## Uma direção para o jogo inteiro

O objetivo continua sendo reconstruir **Diablo 1 inteiro em 3D**. Tristram é a primeira etapa de validação, enquanto o estudo de interfaces considera as telas e os estados que acompanham a experiência mais ampla: entrada, personagens, equipamentos, magias, missões, diálogos, comércio e configurações.

A direção procura manter pedra escura, ferro envelhecido, texto em marfim e destaques em âmbar. Molduras e ornamentos precisam dividir espaço com nomes, valores e descrições legíveis. O fundo deve sustentar essa atmosfera de maneira consistente, sem disputar atenção com a informação que o jogador está procurando.

O menu principal reutiliza a direção anterior. O **HUD principal já instalado e os créditos permanecem preservados**; este lote trata das demais interfaces. A [documentação do estudo](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/README.md) explica a organização e os próximos passos.

## 65 arquétipos, 223 estados

O catálogo reúne **65 arquétipos e 223 estados rastreados**. Um arquétipo é uma estrutura compartilhada: uma lista de opções pode ter diferentes conteúdos e condições sem precisar de um desenho independente para cada combinação. Esses números não representam 223 menus distintos.

As propostas cobrem os 63 arquétipos que precisam de arte. Foram produzidas **60 composições novas**: 18 de entrada e navegação, 12 de configurações, 21 de painéis e nove de comércio. Algumas pranchas apresentam várias variantes. Com três peças sem texto e o menu reutilizado, o catálogo reúne 64 recursos visuais.

Entre os grupos estão seleção de heróis e sessões, ajustes de áudio e gráficos, teclas e controles, personagem e inventário, baú e ouro, livro de magias, mapas, missões, conversas, lojas e avisos. Cada proposta está relacionada às ações e condições registradas no [catálogo](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/catalog.json).

O levantamento inclui estados condicionais de Hellfire, rede, shareware, controle, toque e ferramentas de desenvolvimento. Sua presença no catálogo não significa que todos estejam disponíveis na build atual.

## Conceito e referência têm papéis diferentes

Uma ilustração ajuda a avaliar composição, materiais e hierarquia. A referência nativa permite conferir o que a interface realmente precisa mostrar e fazer. A galeria identifica os dois papéis para que uma proposta gerada não seja confundida com o original.

Há **14 referências nativas em imagem**: uma captura histórica de janela e 13 composições técnicas produzidas fora da janela da partida. Algumas já mostram fundos modificados pelo projeto; não são todas reproduções intactas do Diablo original. Uma referência também pode cobrir apenas parte das variantes de um arquétipo.

Os outros **51 arquétipos ainda precisam de captura nativa**. Nesses casos, a comparação informa a lacuna e aponta para as funções existentes no código. A comparação visual integral permanece pendente.

Para publicação, 60 conceitos e 14 referências foram convertidos para WebP sem perdas. O [recibo das 74 conversões](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/publication-media.json) registra igualdade dos pixels RGBA e das dimensões. Não houve recorte, redimensionamento ou retoque; os masters originais permaneceram intactos.

## Do desenho a controles editáveis

Três exemplos passaram da composição ilustrada para cenas separadas no Godot: **configurações, inventário e comércio**. As molduras usam os três kits sem texto; nomes, valores, botões e células são elementos editáveis. Assim, a arte pode orientar a apresentação enquanto o conteúdo permanece separado.

O inventário conserva **sete receptáculos de equipamento e mochila de 10×4 células**. O exemplo de comércio usa **loja de 10×9 células**, além da mochila. Configurações apresenta 18 linhas e um rodapé horizontal. Os itens geométricos e os atributos exibidos são exemplos locais.

O [recibo dos protótipos](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/Godot-HANDOFF.json) registra **819 verificações aprovadas** de estrutura, geometria e acionamentos locais. Esses testes não produziram renders ou capturas do Godot, nem validaram input físico ou aparência final. Comprar apenas abre uma confirmação demonstrativa; confirmar ou cancelar fecha a prévia, sem transação.

Salvar essas cenas não altera a partida, as configurações ou os saves. A integração futura ainda precisa ligar cada controle aos comportamentos nativos.

## O que ainda precisa de correção

Duas pranchas deixam claro por que uma imagem não pode determinar sozinha as regras da interface. **P08** desenha aproximadamente um baú de 10×9 e uma mochila de 10×3; os contratos corretos são **10×10 e 10×4**. A prancha **c-05** apresenta uma loja de **11×8**, enquanto o protótipo Godot já usa os **10×9 corretos**. Essas grades ilustradas estão bloqueadas para promoção direta.

Letras, números, itens e exemplos de chat também precisam virar conteúdo real. Os ornamentos centrais das molduras exigem revisão ao mudar de proporção, e a leitura deve ser conferida em diferentes tamanhos e idiomas. A validação estrutural não substitui essa avaliação visual.

O próximo passo é revisar cada família, completar as referências faltantes e ajustar os componentes antes de integrar uma família por vez. A cobertura de conceitos está organizada; a aprovação artística e a comparação completa continuam em aberto.

Em outra frente do projeto, Griswold e Ogden aparecem nos dois vídeos públicos abaixo, em uma volta rápida de 360°. São modelos em desenvolvimento: a apresentação não é gameplay e não comprova integração ao jogo ou aprovação artística.

<div data-model-showcase="public"></div>

## Galeria completa do estudo R2

A galeria reúne os 64 recursos visuais e as 14 referências disponíveis, com suas identificações e limites. Observe a disposição dos controles, a leitura dos textos e a continuidade dos materiais. Nas propostas de baú e loja, considere também as divergências de grade descritas acima: elas fazem parte da revisão pendente.

<div data-study-gallery="hud-r2"></div>
