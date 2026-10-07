---
title: "Duas velas, duas janelas e o mesmo cômodo"
date: 2026-10-07
description: "A cabana opcional recebeu duas velas físicas com oscilação discreta e uma abertura traseira real, preservando o modelo exterior e sua calibração."
slug: velas-janelas-cabana
image: /assets/captures/cabin-fire-tone.webp
image_alt: "Janela frontal: referência original, interior com a fonte anterior e revisão com duas velas, em recortes ampliados sem ajuste de cor."
category: Luz
order: 10
status: published
---

A luz da cabana leste passou a vir de duas velas, cada uma com cera, pavio, suporte e chama com volume. A janela traseira também se abre para o mesmo cômodo, com paredes de pedra, teto e piso de tábuas. Essa revisão substitui a fonte genérica e o fechamento traseiro descritos no [registro anterior](/devlog/interior-cabana-sombras-terreno/).

A mudança foi publicada em 7 de outubro de 2026, às 20:02 no horário de São Paulo, no [commit cdeaaab0d](https://github.com/douglasopan/diablo-3d/commit/cdeaaab0d208bf0da4239b98c287619508e308a5). Ela permanece restrita ao perfil opcional de revisão Meshy; não conclui os interiores de Tristram nem os demais ambientes de Diablo 1.

## Aberturas que atravessam a parede

As duas janelas têm recortes na parede interna, túneis de pedra e divisórias de madeira. Seus limites físicos também governam a passagem da luz. Polígonos de 20 lados acompanham as aberturas, evitando que um retângulo invisível deixe raios atravessarem os cantos opacos da alvenaria.

A abertura frontal foi medida no modelo importado. A traseira é uma inferência artística explicitamente aprovada pelo responsável pelo projeto: Diablo não oferece uma vista original das costas que comprove esse desenho. A revisão abre a parede interna atrás do vão existente no candidato, preservando sua borda de pedra. Essa distinção entre referência e decisão de desenho faz parte do novo [padrão de aberturas e iluminação de fogo](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/BUILDING-OPENINGS.md).

![Janela traseira com as fontes de fogo desligadas e ligadas, na mesma câmera.](/assets/captures/cabin-rear-fire.webp)

*A luz revela o interior através da abertura física. A porta continua fechada, e a colisão original mantém o jogador do lado de fora.*

## Fogo com uma variação pequena

As fontes usam RGB linear `(1, 0.665, 0.094)`, raio de quatro unidades e intensidades distintas. Cada vela varia suavemente em até ±6%, com uma fase própria. Essa oscilação usa o tempo visual e uma identidade estável por fonte, sem consumir o acaso da partida.

A chama separa um corpo âmbar, um núcleo pequeno e uma ponta mais escura. Sua emissão é calculada separadamente da luz que chega às superfícies. Isso reduz a área amarela muito clara do emissor anterior. Um pequeno realce claro ainda pode aparecer depois da conversão para a paleta; o resultado não representa uma correspondência perfeita com os pixels originais.

Os materiais recebem a iluminação antes da conversão final de cor. Caches limitados evitam repetir buscas de paleta em cada pixel. A [referência de iluminação](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/TRISTRAM-LIGHTING.md) registra os parâmetros e seus limites.

## Conferir frente, costas e tempo

A comparação real liga e desliga as fontes e suas emissões, com câmera e tempo iguais. Na frente, 112 pixels mudaram, dos quais 111 foram classificados como aquecidos. Atrás, os 73 pixels alterados foram aquecidos. Nenhum pixel pertencente a outros objetos mudou nesses testes.

O teste temporal congela a cena em 0, 3 e 17 segundos. A soma das diferenças contra o instante zero registra 19 pixels alterados depois da conversão para a paleta, novamente sem mudanças fora da cabana. São resultados dessa cena de teste, não medidas de qualidade para todos os modelos.

![Cabana antes e depois da revisão em giros de menos cinco, zero e mais cinco graus.](/assets/captures/cabin-fire-orbits.webp)

As verificações matemáticas de luz e as cenas GOG, shareware e Meshy passaram. Os 5.783 triângulos exteriores, suas UVs e o arquivo convertido do modelo foram preservados. Quatro regiões externas de telhado e paredes mantiveram exatamente seus pixels, e o perfil comum de iluminação exterior não mudou.

## Um padrão para continuar

![Comparação antes e depois em quatro ângulos, mostrando frente e traseira da cabana.](/assets/captures/cabin-fire-angles.webp)

A posição da janela e os materiais ainda diferem da referência. A pequena abertura limita a leitura das tábuas na câmera normal. Divisórias e objetos internos ainda não lançam sombras individuais dessas fontes pontuais, e o sistema de paredes não cobre aberturas arbitrárias em telhados inclinados.

O padrão começa nesta cabana e deverá ser validado a partir das referências de cada construção. O objetivo continua sendo Diablo 1 inteiro em 3D; Tristram é a primeira etapa, seguida pelos níveis procedurais e demais personagens, monstros e efeitos. O [roadmap](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/ROADMAP.md) descreve essa sequência sem prometer uma data de conclusão.
