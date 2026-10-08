# Entrega para o devlog — demais interfaces R2

Data: 8 de outubro de 2026. Estado: **estudo candidato**, sem instalação no jogo.

O usuário pediu estudar todas as demais telas com geração de imagem, comparar com o original e preparar peças que depois possam virar interface editável. A entrega amplia a direção já escolhida para menus, personagens, configurações, inventário, atributos, magias, missões, baú, automapa, NPCs, comércio, chat, controles, diálogos e transições. As condições Hellfire, rede, toque e debug ficam identificadas; não representam recursos disponíveis em toda build.

- 65 arquétipos atuais rastreiam 223 exemplos/estados anteriores.
- 63 arquétipos que precisam de proposta estão cobertos. O HUD instalado e os créditos permanecem preservados.
- 60 novas composições de conceito, algumas como pranchas de variantes, mais três peças sem texto. O menu principal reutiliza a direção anterior.
- 14 referências nativas em imagem: uma captura histórica da janela e 13 fixtures offscreen. Faltam capturas de 51 arquétipos, que exibem referência de código. Uma captura-base não prova todos os estados da mesma tela.
- Três protótipos Godot separados: configurações, inventário e comércio. Passaram 819 verificações de estrutura, geometria e sinais; render e revisão visual no editor estão pendentes. Não executam transações nem alteram a simulação.
- 60 WebPs de conceito e 14 referências completas em WebP sem perdas: 74/74 comparações de RGBA decodificado exatamente iguais. Sem resize, recorte ou retoque; masters preservados localmente.

## Material para publicação

[index.html](index.html) é a galeria navegável. [catalog.json](catalog.json) registra imagens selecionadas, prompts, dimensões efetivas, SHA-256, telas e estados. [publication-media.json](publication-media.json) é o recibo de conversão de formato. Os conceitos ficam em `previews/`, as capturas em `references/` e as três peças com transparência em `assets/`. A galeria funciona também por arquivo local, sem uma API privada.

O conjunto de mídia lossless tem cerca de 113 MB. Carregar imagens sob demanda e copiar de `docs/hud-study/r2/` para o artefato do site durante o build evita duplicar esses bytes em `website/public/` no Git. Publicar todas as propostas selecionadas em uma galeria organizada por família, com legenda de conceito e vínculo para a comparação. Não publicar revisões descartadas só por existirem na pasta local.

## Legendas e limites

Uma imagem gerada nunca representa a captura original. Exemplos de texto, atributos, personagens, itens e mapas são ilustrativos. Não anunciar as novas telas como funcionais ou instaladas. Não anunciar Full HD universal: as dimensões recebidas pelo gerador estão no catálogo.

As grades raster de baú P08 e comércio c-05 ainda divergem do contrato nativo e não podem ser promovidas diretamente. A montagem Godot já usa mochila 10×4, sete receptáculos e loja 10×9 com nós determinísticos. O baú futuro precisa preservar 10×10. O ornamento central das molduras NinePatch ainda merece ajuste quando mudar a proporção.

Os PNGs locais de conceito não entram no pacote público, pois o WebP preserva seus pixels. Não incluir MPQ, sprites isolados extraídos, saves, GLBs privados, credenciais ou caminhos da conta local. A próxima etapa é revisar as famílias, completar as capturas nativas e então integrar componentes em lotes, preservando os handlers do jogo.
