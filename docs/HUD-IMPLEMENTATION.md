# HUD principal e edição visual no Godot

Entrega autorizada em 8 de outubro de 2026: implementar primeiro a visão principal do HUD aprovada no estudo, conservando os controles do DevilutionX. Esta trilha é independente da revisão G1 da cabana; não conclui Tristram. Os arquivos em `docs/hud-study/images/` são conceitos, não capturas da implementação.

O escopo é refazer a apresentação das funções existentes e permitir editar seu layout depois. O usuário reforçou que novas funções poderão ser discutidas no futuro, mas não pertencem a esta entrega. A faixa adicional de quatro botões de magia foi retirada; os atalhos originais permanecem. O contrato visual corresponde aos seis botões de jogador único, às duas funções condicionais de multijogador, aos oito espaços do cinto, aos dois globos, à magia preparada e à caixa de informações, com os avisos e estados condicionais nativos preservados.

O alinhamento com o chat do estudo gerou novas referências funcionais [16:9](hud-study/images/07-hud-native-16x9-v1.png) e [4:3](hud-study/images/08-hud-native-4x3-v1.png), a partir da captura do HUD original fornecida pelo usuário. Os prompts e a origem estão em [native-hud-r1.json](hud-study/native-hud-r1.json). São conceitos visuais para orientar a apresentação; não são capturas da implementação nem substituem automaticamente uma revisão artística aceita.

## Comportamento da primeira entrega

- Vida e mana usam os valores e os sprites nativos. A composição conserva as duas regiões e os limites de preenchimento dos globos originais. As preferências de mostrar números e a restrição de mana continuam vigentes.
- Os oito espaços do cinto usam `SpdList`, desenho de itens, contorno, requisitos, atalhos, uso, retirada e colocação nativos. Slots vazios também mostram os atalhos realmente configurados. Desenho, hover, clique e arraste consultam os mesmos retângulos.
- A magia atual abre a lista nativa, incluindo Shift para limpar a seleção. As teclas configuradas conservam a seleção pelos handlers originais; não há uma fileira adicional de botões de magia.
- Personagem, inventário, livro de magias, missões, automapa e menu conservam seus handlers. O botão de mapa aciona o automapa existente; esta entrega não implementa um minimapa novo.
- Comunicação e modo amigável continuam condicionados à disponibilidade nativa no modo multijogador.

O desenho usa globos e ícones originais em placas escuras com molduras douradas, seguindo a composição escolhida. Não equivale à reprodução integral da arte dos conceitos. Os submenus, lojas, livro e painéis ainda usam a apresentação herdada. Chat, controles virtuais e os menus de modificador do gamepad usam o painel original para conservar seu desenho e navegação. Em telas nas quais os painéis laterais podem cobrir toda a vista, abrir um deles também restaura o painel original: a faixa compacta nova cruzaria os últimos 16 pixels do painel lateral nativo. A mudança de apresentação força redesenho completo para não deixar imagens antigas.

## Editar visualmente

Execute `Abrir-HUD-Godot.cmd` na raiz do workspace para abrir diretamente a cena [ui/hud.tscn](../editor/godot/ui/hud.tscn) na área **2D**. Também é possível abrir [editor/godot/project.godot](../editor/godot/project.godot) no Godot portátil já usado pelo editor de arquitetura. A cena do menu de entrada é separada, `ui/main_menu.tscn`; ambas compartilham o mesmo arquivo exportado e preservam as seções da outra.

1. Selecione um Control `Hud*` e ajuste posição e tamanho pelos offsets, preservando sua âncora. O cinto é um Control único; o jogo divide sua largura em oito espaços sem lacunas.
2. Salve a cena. Use **F6** para testar a prévia responsiva. Vida, mana, itens e magias da prévia são demonstrações; a cena não executa a partida.
3. Selecione a raiz e use **Exportar layout HUD** no Inspector. A exportação explícita grava `editor/godot/local/ui/layout.ini`, com backup; salvar a cena não instala mudanças.
4. A aplicação usa o mesmo fluxo do menu, `Aplicar-Menu-Godot.cmd`, com o jogo fechado. O aplicador valida os elementos e preserva um backup. Reabra o jogo para recarregar. Consulte também a documentação do menu preparada pelo chat responsável por ele.

O formato v1 exporta **layout e áreas de clique**. Alterações de cor, fontes, shaders, texturas ou animações da prévia não são transportadas ao C++. O jogo mantém os textos traduzidos, os atalhos configurados, os ícones e estados reais.

## Contrato compartilhado

`Source/engine/render/d3d_ui_layout.*` carrega `d3d-ui/layout.ini`. O cabeçalho é `[Layout]`, `format=d3d.ui-layout`, `schemaVersion=1`. Cada elemento possui `anchor`, `offsetX`, `offsetY`, `width` e `height`, em inteiros. Strings do INI são cruas, sem aspas. Exportar o HUD conserva os blocos não-HUD; exportar o menu conserva os blocos HUD.

Os elementos do HUD usam uma altura lógica de **480**. A largura lógica é `floor(larguraDaTela × 480 / alturaDaTela)`. O SafeFrame é centralizado e tem largura `min(larguraLógica, floor(480 × 16 / 9))`. O resultado do loader é resolvido nesse espaço, limitado ao SafeFrame e ampliado proporcionalmente por `alturaDaTela / 480`; desenho e input recebem o mesmo retângulo final. Assim, Full HD não deixa controles do tamanho de uma interface de 640 pixels e ultrawide não desloca utilitários para as extremidades.

| Elemento | Âncora | Offset X, Y | Tamanho |
| --- | --- | --- | --- |
| `HudHealthOrb` | bottom-center | −224, −104 | 88 × 88 |
| `HudManaOrb` | bottom-center | 136, −104 | 88 × 88 |
| `HudBelt` | bottom-center | −116, −43 | 232 × 29 |
| `HudSpell` | bottom-center | 60, −103 | 56 × 56 |
| `HudInfo` | bottom-center | −200, −210 | 400 × 64 |
| `HudCharacter`, `HudInventory`, `HudSpellbook`, `HudQuests`, `HudMap`, `HudMenu` | bottom-right | −198 / −166 / −134 / −102 / −70 / −38, −44 | 30 × 32 |
| `HudChat`, `HudFriendly` | bottom-right | −70 / −38, −80 | 30 × 32 |

A faixa de utilitários sobe **100 pixels lógicos** quando a largura do SafeFrame é menor que **844**. Esta translação de apresentação é retirada ao exportar os offsets; não se acumula ao reabrir a cena. Coordenadas são limitadas e bordas escaladas em conjunto para manter células adjacentes. Uma edição pode sobrepor elementos; os testes garantem ausência de colisão nos padrões, não aprovação automática de todo layout personalizado.

O `MainPanel` físico permanece com 640 × 128 para buffers, sprites e cálculos herdados. O novo HUD desenha sobre a vista completa, usa regiões efetivas no compositor 2× e redesenha todos os seus elementos. Os gates de mouse, hover, câmera e navegação do cinto por controle usam os mesmos helpers. XP, durabilidade, level-up e dicas de controle conservam as funções existentes.

O botão de subir nível e os avisos de durabilidade têm áreas reservadas que evitam os elementos do HUD e os painéis abertos. Desenho e clique de subir nível usam o mesmo retângulo; o botão continua abrindo os atributos nativos. Os quatro avisos mantêm seus sprites, ordem e limites de desgaste. São estados originais reposicionados para continuarem visíveis, sem novas ações ou campos de exportação. Durante os modificadores de navegação e magia do gamepad, o painel herdado impede que os globos/magia encubram as dicas originais.

Quando a placa dos números cruzaria a barra de XP, ela fica dentro do globo, conservando a barra acessível e visível. O cálculo inverso do cursor desconta as linhas cobertas conforme o viewport efetivo: a vista completa no PC compacto e a vista reservada dos controles virtuais mantêm o mesmo alinhamento do desenho. Um cinto movido sobre a área de um inventário oculto tem prioridade nos três caminhos nativos de hover, retirada e colocação, evitando selecionar um item invisível.

A conjuração, inclusive pelos atalhos existentes, a seleção de nomes de itens no chão e o feedback de entradas/escadas consultam as regiões atuais do HUD. Assim, o espaço de mundo liberado pelo painel antigo mantém suas interações, e os controles reposicionados não deixam selecionar o chão através deles. O clique direito continua cancelando os cursores especiais nativos. Com o menu da partida aberto, a caixa de informações flutuante não cobre suas opções; a apresentação herdada conserva seu caminho original.

## Evidência e limites

- `editor/godot/tests/hud_geometry_smoke.cpp` executa os helpers geométricos C++ reais: **1.160 verificações** em 640 × 480, 800 × 600, 1280 × 720, 1920 × 1080, 2560 × 1080, 3440 × 1440, 1280 × 1024 e 640 × 360. Verifica limites, bordas exclusivas, interseções dos padrões, oito células e um cinto editado cuja largura não é divisível por oito.
- `editor/godot/tests/hud_layout_smoke.gd`: **962 verificações**, sem falhas, no Godot 4.7.2. Inclui defaults/âncoras iguais aos do C++, proporções, salvar/reabrir edição, translação compacta, preservação literal dos blocos de menu e comentários, backup e recusa de contratos incompatíveis. A prévia oculta Chat/Amigável por padrão; **Demo Multiplayer** permite inspecioná-los. Confere os 13 elementos do contrato e elimina as quatro seções de botões extras de exportações anteriores.
- `editor/godot/tests/hud_visual_smoke.gd`: **quatro renders** inspecionados, em 1280 × 720, 2560 × 1080, 640 × 480 e vida vazia/mana cheia. Evidência privada em `editor/godot/local/hud-visual-tests/`.
- `tools/hud_runtime_smoke.cpp`: **697 verificações**, sem falhas, e **18 capturas técnicas** em 640 × 480, 1280 × 720, 1920 × 1080 e 2560 × 1080. Usa arte, paleta, fontes, loader INI e handlers nativos. Exercita abertura/fechamento de personagem, inventário e livro, exclusão mútua dos painéis, hover dos seis utilitários, oito itens do cinto, retirada/colocação, seleção pelos atalhos originais e Shift no botão da magia atual, vida/mana vazias/meias/cheias, NoMana, preferências de números, viewport compacto, proteção de XP e preservação do RNG no desenho/hover. Duas edições reais de layout comprovam áreas de clique compartilhadas, incluindo um cinto sobre a área de um inventário oculto. O estado amigável altera o desenho sem emitir comandos de rede. Recibo privado: `diagnostics/hud/hud-runtime-499351574346400/receipt.json`, na raiz do workspace; log: `diagnostics/hud-runtime-final.log`.
- A rodada final também verifica desenho e clique nativos de subir nível, sua supressão por estado, um a quatro avisos de durabilidade após a composição e seus limites 2/3/5/6, informações sem cobrir um menu real ativo e desenho das duas dicas de modificador do gamepad com fallback. O dispatch real de magia rejeita os controles do HUD e aceita a área de mundo liberada do painel antigo; o comando usa um provider privado, sem executar o feitiço ou a simulação.
- O diagnóstico usa um perfil exclusivo, grava somente sua configuração privada e não inicia partida, mundo ou saves. Não exercita cliques nos botões de mapa/menu/missões, sessão multijogador nem navegação completa por gamepad. Os gates de nomes de itens no chão e entradas/escadas foram revisados por código, sem fixture própria de gameplay. O teste atualizado em `test/scrollrt_test.cpp` não foi executado pelo gtest neste build; o diagnóstico nativo acima executou os mesmos casos de viewport e linhas cobertas.
- O build Windows integrado passou, incluindo o executável candidato e o diagnóstico do HUD. O executável final tem SHA-256 `c87c2327063f092aa68ac2df6bc89625b6a3b87866427dd34cc462255907ea08`. As capturas nativas foram inspecionadas para preenchimento dos globos, limites, proporções, ausência de botões extras e disposição dos controles/avisos; o layout padrão ficou sem cortes ou sobreposições nas resoluções capturadas.
- Capturas de diagnóstico com fundo sintético comprovam o desenho dos controles, não a composição final na janela do jogo. A fidelidade visual ao estudo e a experiência prolongada precisam de revisão identificada no executável candidato.

Nenhum save, modelo, luz, colisão ou rotina de simulação é parte desta mudança. Não existe decisão de portar o jogo para Godot. Próxima ação desta trilha: validar o HUD principal na partida e registrar defeitos concretos antes de iniciar a apresentação dos menus e submenus.
