---
title: "Comparar sem esconder as diferenças"
date: 2026-10-07
description: "A etapa v3 corrigiu chão e limites das casas e separou o retorno ao backend original da comparação real das malhas reconstruídas."
slug: comparar-sem-esconder-diferencas
image: /assets/captures/v3-forced.webp
image_alt: "Comparação da taverna na v3: backend original e malha 3D forçada na mesma perspectiva."
category: Ferramentas
order: 3
---

Comparar a reconstrução com Diablo exige duas imagens do mesmo lugar, com a mesma câmera e o mesmo estado da partida. Também exige saber qual renderer produziu cada uma. Essa distinção se tornou central na etapa v3: o retorno à imagem original e a fidelidade das malhas são verificações diferentes.

Este artigo foi publicado em 7 de outubro de 2026 como retrospectiva. O diagnóstico `diagnostics/v3-gog` tem registro às 08:38 do mesmo dia. A etapa foi consolidada com o protótipo v4 no [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5). Não há um lançamento público independente para cada número dessas rodadas locais.

## A área pintada não é o limite da parede

Uma imagem de uma casa inclui telhado, projeção das laterais, sombras e detalhes próximos. Usar todo esse retângulo como corpo físico da construção pode fazer a geometria ocupar um lugar por onde o jogo permite andar. O personagem continua seguindo a colisão nativa, mas parece atravessar a casa desenhada.

A v3 separou essas responsabilidades. As paredes fechadas foram conferidas contra centros de células caminháveis, posições dos pés dos atores e segmentos de passos permitidos pelo mapa. O objetivo é manter o corpo da casa dentro das áreas bloqueadas, preservando a composição visual da fachada e do telhado.

Oficina, cabana de Adria e entrada da Catedral têm aberturas relevantes. A reconstrução precisa conservar essas passagens. Fechar todas as superfícies de maneira indiscriminada poderia produzir uma malha mecanicamente consistente e um cenário errado para a partida.

## O chão de índice zero existe

Outra correção tratou uma peça de chão que havia sido interpretada como ausência de conteúdo. O índice zero é chão real e caminhável perto da cabana leste. Descartá-lo deixava buracos pretos no terreno.

O diagnóstico passou a verificar que essa região fosse desenhada e continuasse selecionável. A correção não modifica o mapa nem sua colisão: ela reconhece corretamente um dado que já estava presente. É um exemplo de falha pequena na interpretação da fonte que pode criar uma diferença visual grande.

![Exterior da Catedral na v3, com malha forçada na perspectiva nativa.](/assets/captures/v3-low.webp)

*Captura de `diagnostics/v3-gog/calibrated-cathedral.png`. O nome do arquivo identifica uma comparação com a malha forçada, na âncora nativa. A cena mostra o exterior da Catedral em Tristram; ela não representa o primeiro nível procedural concluído.*

## Home e a malha forçada

Na posição restaurada por Home, o protótipo usa o próprio backend original, incluindo sua ordem de sobreposição e regras de seleção. Se os pixels coincidem nessa rota, o retorno ao desenho original está funcionando. Esse resultado não mede a qualidade da geometria reconstruída.

Para medir essa outra coisa, as ferramentas forçam o renderer de malhas no mesmo ângulo nativo. A v3 conferiu a projeção dos centros do chão em 121 pontos e registrou separadamente as imagens do backend original, do retorno por Home e da geometria forçada. A câmera modificada passa a usar também a seleção da rota geométrica.

A comparação posterior da v4 foi explícita: os 24 enquadramentos de Home coincidiram com a referência, enquanto nenhuma das 24 vistas de malhas forçadas coincidiu integralmente. Os resultados estão descritos no [estado do protótipo](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md).

## Usar a diferença para orientar a revisão

Uma porcentagem alta de pixels iguais pode ser dominada por chão e fundo. Um erro numa janela, num telhado ou no corpo de um habitante ocupa uma área menor e ainda pode ser decisivo para reconhecer o objeto. Os relatórios precisam ser lidos junto das capturas e dos recortes relevantes.

A ferramenta compara RGB nas mesmas coordenadas, incluindo preto verdadeiro, sem mover ou redimensionar as imagens para esconder diferenças. Giros pequenos dos dois lados do ângulo original e a volta completa acrescentam outra evidência: revelam superfícies que a comparação frontal não pode julgar sozinha.
