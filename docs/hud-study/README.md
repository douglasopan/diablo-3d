# Estudo visual de HUD e menus do Diablo 3D

Estudo solicitado em **8 de outubro de 2026**, ampliado pelo usuário para cobrir as telas atuais do jogo. A etapa operacional continua **G1 — cabana de referência**, conforme [PROJECT-EXECUTION.md](../PROJECT-EXECUTION.md). Este trabalho ocupa somente `docs/hud-study/`; a integração do jogo e do editor pertence ao outro chat. O guia central, Source, opções, áudio, executáveis, saves e launchers não foram alterados por este estudo.

## Abrir e revisar

Abra [a galeria](index.html) em um navegador. Ela inclui busca, famílias, disponibilidade e fontes, três formatos de visualização e **Ampliar**. Seus dados estão embutidos para abertura local, sem instalação ou conexão externa.

Há **223 exemplos visuais em 13 famílias**: 142 bases iniciais, 33 variantes adicionais auditadas e 48 variantes do front-end. A contagem inclui estados e apresentações compartilhadas; não significa 223 menus distintos. Cada exemplo tem uma prévia JPEG em [examples/](examples/). O [índice de cobertura](COVERAGE.md) relaciona IDs, telas, condições e fontes.

As composições individuais combinam arte gerada e layout/texto determinísticos. Os quatro conceitos de HUD e o fundo foram produzidos com o **gerador integrado image_gen**; os JPEGs de cada tela são renderizações da galeria. São propostas de apresentação com dados fictícios, sem executar as ações do jogo.

## Revisão funcional — somente controles nativos

Novo requisito explícito do usuário, recebido em **8 de outubro de 2026** pelo alinhamento com o chat de implementação: manter exatamente as funções do HUD original nesta etapa. Foram geradas duas novas referências pelo `image_gen` integrado:

- [07 — funções nativas, 16:9](images/07-hud-native-16x9-v1.png).
- [08 — funções nativas, compacto 4:3](images/08-hud-native-4x3-v1.png).

Ambas mostram somente **CHAR, QUESTS, MAP, MENU, INV, SPELLS**, dois globos, oito espaços de cinto, uma caixa de informações contextual e uma magia preparada. A caixa central comporta o texto nativo multilinha; não é um registro de mensagens. Não existe botão permanente de ação esquerda no painel original. Chat/amistoso e subir nível continuam estados condicionais nativos a preservar na integração, embora não apareçam no exemplo solo.

Os atalhos de magia já existentes continuam no jogo; esta revisão não os transforma numa barra adicional de botões. As imagens 01–04 preservam a rodada artística anterior, mas seus controles ilustrativos não determinam o contrato funcional atual. As novas referências são **candidatas para revisão**, sem sobrescrever ou promover as anteriores e sem aprovação automática da implementação. O [manifesto da revisão](native-hud-r1.json) contém prompts completos, fontes, hashes, dimensões efetivas e conferência dos elementos.

O usuário esclareceu que **tamanho, proporções, disposição, espaçamento e agrupamento podem mudar**. A restrição vale para os botões e suas funções; o layout original não precisa ser repetido. As coordenadas descritas nos prompts registram somente aquela geração.

## Direção recomendada

Ferro escurecido, pedra em baixo contraste, pequenos relevos góticos e detalhes de latão envelhecido. Vida vermelha e mana azul conservam os orbes clássicos. Uma ilha inferior reúne cinto, ação contextual e magia preparada; o restante do rodapé permite ver o mundo. O logo autoral D3D existente foi preservado por cópia exata, exibida como quadro estático de referência.

| Referência | Uso |
| --- | --- |
| [01 — 16:9](images/01-hud-16x9-v2.png) | Núcleo compacto; oito posições e quatro atalhos padrão. |
| [02 — ultrawide, revisão 2](images/02-hud-ultrawide-v2.png) | Laterais livres; mapa, mensagens e utilidades aproximados do centro. |
| [03 — viewport menor](images/03-hud-compacto.png) | Favorece leitura; favoritos recolhidos; cinto completo. |
| [04 — personagem e inventário](images/04-personagem-inventario.png) | Dois painéis e corredor de mundo entre eles. |
| [05 — fundo dos menus](images/05-fundo-menus.png) | Arte original gerada, sem texto ou HUD. |
| [Menu principal composto](examples/F03.jpg) | Logo existente e seis entradas atuais. |

As referências **01-hud-16x9-v2.png** e **05-fundo-menus.png** foram mantidas intactas após a comunicação do chat responsável pela integração. A revisão 1 de ultrawide foi preservada; a revisão 2 corrige a posição dos elementos periféricos.

## Escala, margens e leitura

Medidas abaixo são **alvos de desenho**, em pixels de referência a 1920×1080. A imagem gerada ilustra a direção; o layout da implementação deverá seguir medidas e estados, não coordenadas extraídas do bitmap.

| Elemento | Alvo |
| --- | --- |
| Margem externa | 24–32; distância mínima entre grupos 16. |
| Núcleo inferior | Aproximadamente 880–940 de largura e 160–180 de altura. |
| Orbes | 104–112; algarismos permanecem visíveis sem hover. |
| Cinto | Oito células, 44–48 cada, sempre acessíveis. |
| Ação, magia ativa e utilidades | Alvo clicável mínimo 44×44; toque 48–56. |
| Texto | Mínimo 16 para conteúdo essencial; 18–22 em listas; títulos 28–32. |
| Mapa compacto | 160–208, com limite independente da largura total. |

Usar escala de UI própria, ajustável, com limites mínimos de texto e interação. A densidade do mundo e seu enquadramento permanecem decisões separadas. Essa escala independente e os recursos de fontes/ícones HD **ainda dependem de implementação**.

Em **16:9**, ilha central e painéis laterais deixam o herói visível. Em **ultrawide**, as informações essenciais usam uma faixa central de largura `min(W, H × 16/9)`; a margem é `(W − faixa)/2 + 24`. As asas mostram o cenário. O catálogo usa 2560×1080; o mesmo princípio atende 3440×1440.

Em **1280×720**, conservar oito posições, orbes menores e magia preparada; recolher favoritos e informações secundárias. Quando as duas laterais não deixarem ao menos 320–360 de mundo livre, alternar personagem/inventário por abas. Listas longas rolam internamente, com ações fora da rolagem. Em viewports abaixo disso, priorizar um painel por vez e leitura; o estudo não estabelece suporte nativo validado em 960×540.

## Contratos preservados

- Cinto de **oito itens de uma célula**, teclas 1–8; mochila **10×4**; sete locais: cabeça, amuleto, torso, duas mãos e dois anéis. Baú **10×10**, com 100 páginas; loja visual **10×9**.
- O personagem guarda **12 atalhos de magia**; F5–F8 são os quatro padrões. Eles selecionam a magia, ou usam o quickCast já existente. A lista rápida e o grimório são telas diferentes. Magias proibidas na cidade aparecem inválidas.
- Mapa opaco, transparente e minimapa preservam os dados nativos de exploração. Mensagens transitórias, chat e histórico têm composições próprias. Não se acrescentam radar, objetivos ou voz.
- Personagem conserva Base/Agora, pontos, XP, ouro, combate, vida/mana e resistências. Inspeção do aliado é leitura. Em telas menores, as abas reorganizam a apresentação sem alterar capacidade.
- Lojas textual, com miniaturas e em grade têm exemplos próprios. Pepin cura ao abrir; Wirt cobra 50 para mostrar a oferta; Cain identifica por 100. A grade comercial usa transferência direta; as confirmações Sim/Não pertencem ao fluxo textual.
- Menus sobre a partida preservam ações e condições atuais. Salvar/carregar são ações imediatas com mensagens; não foi inventado navegador de slots. Pausa solo e multiplayer mantêm suas diferenças. Rede, Hellfire, toque e debug estão identificados como condicionais.

A [auditoria de entrada](FRONTEND-AUDIT.md), a [de configurações](OPTIONS-AUDIT.md) e a [dos painéis e comércio](INGAME-AUDIT.md) documentam as telas e respectivas fontes.

## Limites e próxima revisão

A análise de telas veio do código local; não houve abertura do executável nem validação de todos os fluxos na partida. As capturas históricas locais consultadas mostram o mundo e o menu herdado; não foi localizada uma captura completa e atual do HUD. O build local está `NONET=ON`; exemplos de TCP/ZeroTier documentam caminhos condicionais do código.

O cenário dos mockups, os ícones ilustrativos e os retratos esquemáticos não representam modelos ou arte HD aceitos. A galeria conserva as grades e o texto com precisão, mas não fornece um pacote de sprites/fontes para produção. Animação do logo, acessibilidade, localização, foco, contraste, hitboxes e o custo do compositor precisarão de validação na integração. A ajuda herdada cita quatro magias rápidas; o código atual define doze, divergência registrada sem alterar o jogo.

Próxima revisão deste escopo: avaliar hierarquia, legibilidade e densidade das famílias de tela. A implementação em andamento no outro chat usa as referências preservadas; aprofundar menus dependerá do escopo de integração definido ali. [Prompts completos](prompts.json), [proveniência](provenance.json) e [verificação](QUALITY.md) acompanham o estudo.
