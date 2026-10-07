# Estado do protótipo Tristram v4

Protótipo de uma visualização 3D para o mapa de Tristram, antes da Catedral. Usa a mesma partida do DevilutionX: a tecla F4 alterna entre o desenho original e o novo desenho, sem recarregar o mapa. A quarta versão está disponível pelo iniciador e reconstrói construções, árvores, pedras e personagens com volume. As faces ocultas e alguns detalhes ainda precisam de refinamento visual.

Este documento registra a validação local realizada em 07/10/2026. Capturas, relatórios gerados, modelos experimentais de Meshy, arquivos do jogo e perfis de jogador não são distribuídos neste repositório. Os caminhos de diagnóstico abaixo são saídas locais que precisam ser geradas de novo. Consulte o [roadmap](ROADMAP.md) para trabalho futuro.

## Jogar

Depois de compilar seguindo [BUILDING-D3D.md](BUILDING-D3D.md), abra **Iniciar-Tristram.cmd**, escolha um jogador e entre em Tristram. O jogo começa na visão original. Para usar uma versão recém-compilada, salve, feche o jogo que já estiver aberto e abra novamente pelo iniciador.

| Tecla | Ação |
|---|---|
| F4 | Alternar original / 3D em Tristram |
| Segurar botão do meio e arrastar horizontalmente | Girar a câmera em 360° |
| Segurar botão do meio e arrastar verticalmente | Ajustar a inclinação |
| Roda do mouse | Aproximar / afastar |
| Shift + botão do meio e arrastar | Deslocar o enquadramento |
| Home | Restaurar ângulo, inclinação, distância e enquadramento |
| [ e ] | Girar a câmera 3D |
| Page Up / Page Down | Aproximar / afastar a câmera |
| Clique no chão | Andar com as regras originais |
| Clique em um personagem | Conversar usando a interação original |

O iniciador usa automaticamente a instalação completa em `C:\Program Files (x86)\GOG Galaxy\Games\Diablo`. Ele apenas lê os dados dessa instalação. Se essa pasta não estiver disponível, usa os dados shareware que você colocar na pasta `data`, obtidos separadamente pelo link publicado pelos mantenedores do DevilutionX. Para escolher outra instalação, informe a pasta do seu DIABDAT.MPQ:

```powershell
.\Iniciar-Tristram.ps1 -DataDirectory 'C:\caminho\do\Diablo'
```

As configurações e os jogadores deste protótipo ficam em `perfil-tristram`, dentro desta pasta. A primeira abertura pede um personagem novo para esse perfil. O registro de execução fica em `perfil-tristram/prototipo.log`.

Use os controles do mouse sobre o cenário. Sobre a interface, lojas, diálogos e outras janelas, os comandos conservam as funções do jogo. A câmera acompanha o herói; o deslocamento com Shift fica relativo a ele até pressionar Home.

## Estado visual desta versão

O terreno usa as imagens originais, convertidas da projeção isométrica para superfícies no mundo 3D. A câmera inicial e a tecla Home usam a mesma projeção paralela de Diablo: 32 pixels na horizontal e 16 na vertical para cada passo diagonal. Giro, inclinação, zoom e deslocamento continuam disponíveis. A caminhada interpola a posição entre os quadrados sem o salto ao terminar cada passo.

A extensão visual acompanha a grade nativa completa de 112×112 células, incluindo o terreno externo visto ao afastar a câmera. O primeiro tipo de chão, de índice zero, também é desenhado e selecionável; descartá-lo como uma célula vazia causava os losangos pretos perto da cabana leste e de Adria.

Na posição restaurada por Home, o cenário usa o próprio desenho original do jogo, com sua ordem de sobreposição entre telhados, árvores e personagens. A seleção também usa as regras originais nessa posição. Ao girar, inclinar, aproximar ou deslocar a câmera, o jogo passa a desenhar os volumes 3D reconstruídos. Em Home, os pixels idênticos vêm do uso do mesmo backend original; isso não significa que as malhas 3D coincidam integralmente com a arte. Os novos ângulos mostram o estado real da reconstrução. As capturas de diagnóstico podem forçar o desenho das malhas no ângulo original para revelar diferenças que esse ponto de retorno preserva.

As casas, a taverna, a oficina aberta de Griswold, a cabana de Adria, o poço, a Catedral e a cripta usam malhas contínuas. As faces externas, internas e inferiores fecham os volumes e têm materiais opacos. A composição das imagens originais, incluindo portas, janelas e iluminação pintada, é projetada somente nas faces visíveis pelo ângulo nativo, respeitando a profundidade e as partes ocultas por outras superfícies. As costas e as superfícies reveladas ao girar recebem materiais próprios; a imagem frontal não é esticada sobre todo o objeto.

As duas cabanas com janela redonda e porta azul têm paredes baixas, telhado íngreme, porta lateral fechada, janela no frontão e barril junto à entrada. A área da imagem foi separada do volume das paredes: as paredes residenciais ficam dentro das áreas bloqueadas pelo mapa, evitando que o herói pareça entrar numa casa fechada. A oficina, Adria e a entrada da Catedral conservam as aberturas originais. A correspondência visual é conferida em capturas na mesma projeção; superfícies vistas por outros ângulos e algumas formas menores ainda precisam de reconstrução mais detalhada.

O agrupamento completo identifica seis famílias de árvores: na cena de teste com a grade nativa de 112×112, são 93 objetos. Combina os fragmentos do cenário e as imagens especiais, incluindo as árvores pequenas desenhadas somente por peças MIN. Os troncos que apareciam como prismas são substituídos, mantendo chão, riacho e pedras. Cada árvore recebe tronco e galhos com espessura, além de pequenos volumes fechados de folhagem quando presentes. Essas formas são uma reconstrução procedural: o jogo original fornece uma vista pintada e não define a geometria das costas.

As pedras são reunidas por seis padrões exatos de peças MIN: na cena final com a grade de 112×112, são 501 grupos e 1.607 células de origem. Esse total inclui 19 preenchimentos ocultos dentro das construções. Um grupo inteiramente substituído pela arquitetura, com seu ponto de referência no interior de um corpo fechado, permanece oculto e disponível para a auditoria. As rochas externas são mantidas. Uma pedra grande de quatro peças recebe um único volume fechado, com profundidade arredondada ajustada à sua silhueta. A frente conserva a arte original e as outras faces usam suas cores em materiais opacos. Chão e colisão permanecem nativos. Objetos sólidos ainda sem um grupo identificado recebem relevo fechado por fragmento: isso retira as caixas genéricas, mas não garante uma forma contínua para ruínas ou decorações compostas de vários fragmentos. A profundidade das pedras também é inferida de uma única vista.

O herói usa as oito vistas disponíveis de sua animação para formar um corpo com profundidade; a cena de diagnóstico desta versão usa o guerreiro. As vacas também usam suas oito vistas originais. As diferentes faces recebem a arte da direção correspondente. Os demais habitantes têm apenas uma vista original: seus corpos usam cabeça, tronco e membros arredondados, ou roupa contínua, ajustados ao contorno dessa imagem. A profundidade e as costas desses habitantes são inferidas; a anatomia ainda é uma aproximação. O diagnóstico usa as 11 fontes originais de habitantes, incluindo as três vacas. No fallback de uma só vista, as vacas conservam o relevo de seu contorno nativo, sem receber anatomia humana.

Os corpos encostam no chão por uma translação rígida que preserva a projeção frontal. A sombra pintada no chão fica separada da geometria do corpo. A máscara retira o preto externo da região inferior usado como sombra; detalhes pretos fechados e superiores permanecem. As imagens originais continuam intactas. Efeitos continuam usando imagens planas.

O modo 3D se aplica somente a Tristram. Os outros mapas continuam com a renderização original. A geometria visual acompanha o mapa da partida; colisão, movimentação, inventário e diálogos continuam sendo calculados pelo jogo. A câmera 3D tem controles próprios de distância, independentes do zoom isométrico.

Nesta versão, os rótulos de itens pela tecla Alt ainda não aparecem no modo 3D. Os itens continuam visíveis e podem ser selecionados diretamente. A renderização usa o processador e a paleta original do jogo.

## Reconstrução e Meshy

O critério de reconstrução vale para todo o cenário: o mesmo enquadramento deve preservar posição, proporções, contorno, telhado, porta, janela e objetos próximos. A geração automática fornece um candidato; o candidato é comparado com a arte original antes de substituir um objeto do jogo.

O primeiro teste de Meshy usou uma composição dos pixels originais da cabana leste, sem árvores nem personagens. Consumiu 30 créditos e produziu um modelo de 2.760 triângulos, além de uma malha mestre preservada. O resultado recuperou a forma geral, mas alterou janela, acabamento e capa do telhado. Está guardado para calibração em `models/meshy/cabin-east-v1` e permanece fora do jogo; as casas desta versão usam a reconstrução local. `quality-review.json` registra a decisão.

`tools/meshy_assets.py` permite consultar saldo, criar uma única geração, consultar seu estado e baixar os modelos e texturas. As imagens são enviadas como dados incorporados; os metadados guardam a origem e seu hash. A chave é lida de `MESHY_API_KEY` ou do armazenamento protegido do usuário Windows, fora deste projeto. `tools/inspect_glb.py` inspeciona geometria, orientação, UVs e materiais e extrai as texturas sem redesenhá-las. Documentação usada: [Meshy Image to 3D](https://docs.meshy.ai/en/api/image-to-3d).

## Logo de entrada

No perfil de teste, a entrada e os menus mostram **Diablo 3D**, com as letras originais e o fogo animado do jogo. O D acrescentado é uma cópia do primeiro D; o numeral 3 foi criado no mesmo estilo. As animações mantêm os 15 quadros e o intervalo original de 60 ms.

Os arquivos de substituição ficam em `perfil-tristram/ui_art/smlogo.pcx` e `perfil-tristram/ui_art/logo.pcx`. É necessário fechar e reabrir o jogo para carregar uma alteração do logo. O DIABDAT.MPQ da instalação original é somente lido.

A arte do numeral e a instrução da edição estão em `branding/numeral-3.png` e `branding/brand-info.txt`. A ferramenta opcional `diablo_logo_build` monta as animações e verifica a leitura pelo mesmo carregador usado no jogo.

Para montar novamente e instalar o logo no perfil:

```powershell
.\build.ps1 -WithBranding -Targets diablo_logo_build
.\Atualizar-Logo.ps1
```

## Código e compilação

O código do engine está na raiz deste repositório e em `Source`, baseado no commit `dac104babfb6187415432f428ac2516747ffc154` do DevilutionX 1.6.0-dev.

```powershell
.\build.ps1
```

O script detecta Visual Studio, CMake e Ninja instalados; consulte [BUILDING-D3D.md](BUILDING-D3D.md). O resultado da compilação v4 fica em `build/devilutionx-tristram-v4.exe`. As bibliotecas são compiladas a partir dos códigos publicados de suas dependências. Este build usa apenas o modo local.

## Validação

A compilação Windows v4 e os diagnósticos finais passaram em 07/10/2026 com `DIABDAT.MPQ` do GOG e, separadamente, com `spawn.mpq`. A ferramenta `town_view_smoke` verifica alternância de visual, seleção pela câmera, preservação da partida, recarga dos recursos e liberação da memória sem abrir uma janela. Os relatórios e as capturas finais ficam em `diagnostics/v4-final-gog` e `diagnostics/v4-final-shareware`; `diagnostics/v4-release-audit.json` registra o executável e os resultados.

Em cada conjunto de dados, passaram as auditorias dos 93 volumes de árvores e dos 501 grupos de pedras, incluindo os preenchimentos ocultos. Foram gravadas 40 vistas de giro de cabana, poço, árvore e herói. Os 12.794 raios independentes da cabana e do poço não encontraram faces ausentes; o guerreiro permaneceu selecionável nos dez ângulos do teste em espaço aberto. A cena perto da cabana conserva a oclusão real do herói pela construção. Os testes também verificam as oito direções originais do guerreiro e das vacas, os habitantes, a projeção dos pés e os centros caminháveis.

A auditoria dos volumes confere triângulos finitos, profundidade, fechamento e orientação das arestas, pés no chão e reconstrução determinística. Também verifica o guerreiro em oito direções, os habitantes reais, cada árvore completa e os grupos de pedras. Essas verificações mecânicas não aprovam a fidelidade visual: as capturas de giro devem ser examinadas para detectar formas erradas, sobreposições e diferenças de textura.

As comparações separam três situações: o backend original do jogo, a rota de Home que usa esse mesmo backend e o desenho forçado das malhas no ângulo nativo. Os 24 enquadramentos de Home tiveram pixels exatamente iguais à referência em cada conjunto de dados. Isso confirma o uso correto do desenho original; não representa coincidência total das malhas. A comparação forçada das malhas com os dados GOG registrou diferenças nos 24 enquadramentos: a reconstrução ainda não satisfaz coincidência completa. Os resultados estão em `diagnostics/qa-v4-final-gog`. `tools/compare_native_views.py` compara RGB na mesma posição, incluindo preto verdadeiro, sem reposicionar nem redimensionar. Uma porcentagem global alta pode esconder falhas pequenas entre grandes áreas de chão e fundo.

Os diagnósticos anteriores foram preservados em `diagnostics/gog`, `diagnostics/v2-gog` e `diagnostics/v3-gog`. A validação sem janela deve ser complementada pela experiência com teclado, caminhada e diálogos no jogo aberto pelo iniciador.

```powershell
.\build.ps1 -WithSmoke -Targets devilutionx,town_view_smoke
.\build\town_view_smoke.exe 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo' '.\build\assets' '.\diagnostics\v4-final-gog'
```

Fontes: [DevilutionX](https://github.com/diasurgical/devilutionX), [dados shareware](https://github.com/diasurgical/devilutionx-assets/releases/latest/download/spawn.mpq).

## Sombras, comunidade e próximos passos

[Discord oficial](https://discord.gg/4YxQ7s69S). O plano inclui sombras reais calculadas a partir dos volumes e das luzes da cena, com personagens e luzes em movimento. As sombras pintadas atuais são temporárias. Será necessário separar a iluminação presente nas texturas originais para evitar sombra duplicada. Isso ainda não está implementado.

Geração 3D dos níveis procedurais, voz por proximidade e um hub com mais participantes são etapas futuras. Consulte o [roadmap](ROADMAP.md) e a [pesquisa de rede e voz](NETWORKING-RESEARCH.md). O código público preserva a [Sustainable Use License do engine](../LICENSE.md), que limita a distribuição a usos gratuitos e não comerciais.