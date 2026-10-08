# Diablo 3D — estudo das demais telas, R2

Abra [a galeria](index.html) para navegar pelas famílias, ver as propostas geradas e comparar com as capturas nativas disponíveis. O catálogo rastreia os 223 exemplos anteriores em 65 arquétipos atuais. Os 63 que precisam de proposta estão cobertos por **60 novas imagens de conceito**, algumas em pranchas de variantes; o menu principal reutiliza a direção anterior. Há também **três peças sem texto** para montagem. A arte R2 é candidata: não foi instalada no executável. O HUD principal já instalado e os créditos ficam preservados.

## Referência original e comparação

Há 14 referências de imagem nativa no inventário inicial: uma captura histórica de janela e 13 fixtures offscreen. As outras 51 telas têm referência de código, mas ainda precisam de captura nativa. A galeria identifica isso e verifica se a imagem existente cobre a variante escolhida. Fundos D3D já modificados aparecem em algumas referências; não são apresentados como Diablo vanilla intacto. Imagens de IA e exemplos HTML anteriores nunca representam o original.

## Imagens e peças

`frontend-art.json`, `settings-art.json`, `panels-art.json` e `commerce-art.json` preservam os prompts e o vínculo de cada proposta com telas/estados. `catalog.json` reúne dimensões reais, SHA-256, origem e funções. Toda geração usa a ferramenta **image_gen integrada**, sem API de serviço 3D. Os pedidos de resolução são alvos; o catálogo registra a resolução efetivamente recebida, sem afirmar upscale inexistente.

Os PNGs originais de `images/` são masters locais preservados. As cópias públicas em `previews/` usam WebP **sem perdas**, sem redimensionar, retocar ou recortar; `publication-media.json` registra identidade e comparação dos pixels decodificados. As referências nativas completas ficam em `references/`, também sem perdas. As peças de `assets/` são variantes sem texto, feitas pelo próprio gerador, com alfa exterior. A produção deve combinar molduras com texto, valores, slots, foco e input vivos. Não usar uma captura achatada como interface clicável inteira. Uma inconsistência de letra, grade ou função na ilustração precisa ser corrigida antes de promoção; o contrato do código prevalece.

## Editar no Godot

No gerenciador do Godot 4, escolha **Importar** e selecione [project.godot](project.godot) desta pasta. Este projeto de estudo é separado do editor do mapa e do jogo. Abra `scenes/configuracoes.tscn`, `scenes/inventario.tscn` ou `scenes/comercio.tscn`; execute com F6 para experimentar a cena atual. Textos e retângulos são nós editáveis, e os botões navegam entre os três exemplos. As ações de loja são demonstrações locais.

As grades determinísticas mantêm sete receptáculos de equipamento, mochila 10×4 e loja visual 10×9. Salvar essas cenas não altera o executável, as configurações, o mapa, os modelos ou os saves. Veja [Godot-HANDOFF.json](Godot-HANDOFF.json): passaram 819 verificações de estrutura, geometria e sinais. Render e revisão visual no Godot ainda estão pendentes; isso não é validação artística nem integração funcional com a simulação.

## Pontos de revisão

- A prancha P08 ainda desenha o baú com 10×9 células; o contrato nativo é 10×10. A mochila ilustrada também diverge do 10×4 nativo.
- A prancha c-05 ainda desenha a loja com 11×8 células. O protótipo Godot usa os 10×9 corretos; a grade raster não pode virar a grade do jogo.
- Letras, números, itens, traçados de mapa e exemplos de chat são ilustrativos. Devem ser substituídos por dados e texto vivos.
- O ornamento superior central das molduras pode deformar com o NinePatch; separar o ornamento antes de promover esse componente, caso a proporção mude.
- A prancha de opções Hellfire apresenta o modo como contexto. Isso não autoriza trocar a variante do jogo durante uma partida.

## Continuidade

Revisar a arte por família → completar capturas nativas faltantes → separar/ajustar peças → validar layout, localização e áreas clicáveis → integrar uma família por vez com os handlers nativos. As variantes condicionais Hellfire, rede, shareware, controle, toque e debug não indicam que a build atual oferece todos esses modos.

`build_catalog.py` reconstrói o índice com Python e Pillow. Usa o inventário privado quando disponível ou o snapshot público `screen-inventory.json`. Verifica os hashes dos masters locais presentes e das cópias de publicação; o checkout público funciona sem os PNGs locais. Não modifica a produção. O devlog deve chamar os novos desenhos de conceitos e os protótipos Godot de estudo editável, sem anunciar submenus já instalados.

O conjunto público não inclui MPQs, sprites extraídos isolados, saves, credenciais ou modelos privados. As referências são capturas completas de interface com proveniência identificada.
