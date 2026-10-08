---
title: "Créditos do projeto e menos trabalho por quadro"
date: 2026-10-08
description: "O menu reúne suporte e créditos de Diablo 3D, enquanto props e árvores passam a reutilizar geometria na GPU, preservando modelos, texturas e a partida."
slug: creditos-e-geometria-gpu
image: /assets/captures/project-credits-main-pt.webp
image_alt: "Menu principal em português com Suporte e Créditos ao Projeto; interface nativa produzida em diagnóstico offscreen, sem iniciar uma partida."
category: Ferramentas
order: 16
status: published
---

O desenvolvimento avançou em duas partes da experiência: reconhecer quem constrói Diablo 3D e diminuir o trabalho repetido para desenhar Tristram. O menu agora organiza suporte e créditos em um mesmo lugar. Na engine, props e árvores passam a reutilizar sua geometria na placa de vídeo nas câmeras de perspectiva.

As duas mudanças foram publicadas no [commit fc9642982](https://github.com/douglasopan/diablo-3d/commit/fc9642982da2b74b37af366f74c75e966aad4578) e instaladas no iniciador habitual em **8 de outubro, às 15:01, horário de Brasília**. Os modelos e as texturas foram preservados. As imagens deste registro mostram a interface nativa em um diagnóstico offscreen: não são capturas de gameplay nem uma nova aprovação artística integral.

## Um lugar para suporte e créditos

O menu principal reúne as entradas anteriores em **Suporte e Créditos ao Projeto**. O submenu conserva **Suporte** e **Exibir Créditos**, acrescenta **Créditos do Diablo 3D** e oferece a opção de voltar. O conteúdo e os créditos dos autores originais permanecem preservados.

<figure><a href="/assets/captures/project-credits-submenu-pt.webp" data-lightbox="true"><img src="/assets/captures/project-credits-submenu-pt.webp" alt="Submenu em português com Suporte, Exibir Créditos, Créditos do Diablo 3D e Voltar, em diagnóstico nativo offscreen." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>Submenu nativo em português, produzido pelo diagnóstico offscreen. A captura foi publicada inteira, sem recorte ou retoque; não representa uma partida em execução.</figcaption></figure>

A nova tela identifica **Autoria e direção: Douglas Pan**, reafirma o objetivo de reconstruir **Diablo 1 inteiro em 3D**, com Tristram como primeira etapa, e convida à colaboração com arte, código, testes e sugestões. GitHub, site e Discord têm links selecionáveis.

<figure><a href="/assets/captures/project-credits-author-pt.webp" data-lightbox="true"><img src="/assets/captures/project-credits-author-pt.webp" alt="Créditos do Diablo 3D em português, com Douglas Pan, objetivo de reconstruir o jogo inteiro e links da comunidade; diagnóstico nativo offscreen." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>Créditos do projeto em português. A interface nativa reconhece Douglas Pan e preserva a autoria, as licenças e os créditos herdados de Diablo e DevilutionX.</figcaption></figure>

O fluxo funciona em português brasileiro e inglês. Ao voltar, a seleção retorna ao mesmo item; navegar entre essas telas não inicia outra faixa de música. O título longo se ajusta à largura disponível, e a cena do menu no Godot acompanha as cinco ações do menu principal.

<figure><a href="/assets/captures/project-credits-author-en.webp" data-lightbox="true"><img src="/assets/captures/project-credits-author-en.webp" alt="Diablo 3D Credits em inglês, com autoria e direção de Douglas Pan e links da comunidade; diagnóstico nativo offscreen." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>A mesma tela em inglês, também produzida no diagnóstico nativo offscreen. Os dois idiomas fazem parte da entrega instalada.</figcaption></figure>

Passaram **1.193 verificações nativas**, com **26 capturas** nos dois idiomas em três resoluções, além de **103 verificações Godot**. Os casos incluem foco, teclado, clique, largura reduzida, links simulados e falha ao abrir o navegador; não abriram endereços reais. Gamepad físico e reprodução sonora não foram testados nesta rodada. O [guia do editor](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/GODOT-EDITOR.md) registra o contrato e os limites.

## A geometria fica na placa de vídeo

Mesmo com a rasterização na GPU, ainda havia trabalho repetido: preparar e enviar as faces dos volumes a cada quadro. O novo caminho guarda a geometria de props e árvores em **buffers imutáveis e indexados residentes na GPU**. A placa passa a reutilizar esses dados, enquanto a câmera muda sua projeção.

O cache geométrico tem um orçamento independente de **256 MiB**. Nas cenas medidas, os quadros aquecidos não precisaram renovar os dados dos caches: zero upload de geometria residente, texels ou LUT. Isso não significa zero envio de toda a cena: os objetos que permanecem no caminho projetado ainda têm seu custo por quadro.

Essa otimização não reduz modelos ou texturas, nem altera luz, colisão ou saves. Também não instala LOD, novos materiais ou masters mais densos. Seu alcance atual é **props e árvores em perspectiva**. Construções importadas, cenário individual e atores continuam usando o caminho projetado anterior.

## Menos tempo para desenhar o mundo

A comparação final isolada usou **1920×1080, Radeon RX 570, suavização desligada e amostragem 1×**. Cada resultado é a mediana de **quatro pares alternados AB/BA**, comparando o caminho GPU projetado com o novo caminho residente na mesma condição.

| Câmera | GPU projetada por quadro | GPU com volumes residentes |
| --- | ---: | ---: |
| Terceira pessoa | 229,183 ms | 77,821 ms |
| Primeira pessoa | 214,149 ms | 68,647 ms |

Os tempos medem **somente o desenho do mundo**, incluindo a leitura de volta da GPU, e excluem simulação, interface e apresentação SDL. **Não são FPS sustentado da partida, nem comprovam uma meta de 60 FPS.** São uma comparação entre dois caminhos GPU nesta rodada; não substituem a [medição histórica de CPU contra GPU](/devlog/renderizacao-gpu-tristram/).

A leitura de volta continua síncrona e tem custo próprio. Mesmo com o ganho total, a mediana desse custo em primeira pessoa aumentou de **9,057 para 15,607 ms**. A preparação das construções também permanece entre os gargalos a investigar.

## Preservar imagem, seleção e estado

Na rodada final Full HD, cores e identificadores centrais foram exatos nas duas câmeras, e a profundidade ficou dentro da tolerância de **0,002**. O mapa completo de sombras, o estado nativo e o gerador de números aleatórios da simulação foram preservados.

Uma rodada anterior conferiu quatro modos de câmera em **960×540**, com suavização desligada e ligada. Um pixel de borda na terceira pessoa sem suavização foi classificado comparando o **mesmo pixel** de uma referência projetada com deslocamento subpixel limitado a **1/256 por eixo**. Cor, identidade e profundidade precisaram corresponder em conjunto; os demais casos tiveram cores e identificadores centrais exatos.

A isométrica residente ficou de fora da integração após uma divergência de recorte que ainda não recebeu classificação. Ela conserva o caminho projetado validado, sem ampliar a tolerância para habilitar o novo caminho. A suíte sintética final passou **1.768 verificações**. A [documentação do renderizador](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/GPU-RENDERER.md) detalha esses testes.

## Instalação e próximos passos

A instalação ocorreu sem partida aberta. Os **40 arquivos do perfil habitual** conservaram hash, tamanho e data; a preparação posterior mudou somente seu recibo esperado. A revisão artística integral continua com o autor.

O próximo passo de desempenho é medir a partida real e investigar os custos restantes de arquitetura, cenário e leitura da GPU. Ampliar o caminho residente exige demonstrar a mesma preservação para cada família antes de habilitá-la. O [guia de execução desta revisão](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/PROJECT-EXECUTION.md) mantém a fila e as dependências.

O objetivo segue sendo todos os níveis de Diablo 1. Tristram é o primeiro ambiente de validação; os demais níveis ainda usam o renderizador original. Quem quiser participar pode conhecer [as formas de colaboração](/participar/) e [de apoio ao desenvolvimento](/apoiar/).
