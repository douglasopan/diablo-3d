---
title: "Catedral: materiais nativos e transparência mais leve"
date: 2026-10-09
description: "O piloto do primeiro andar recebe piso, alvenaria e madeira do original, com aproximações visuais explícitas. A cópia regional reduz o custo medido da transparência; formas, portas e gameplay ainda precisam de revisão."
slug: catedral-materiais-transparencia
image: /assets/captures/cathedral-materials-after.png
image_alt: "Teste técnico offscreen do piloto da Catedral com piso e alvenaria usando materiais nativos aproximados, porta fechada e atores em sprites; não é gameplay ou arte final."
category: Geometria
order: 22
status: published
---

O [primeiro piloto da Catedral](/devlog/catedral-primeiro-andar-piloto/) usava piso quadriculado e paredes em cores simples. Agora o recorte recebe **materiais do original**, enquanto a transparência passa a copiar somente a região necessária de cada triângulo. A mudança aproxima a aparência e reduz o custo do renderizador nos casos medidos.

A entrega do [commit a783aff4](https://github.com/douglasopan/diablo-3d/commit/a783aff47ff66fc9abb45d0345c604b444957087) foi instalada em **9 de outubro de 2026, às 17:34, horário de Brasília**, pelo mesmo **Iniciar-Tristram.cmd**. O escopo continua em **nove regiões próximas do jogador, somente no primeiro andar normal da Catedral**. Demais andares e níveis de missão mantêm a apresentação nativa; os atores deste piloto continuam em sprites.

## Materiais originais, formas ainda provisórias

O chão usa os dados **CEL/MIN** já preparados pela engine: bitmap de **64×32** e uma máscara de cobertura independente da cor. O alinhamento visual foi corrigido sem substituir a malha física ou os vínculos nativos de seleção. A alvenaria usa faixas orientadas pela face e pela fonte SOL; a madeira vem de um pequeno trecho do **CLX** da porta.

Repetições, materiais doadores, tampas e faces sem referência são **aproximações**. As formas, alturas, arcos, escadas e detalhes não estão finalizados. Usar materiais nativos não constitui aprovação artística integral nem significa que todo o andar foi reconstruído.

O par abaixo mostra o mesmo enquadramento técnico próximo, sem suavização: antes, com cores simples; depois, com materiais e a cópia regional. São execuções **offscreen, com cenário parado e inicialização parcial**, sem HUD ou sessão física de gameplay. O piso visual muda deliberadamente; o antes/depois não é uma alegação de igualdade de pixels.

<figure><a href="/assets/captures/cathedral-materials-before.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-before.png" alt="Antes: teste técnico offscreen da Catedral no enquadramento próximo sem AA, com piso quadriculado, paredes em cores simples, porta e atores nativos em sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Antes — baseline técnica próxima, sem AA, com materiais provisórios. Cenário parado e inicialização parcial; não é uma captura de gameplay.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-materials-after.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-after.png" alt="Depois: o mesmo enquadramento técnico próximo sem AA, com piso e alvenaria de materiais nativos aproximados, porta fechada e atores em sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Depois — materiais nativos aproximados e cópia regional na mesma pose técnica. O piso visual foi corrigido; formas e detalhes continuam provisórios.</figcaption></figure>

A comparação seguinte força a geometria na perspectiva original, ao lado da referência nativa. Ela torna visíveis as diferenças de cobertura, paredes e arcos que ainda precisam de revisão. **Home** continua usando o desenho original; esse retorno, por si só, não valida a fidelidade da reconstrução.

<figure><a href="/assets/captures/cathedral-materials-original-perspective.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-original-perspective.png" alt="Perspectiva original com a geometria do piloto forçada: piso nativo aproximado e paredes planas, com formas e detalhes ainda diferentes da referência." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Geometria forçada — teste offscreen na perspectiva original. Os materiais aproximam o recorte; as formas e os detalhes ainda divergem da referência.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-pilot-native-reference.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-native-reference.png" alt="Referência nativa do estado técnico da Catedral, com piso, paredes e arcos originais; captura offscreen sem HUD ou sessão física de gameplay." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Referência nativa — o PNG é idêntico ao já publicado e foi reutilizado. Serve à comparação técnica; não representa cada falha intermediária nem aprovação artística do piloto.</figcaption></figure>

Os três PNGs novos conservam os bytes e a resolução original de **640×480**, sem conversão, recorte ou retoque. A referência existente também permanece intacta.

## Transparência com menos trabalho por quadro

Antes, cada triângulo transparente guardava uma cópia da tela inteira. Agora o backend copia uma área conservadora ao redor do triângulo, preservando ordem, cores, profundidade, seleção e limites de admissão. O A/B isolado manteve **nove buffers completos** iguais; o A/B do backend na cena nativa conservou **12 PNGs byte-exatos**. Essa igualdade cobre a otimização isolada: a adição posterior dos materiais muda a imagem deliberadamente.

A medição usou uma **Radeon RX 570 real**, seis combinações de pose e qualidade, cinco aquecimentos e vinte amostras por caso, com **156 chamadas do renderizador por fase**. A resolução lógica foi **640×480**; AA2x desenha em **1280×960**. Dois recortes das distribuições mornas:

| Caso | Antes: mediana / p95 | Materiais + cópia regional: mediana / p95 |
| --- | --- | --- |
| Perto, sem AA | 13,42 / 13,76 ms | 10,27 / 11,29 ms |
| Longe, AA2x | 77,73 / 79,05 ms | 65,86 / 68,64 ms |

São tempos de parede do **renderizador, incluindo espera da GPU e readback**. Excluem gameplay, HUD, apresentação e preparação nativa externa; não são FPS sustentado nem promessa de 60 FPS. Os quadros frios de criação de recursos ficam separados das amostras mornas. Os demais casos e a fase intermediária estão no [registro versionado da GPU](https://github.com/douglasopan/diablo-3d/blob/a783aff47ff66fc9abb45d0345c604b444957087/docs/GPU-RENDERER.md).

No caso distante com AA2x, o payload dos snapshots caiu de **253.132.800 para 820.803 bytes por quadro**. É o volume das regiões copiadas, não uma medição do tráfego total do barramento. A GPU permaneceu ativa em todas as amostras, sem rasterização 3D pela CPU ou uploads mornos de texels/LUT. Comandos e readback continuam custos relevantes.

## O que foi instalado e o que falta testar

O gate de materiais passou **168.022 verificações sobre 81.152 texels**, comparados aos rasterizadores nativos. Foram oito quadros com geometria forçada e sete PNGs técnicos. O cache do recorte tem **72 entradas e 162.304 bytes** de pixels e cobertura; quatro redesenhos mornos não fizeram novas decodificações. Seus limites e a ausência de descarte automático ainda precisam de trabalho antes de ampliar o andar.

A recuperação foi conferida separadamente no mesmo build: **157 verificações do harness, 188 contextuais e 93 de alocação**, com quatro falhas e recuperações imediatas na GPU. Ogden e Tristram passaram **1.348 verificações**, mantendo vinte PNGs byte-exatos. O witness nativo de **290.243 bytes** permaneceu igual, com mapa, SOL, RNG e transparência preservados. A fixture não tinha uma porta aberta: **sua operação não foi testada**.

Os três executáveis instalados têm os mesmos **6.559.744 bytes**, SHA-256 **929a78f6c15d7e642225c11bc035462c5d93009a87786165eddf9d48746be652**. Os **48 arquivos originais do perfil** conservaram conteúdo, tamanho e data; somente o recibo derivado do launcher foi renovado. Não houve redução ou troca de masters, seleções e texturas de Tristram, instalação de payload, encerramento de processos ou nova operação paga.

O próximo passo é testar entrada, movimento, combate, transições e portas na partida, revisar formas e detalhes e medir comandos, readback e cache antes de ampliar os níveis. Capturas técnicas e tempos do renderizador não substituem esses testes. O [estado versionado da integração](https://github.com/douglasopan/diablo-3d/blob/a783aff47ff66fc9abb45d0345c604b444957087/docs/PROJECT-EXECUTION.md) e o [roadmap](/roadmap/) mantêm o piloto separado da meta de reconstruir **Diablo 1 inteiro em 3D**.
