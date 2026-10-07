# Estado do protótipo Tristram v4

O objetivo do Diablo 3D é reconstruir o jogo inteiro em 3D, incluindo os níveis procedurais. Este documento acompanha a etapa atual em Tristram, usada para validar o pipeline antes do primeiro andar procedural da Catedral e dos demais ambientes. Usa a mesma partida do DevilutionX: a tecla F4 alterna entre o desenho original e o novo desenho, sem recarregar o mapa. A quarta versão está disponível pelo iniciador e reconstrói construções, árvores, pedras e personagens com volume. As faces ocultas e alguns detalhes ainda precisam de refinamento visual.

Este documento distingue as validações locais registradas em 07/10/2026 da revisão atual de cabanas, importação Meshy e sombras. Capturas, relatórios gerados, modelos experimentais de Meshy, arquivos do jogo e perfis de jogador não são distribuídos neste repositório. Os caminhos de diagnóstico abaixo são saídas locais que precisam ser geradas de novo. Consulte o [roadmap](ROADMAP.md) para trabalho futuro.

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

A revisão atual separa materiais de alvenaria, palha, porta, vidro, madeira, degrau e barril nas cabanas. Amostras dos planos originais são retificadas em coordenadas físicas, evitando repetir porta ou janela nas paredes traseiras. A cumeeira foi ajustada de 4,65 para 4,87 unidades a partir de sua linha na imagem original; a janela também foi reposicionada pelo centro dos pixels dourados. Essas medidas refinam o candidato local e não dispensam a revisão artística em todos os ângulos.

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

A segunda geração usou múltiplas vistas pela API: a imagem original primeiro, seguida de vistas geradas da traseira e do frontão. Custou 44 créditos no total e retornou 5.783 triângulos, textura 4K, mapas PBR e malha mestre. Existe importação real opcional na cabana leste, pelo perfil separado `Comparar-Cabana-Meshy.cmd`. A janela traseira é uma interpretação do candidato, aprovada para esta revisão pelo responsável pelo projeto; não existe uma vista traseira original que comprove seu desenho. O candidato ainda requer revisão do telhado, das placas de chão e da topologia antes de se tornar a substituição padrão. Consulte [MESHY-WORKFLOW.md](MESHY-WORKFLOW.md) para geração, conversão, calibração e revisão.

## Calibração da iluminação

Antes de continuar os interiores ou gerar outros modelos, a cabana leste recebeu uma calibração da luz na mesma perspectiva da referência original. Os materiais importados agora recebem luz sobre sua cor de origem, em RGB linear, antes de serem convertidos para a paleta do jogo. A luz ambiente permanece nas sombras; a luz direta e o mapa de sombras usam a mesma direção. As faces vistas pelo verso também recebem a orientação correta para iluminação, eliminando grandes manchas triangulares do telhado sem alterar a geometria, as UVs ou a textura do modelo.

O perfil comum está em `assets/d3d-lighting.ini`, com ambiente `(0.08, 0.085, 0.09)`, luz direta `(1.55, 1.47, 1.62)` e direção `(0.32, 1, -0.3)`. Uma cópia desse arquivo no perfil do jogador pode substituir os valores após reiniciar o jogo. Consulte [TRISTRAM-LIGHTING.md](TRISTRAM-LIGHTING.md) para unidades, comparação por material e orientação aos próximos modelos.

A parede do frontão aproximou-se do tom escuro original, a lateral perdeu a aparência excessivamente clara e o telhado conservou o brilho. A textura gerada ainda tem menos contraste que a arte original e o trecho sob o beiral da porta permanece mais escuro. As texturas nativas que já contêm iluminação pintada continuam com o tratamento de compatibilidade.

O perfil opcional da cabana Meshy leste inclui um cômodo físico com paredes de pedra, teto e piso de 12 tábuas. Nesta revisão, as janelas da frente e de trás recebem recortes contínuos no cômodo, túneis de pedra e divisórias de madeira. Duas velas com cera, pavio, suporte e chama volumétrica substituem a fonte genérica anterior. A luz usa tons quentes de fogo e oscilação suave e independente, sem consumir o acaso da partida. O contorno da luz respeita os mesmos polígonos de 20 lados das aberturas, bloqueando os cantos de pedra. A porta permanece fechada e a colisão original impede a entrada do jogador. Os demais interiores ainda precisam da mesma preparação a partir de suas próprias referências; o [padrão de aberturas e luz de fogo](BUILDING-OPENINGS.md) registra esse contrato, incluindo frestas de telhado quando comprovadas ou aprovadas como inferência.

## Logo de entrada

No perfil de teste, a entrada, o menu e a pausa mostram **Diablo 3D** com a animação fornecida pelo autor: 240 quadros, 30 quadros por segundo e ciclo de 8 segundos. A conversão preserva a arte entregue e adapta formato e paleta ao carregador do jogo. Consulte [ANIMATED-LOGO.md](ANIMATED-LOGO.md).

Os arquivos atuais são `assets/ui_art/d3d-menu.pcx` para o menu principal, `assets/ui_art/d3d-title.pcx` para a entrada e `assets/ui_art/d3d-pause.pcx` para o menu de pausa. A compilação copia esses recursos; uma substituição local usa os mesmos três nomes em `perfil-tristram/ui_art/`. É necessário fechar e reabrir o jogo para carregar uma alteração. [ANIMATED-LOGO.md](ANIMATED-LOGO.md) documenta a conversão da sequência fornecida, dimensões, paleta, ciclo e instalação. O DIABDAT.MPQ da instalação original é somente lido.

`Atualizar-Logo.ps1` e a ferramenta `diablo_logo_build` permanecem como o experimento anterior de 15 quadros, baseado na arte nativa e no numeral local em `branding/numeral-3.png`. Seus resultados ficam no perfil local; a animação atual de 240 quadros tem prioridade quando presente.

## Código e compilação

O código do engine está na raiz deste repositório e em `Source`, baseado no commit `dac104babfb6187415432f428ac2516747ffc154` do DevilutionX 1.6.0-dev.

```powershell
.\build.ps1
```

O script detecta Visual Studio, CMake e Ninja instalados; consulte [BUILDING-D3D.md](BUILDING-D3D.md). O resultado da compilação v4 fica em `build/devilutionx-tristram-v4.exe`. As bibliotecas são compiladas a partir dos códigos publicados de suas dependências. Este build usa apenas o modo local.

## Validação

A revisão de janelas e fogo passou em 07/10/2026: `town_lighting_smoke`, cena GOG, cena shareware e cena com a cabana Meshy. As saídas são `diagnostics/v4-fire-openings-gog`, `diagnostics/v4-fire-openings-shareware` e `diagnostics/v4-fire-openings-final-meshy-gog`; o executável tem SHA-256 `30c6b494fc28603bdb22b3375bc2251e70ba90cc94c544041e9c7ad7ea25e143`. A comparação ligada/desligada registra 112 pixels alterados na frente e 73 atrás, sem mudanças fora da cabana. O avanço do tempo visual em 0, 3 e 17 segundos comprova oscilação também depois da conversão para a paleta. Os raios atravessam as duas janelas e bloqueiam seus cantos de pedra; a malha externa e as UVs dos 5.783 triângulos permanecem exatas. Essas verificações se referem ao candidato opcional, não à conclusão de todos os interiores da cidade.

A revisão anterior do interior e da limpeza de chão passou em 07/10/2026 com `town_lighting_smoke` e `town_view_smoke`, usando dados completos do GOG, dados shareware e o perfil separado com o candidato Meshy. As rodadas são `diagnostics/v4-interior-final-gog`, `diagnostics/v4-interior-final-shareware` e `diagnostics/v4-interior-final-meshy-gog`. O executável daquela etapa tem SHA-256 `332d77da01ba4ac28aeb5729945a37a8ddfe69f6128a643ca4d82463d1a08fd6`; `diagnostics/cabin-interior-release-audit.json` registra as saídas. A captura com a fonte genérica ligada/desligada conferia luz quente e parede traseira opaca. A etapa atual substitui essa fonte por velas e abre a janela traseira, conforme a orientação posterior do responsável pelo projeto. Os registros anteriores permanecem como histórico. A revisão artística e de fechamento da malha Meshy ainda não está encerrada.

A calibração exterior anterior permanece em `diagnostics/v4-lighting-release-gog`, `diagnostics/v4-lighting-release-shareware` e `diagnostics/v4-lighting-release-meshy-gog`, com SHA-256 `1a5f148cd597a4a86032ff7987e6865a4ccef9c8c94c960b25c686e067883607`. O registro `diagnostics/tristram-lighting-release-audit.json` identifica essas referências, que ainda não tinham o interior ativo.

As rodadas anteriores `diagnostics/v4-masked-final-gog`, `diagnostics/v4-masked-final-shareware` e `diagnostics/v4-meshy-masked-review-gog` permanecem como referência anterior à calibração. O registro `diagnostics/meshy-and-shadow-release-audit.json` identifica seu executável de SHA-256 `3d2038a84bfd871991f4c85683311243ea6a1a4bd1eedc96611c6989541d5981`.

O roteiro grava 50 vistas das cabanas leste e oeste, poço, árvore e herói: dez ângulos por alvo, incluindo giros de −5° e +5° para revelar falhas próximas da vista original. Verifica alternância de visual, seleção, oclusão, preservação da partida, recarga e liberação dos recursos. A auditoria mecânica dos volumes reconstruídos confere triângulos finitos, profundidade, fechamento e orientação, pés no chão, determinismo, oito direções do guerreiro e das vacas, habitantes, árvores e grupos de pedras. O importador é exercitado com arquivos válidos e malformados, preservando a alternativa procedural quando o recurso opcional é rejeitado. Os testes de sombra incluem reutilização do cache, mudanças na geometria e na direção da luz, além da preservação dos pixels fora das máscaras auditadas. A leitura independente confirmou oito peças alteradas e 80 controles por rodada sem divergências dos pixels esperados; a preservação integral da opacidade foi verificada no código, pois os PNGs indexados não expõem esse vetor do runtime.

As comparações separam o backend original do jogo, a rota de Home que usa esse mesmo backend e o desenho forçado das malhas no ângulo nativo. Home confirma o retorno correto ao desenho original; sua coincidência de pixels não aprova as malhas procedurais nem o candidato Meshy. A aprovação visual exige examinar os volumes forçados e todos os giros, procurando formas erradas, sobreposições e diferenças de textura. `tools/compare_native_views.py` compara RGB na mesma posição, incluindo preto verdadeiro, sem reposicionar nem redimensionar. Uma porcentagem global alta pode esconder falhas pequenas entre grandes áreas de chão e fundo.

Como registro histórico de 07/10/2026, a compilação e os diagnósticos anteriores `diagnostics/v4-final-gog` e `diagnostics/v4-final-shareware` passaram com GOG e `spawn.mpq`; `diagnostics/v4-release-audit.json` identifica aquela execução. Ela auditou 93 árvores, 501 grupos de pedras, 40 vistas de giro e 12.794 raios da cabana e do poço. Os 24 enquadramentos de Home coincidiram com a referência em ambos os conjuntos, enquanto o desenho forçado das malhas GOG mostrou diferenças nos 24 enquadramentos, registradas em `diagnostics/qa-v4-final-gog`. Esses resultados anteriores não aprovam as alterações desta revisão.

Os diagnósticos anteriores também foram preservados em `diagnostics/gog`, `diagnostics/v2-gog` e `diagnostics/v3-gog`. A validação sem janela deve ser complementada pela experiência com teclado, caminhada e diálogos no jogo aberto pelo iniciador.

```powershell
.\build.ps1 -WithSmoke -Targets @('devilutionx','town_view_smoke','town_lighting_smoke')
.\build\town_lighting_smoke.exe
.\build\town_view_smoke.exe 'C:\Program Files (x86)\GOG Galaxy\Games\Diablo' '.\build\assets' '.\diagnostics\v4-lighting-release-gog'
```

Fontes: [DevilutionX](https://github.com/diasurgical/devilutionX), [dados shareware](https://github.com/diasurgical/devilutionx-assets/releases/latest/download/spawn.mpq).

## Sombras, comunidade e próximos passos

[Discord oficial](https://discord.gg/4YxQ7s69S). A renderização 3D agora calcula sombras das construções com um mapa de profundidade de 512×512 visto por uma luz direcional. Telhados, paredes e objetos importados bloqueiam essa luz; o chão e as superfícies desenhadas recebem a sombra com filtro de borda. A construção do mapa fica em cache durante o giro da câmera.

Nas oito peças auditadas de sombra pintada das cabanas, máscaras congeladas delimitam os pixels cuja cor recebe grama limpa da peça doadora: 867 usa 859, 868 usa 860, 871 usa 875, 872 usa 876, 873 usa 875, 874 usa 876, 882 usa 881 e 884 usa 883. A revisão retira os quatro fragmentos que ainda formavam a sombra antiga junto às paredes, permitindo que a sombra geométrica ocupe esse papel. A aplicação altera somente os pixels selecionados e cobertos por ambas as peças. Todos os pixels originais fora da máscara, inclusive preto opaco, e a opacidade original inteira permanecem preservados. A transição escura legítima entre terra e grama perto do poço foi analisada e mantida. Os dados de origem das peças, o mapa, a colisão e a vista original conservam seus dados nativos.

Os objetos que atualmente lançam sombras por esse sistema são apenas as construções. Árvores, pedras e personagens ainda não lançam sombras por ele; personagens mantêm suas sombras de compatibilidade. Luzes de tochas, movimento e remoção das demais sombras pintadas precisam de trabalho adicional. A direção é configurável em `d3d-lighting.ini` e testada com luz invertida, mas não há controle de iluminação na interface.

Geração 3D dos níveis procedurais, voz por proximidade e um hub com mais participantes são etapas futuras. Consulte o [roadmap](ROADMAP.md) e a [pesquisa de rede e voz](NETWORKING-RESEARCH.md). O código público preserva a [Sustainable Use License do engine](../LICENSE.md), que limita a distribuição a usos gratuitos e não comerciais.
