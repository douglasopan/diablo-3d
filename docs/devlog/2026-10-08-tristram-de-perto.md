---
title: "Tristram de perto: novas câmeras, qualidade e desempenho"
date: 2026-10-08
description: "Quatro câmeras, revisão dos modelos, correções na GPU e uma comunidade bilíngue: os avanços de hoje e os desafios que a primeira pessoa tornou visíveis."
slug: tristram-de-perto
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Comparação técnica de Tristram nos modos isométrico, órbita livre, terceira e primeira pessoa, com CPU acima e GPU abaixo."
category: Protótipo
order: 13
status: published
---

Olhar Tristram do chão muda a maneira de avaliar o projeto. Uma casa que funciona à distância pode revelar uma parede deformada de perto; a textura que parecia suficiente na câmera original passa a mostrar repetição. As novas câmeras abriram essa possibilidade e ajudaram a orientar o trabalho de hoje: preservar os modelos, entender o custo de desenhá-los e melhorar a experiência de jogar e colaborar.

**O objetivo continua sendo Diablo 1 inteiro em 3D, com todos os níveis.** Tristram é o primeiro ambiente de validação. A campanha, as regras, a colisão e os saves continuam no DevilutionX; os demais níveis ainda usam a renderização original.

## Quatro maneiras de olhar a mesma cidade

Isométrico, órbita livre, terceira pessoa e primeira pessoa já foram integrados e instalados no protótipo local, junto das preferências de campo de visão, sensibilidade e horizonte. A entrega anterior está no [commit 38a280881](https://github.com/douglasopan/diablo-3d/commit/38a280881938c43e029227e678d28e1457d8b507). A imagem de abertura reúne capturas técnicas da mesma rodada, com CPU na linha superior e GPU na inferior; não é um vídeo nem uma medição de fluidez.

O horizonte cobre o fundo para além da cidade, mas ainda é provisório. A altura dos olhos precisa de calibração e a câmera ainda não possui colisão com paredes. Esses limites ficam especialmente evidentes ao caminhar perto das construções.

O novo atalho remapeável, **K**, agora percorre os quatro modos na instalação habitual. **F4** conserva a alternância da visualização, e **Home** continua abrindo a referência do renderizador original. Voltar à referência ajuda a comparar o conjunto, mas não aprova automaticamente as malhas.

## Por que a primeira pessoa ficou lenta

O [primeiro piloto GPU](/devlog/renderizacao-gpu-tristram/) já havia trazido uma melhora percebida na partida. A primeira pessoa em Full HD expôs outro problema: o cache podia ultrapassar seu orçamento e devolver o desenho à CPU.

A investigação encontrou uma tabela global de iluminação repetida para cada textura. No caso controlado com nove texturas, essa repetição ocupava **288 MiB**; compartilhar a mesma tabela reduziu essa parcela para **32 MiB**, preservando as malhas e as texturas. A correção passou pela cena real em 1920×1080 e campo de visão de 80°, sem retorno à CPU e com cor, profundidade, seleção e sombras preservadas na comparação.

Essa correção já foi instalada no iniciador habitual em 8 de outubro, junto do descarte espacial e da revisão dos menus, na [revisão ae43f0470](https://github.com/douglasopan/diablo-3d/commit/ae43f0470134af6bb470c68d20fa37647c8a0ec4). Ela resolve a falha de cache reproduzida, mas não encerra o problema de desempenho: preparar a cena na CPU continua caro. Um perfil anterior desta rodada encontrou cerca de **173 ms** nessa preparação, dos quais aproximadamente **111 ms** eram gastos com os elementos menores da cena. São medições de desenvolvimento, não o FPS da partida.

## Trabalhar menos sem apagar o cenário

O descarte antecipado de construções fora do campo de visão também foi entregue. No cenário Full HD examinado, as visitas à geometria da arquitetura caíram de **82.626 para 54.579**, mantendo exatamente os resultados de cor, profundidade, seleção e sombras entre descarte ligado e desligado no mesmo backend. Objetos fora da tela que projetam sombras sobre a área visível continuam considerados. A ordem de percurso do chão também foi reorganizada para evitar ordenar toda a grade a cada quadro.

Os bytes instalados passaram por **16 casos** de quatro câmeras, CPU/GPU e suavização solicitada em 960×540, além da primeira pessoa Full HD. Nos quadros GPU aquecidos não houve novos uploads de texturas nem da tabela de iluminação. Os testes confirmam essas condições finitas; a fluidez sustentada ainda exige trabalho e medição na partida.

Em paralelo, um backend experimental mantém malhas indexadas na GPU entre quadros. A comparação isolada de uma grade de 32.768 triângulos passou e, nesse teste específico, o tempo mediano caiu de **6,80 para 1,93 ms**. O adaptador para a cena do jogo ainda não está pronto; esse ganho não pode ser apresentado como desempenho já entregue ao jogador.

A revisão independente na nuvem reforçou essa direção: reduzir também projeção, recorte e montagem na CPU, além de conservar geometria na GPU. Ela examinou o código público da revisão `89e767d1` e os contratos entre as frentes, sem acesso à placa de vídeo ou aos arquivos privados locais e sem acrescentar uma implementação concorrente.

## Preservar o modelo antes de reduzir detalhes

A aproximação da câmera também tornou mais urgente a revisão da conversão dos modelos. O master de Adria, com **3.491.844 triângulos**, foi preservado, enquanto o resultado texturizado possui **5.134 triângulos**. A transferência de materiais para a malha densa já permite uma comparação visual, mas ainda mostra remendos e compressões. Esse resultado é um candidato de autoria, sem aprovação artística e sem instalação no jogo.

![Master geométrico de Adria à esquerda e resultado texturizado reduzido à direita, na ferramenta de revisão Godot.](/assets/captures/adria-master-textured-comparison.webp)

*Antes da transferência: o master conserva os volumes, mas não tem UV ou textura. O resultado reduzido à direita traz os materiais, com perda visível de formas. A imagem é uma prévia de autoria no Godot.*

![Transferência experimental do material para a geometria densa à esquerda, comparada ao modelo reduzido à direita.](/assets/captures/adria-material-transfer-review.webp)

*Na transferência experimental, os volumes reaparecem à esquerda. Remendos, compressões de UV e variações de material ainda exigem revisão. Não é um modelo aprovado nem uma captura do jogo.*

O novo formato experimental de modelos conserva índices, normais, coordenadas de textura e informações de materiais PBR, com identidade e ancoragem explícitas. Um seletor de níveis de detalhe considera o tamanho e o erro projetados na tela; a histerese evita trocar de versão continuamente perto de um limite. A proposta é usar derivados identificados quando a distância permitir, conservando o master e a qualidade próxima da câmera.

O formato, o seletor e a revisão no Godot passaram por seus testes isolados. O runtime atual ainda não carrega esse formato nem usa o novo seletor. A revisão visual precisa decidir quais derivados são aceitáveis antes da integração.

## Conferir cada edifício e sua origem

O inspetor comunitário passou a organizar **130 arquivos GLB únicos**, suas origens e a relação com os **12 vínculos** do pacote aplicado, além da cabana leste. Ele ajuda a comparar o arquivo fonte com o que o jogo efetivamente monta, sem confundir o catálogo mais amplo com a seleção instalada. O [editor externo Godot](/devlog/editor-godot-arquitetura/) continua auxiliando esse trabalho; o jogo permanece no DevilutionX.

A Catedral recebeu uma correção local importante: o derivado agora usa escala uniforme e ancora a soleira na entrada nativa. Os 16.854 triângulos, as coordenadas de textura e o atlas foram preservados; o GLB original permaneceu intacto. Isso remove o achatamento provocado por escalas diferentes em cada eixo. A proporção entre torre e nave ainda não coincide exatamente com a referência, portanto a correção não representa aceitação integral do edifício.

Orientação, contato com o terreno, aberturas e luz interna seguem como problemas separados na revisão das casas. Um modelo selecionado para comparação não recebe automaticamente o estado de aprovado.

## Um chão pensado também para quem olha de perto

O aspecto quadriculado do terreno em primeira pessoa abriu uma nova investigação. Estão sendo comparados dois caminhos: peças com materiais de maior definição e uma superfície visual contínua, dividida em blocos, com texturas ancoradas no mundo e transições entre materiais.

Seis materiais candidatos foram gerados para revisão, tomando como direção a grama escura, a terra castanha com tom arroxeado e as margens frias das referências. Os masters isolados ainda não comprovam repetição, emendas ou aparência dentro do jogo. Grama seca, barro e substrato de margem também são interpretações, sem confirmação de que correspondam a famílias distintas do terreno original.

A grade nativa continua responsável por caminhos, colisão e seleção. Água, margens não classificadas e peças desconhecidas permanecem preservadas no estudo. Nenhum desses materiais foi aplicado automaticamente à instalação habitual.

O piloto gerou **30 imagens próximas e isométricas** e passou por **13 verificações**, preservando máscaras, opacidade e regiões sem substituição. A comparação recomenda blocos visuais com textura ancorada no mundo, mas ainda conserva recortes nas transições nativas. É um estudo fora do runtime, que ainda precisa de um pequeno teste dentro do jogo antes de qualquer aplicação.

## Menus, HUD e música fazem parte da experiência

O HUD atual recebeu uma correção funcional de composição com a arte nativa, mantendo vida, mana, cinto, magia preparada e os controles existentes, com layout também revisável no Godot. **O novo HUD visual solicitado pelo autor ainda não está concluído.** O feedback mais recente reabriu o acabamento da tela principal, com foco em qualidade visual, legibilidade e adaptação à resolução. Os testes da composição anterior não comprovam a conclusão dessa nova interface. As funções do jogo serão preservadas enquanto esse trabalho continua.

A trilha sonora já permite **Vanilla**, **Rock** ou **Custom**, com escolhas por ambiente. Tristram oferece três arquivos locais e sorteia novamente ao fim de cada faixa quando o modo aleatório está selecionado. Ambientes sem substituição conservam a música original.

A [biblioteca de músicas](/musica/) reúne cinco arquivos personalizados do menu e de Tristram para **ouvir e baixar gratuitamente**, com **Douglas Pan** nos créditos da página e nos metadados internos dos MP3s. A terceira opção de Tristram aparenta ser outra exportação da primeira; os arquivos disponíveis não são contados como composições distintas. As músicas originais do jogo não são distribuídas nessa biblioteca.

A revisão mais recente dos menus já está instalada. Ela retira da partida as opções que só funcionam no menu principal, aumenta a quantidade de linhas conforme o espaço disponível e usa fontes nativas maiores. Anterior, Próxima e Voltar passam a dividir um rodapé. A seleção é conservada ao redimensionar a janela. A suíte de configurações passou por **59.452 verificações**, acompanhada de capturas nativas em duas resoluções; a avaliação visual na partida continua com o autor.

## Uma comunidade em português e inglês

O Discord recebeu uma área em inglês com salas para conversa, ajuda, revisão de modelos, contribuições de assets, bugs e desempenho, além de ideias e modding. As salas em português, as permissões existentes e o lobby de voz foram preservados. Há canais de devlog para os dois idiomas, e o site continua publicando a tradução completa dos registros.

Essa estrutura ajuda quem quiser colaborar a encontrar uma tarefa concreta: comparar proporções e materiais, revisar um modelo em vários ângulos, registrar um problema de câmera ou testar o desempenho no próprio computador. A revisão mantém separados os estados de candidato, aprovação parcial e aceitação integral.

Quem quiser acompanhar por e-mail pode [deixar seu contato voluntariamente](/novidades/), escolhendo português ou inglês. As respostas são privadas e há um formulário para solicitar cancelamento. O cadastro não é exigido para acessar o site ou baixar músicas.

O projeto também precisa de apoio para continuar. Quem puder ajudar com desenvolvimento, arte, testes, recursos financeiros ou créditos de geração pode conhecer as formas de participação em [Participar](/participar/) e [Apoiar](/apoiar/), ou entrar no [Discord do Diablo 3D](https://discord.gg/4YxQ7s69S). Meshy e outros geradores 3D, além de créditos para ChatGPT e Claude, fazem parte das necessidades informadas pelo responsável.

## O que vem depois desta rodada

As câmeras e as ferramentas de revisão permitem enxergar melhor os defeitos; a próxima entrega precisa corrigir esses defeitos e reduzir o custo de desenhar a cena sem sacrificar os modelos. Cache, descarte espacial e menus já foram instalados. Malhas persistentes na GPU, LOD, novo HUD e materiais continuam em desenvolvimento.

Tristram ainda exige revisão visual completa. Depois desse marco vêm o primeiro andar procedural da Catedral e os demais ambientes, personagens, monstros, objetos e efeitos de Diablo 1. A nova perspectiva amplia o que podemos explorar, mantendo o compromisso com o jogo inteiro.

As fontes públicas desta rodada estão fixadas na revisão integrada: [GPU](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/GPU-RENDERER.md), [câmeras](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/TRISTRAM-HORIZON-CAMERAS.md), [inspetor de modelos](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/MODEL-INSPECTOR.md), [configurações](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/INGAME-SETTINGS.md) e [comunidade](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/DISCORD-COMMUNITY.md).
