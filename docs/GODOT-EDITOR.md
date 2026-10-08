# Editor Godot de Tristram

## Editar o menu de entrada visualmente

O menu tem uma cena de autoria própria: `editor/godot/ui/main_menu.tscn`. Na raiz do workspace, **Abrir-Menu-Godot.cmd** abre essa cena na área **2D** do Godot. O jogo continua no DevilutionX; o Godot exporta as posições usadas pelo desenho e pelas áreas de clique do menu real.

1. Selecione **MenuLogo** para mover o título, ou **MenuList** para mover/redimensionar a lista. Use os controles de posição e tamanho no editor 2D; conserve as âncoras centrais, escala 1 e rotação 0.
2. Selecione a raiz **MainMenu**. Em **Preview Size**, confira 640×480, 1280×720, 1920×1080 e 2560×1080. O fundo cobre a tela com recorte proporcional, sem deformar a imagem.
3. Salve a cena e clique em **Exportar layout do menu**, no Inspector da raiz. A saída é `editor/godot/local/ui/layout.ini`; exportar preserva as seções do HUD já existentes e não altera o perfil do jogo.
4. Com o jogo fechado, execute **Aplicar-Menu-Godot.cmd**. O aplicador valida o formato e grava somente `perfil-tristram/d3d-ui/layout.ini`, guardando uma cópia anterior. Reabra pelo atalho habitual. Saves, músicas, modelos e iluminação não são modificados.

No v1, o título animado conserva tamanho 580×154 e sua animação atual; sua posição pode mudar. A lista permite largura 300–640 e altura 258–384, distribuída entre as cinco ações atuais. O jogo usa no mínimo 43 pixels por linha; ajusta a fonte ao espaço disponível e quebra o rótulo longo quando necessário. A fonte e os textos da prévia Godot são auxiliares: a partida usa as fontes, traduções, seleção por teclado/controle e ações nativas. Alterar os rótulos dos botões na cena não renomeia funções do jogo.

O fundo aplicado é a imagem gerada e aprovada no estudo, sem os controles ilustrativos. Sua camada é RGB, independente da paleta do mundo. A composição mantém a transparência de índice zero da UI e conserva o preto opaco dos demais índices, o fade e o cursor. A ausência do fundo, um renderizador sem suporte ou a falha de carregamento conservam o menu herdado. Arquivos de layout inválidos usam posições padrão; as edições entram ao reabrir o menu.

O mesmo cenário agora acompanha Settings e suas categorias/escolhas, seleção de conexão/herói/partida, confirmações, progresso e Support. Os créditos e a apresentação inicial conservam seus fundos originais. O loader comum descarta fundos indexados apenas quando a camada RGB está disponível e a desliga antes da partida. Settings centraliza o bloco ocupado pelo título, lista e descrição; listas longas conservam rolagem e toda a altura disponível. A descrição tem sua própria reserva de 80 px, evitando o corte de 12 px do layout anterior. O diagnóstico `menu_presentation_smoke` passou 196 verificações de composição, ciclo da camada e geometria em cinco resoluções. A autoria Godot v1 ainda edita os retângulos do menu principal; os retângulos dos submenus não são exportados por essa cena.

O HUD principal tem sua própria cena, aberta por **Abrir-HUD-Godot.cmd**, e compartilha o contrato `d3d.ui-layout` versão 1 e a mesma exportação local. O fluxo de edição, aplicação e seus limites estão em [HUD-IMPLEMENTATION.md](HUD-IMPLEMENTATION.md). A primeira integração conserva controles e arte nativos; os painéis e submenus ainda precisam da próxima etapa visual.

Validação executada em 8 de outubro de 2026: cena/exportação Godot **16 verificações**, aplicador PowerShell **48**, composição/retângulos/cliques C++ **112**. Evidências privadas em `diagnostics/menu-godot/` e `diagnostics/town-music/godot-menu-smoke.log`. A validação usa renderer de software sem janela e arquivos temporários; não representa inspeção artística da janela completa. O botão no Inspector usa o recurso [export_tool_button do Godot](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html#export-tool-button).

## Suporte e créditos do projeto — 8 de outubro

O menu principal reúne **Suporte** e **Exibir Créditos** em **Suporte e Créditos ao Projeto**. O submenu mantém as duas telas herdadas e acrescenta **Créditos do Diablo 3D**, com autoria e direção de **Douglas Pan**, objetivo de reconstruir o Diablo 1 inteiro em 3D, convite à colaboração e links selecionáveis do GitHub, site e Discord. As telas herdadas conservam seu conteúdo e comportamento. Voltar retorna ao mesmo item selecionado; a navegação não inicia outra faixa de música.

A cena `ui/main_menu.tscn` acompanha as cinco ações; os retângulos exportados continuam no contrato v1. O diagnóstico final nativo passou **1.193 verificações**, com 26 capturas PT-BR/EN em 640×480, 960×540 e 1920×1080, incluindo override efetivo de largura 300 px, foco, teclado, clique, links simulados e falha de abertura do navegador. Não abriu endereços reais. A fixture Godot passou **103 verificações**. Evidência privada: `diagnostics/project-credits-20261008/project-credits-538077548623700/receipt.json`. As capturas foram revistas sem corte aparente; isso não substitui a avaliação artística do usuário na partida. Gamepad físico e reprodução sonora não foram exercitados nesta rodada.

Em **8 de outubro, 15:53**, a tela autoral passou a distinguir composição original de **Matt Uelmen, para Diablo (Blizzard Entertainment)** e regravação/reinterpretação produzida por **Douglas Pan com auxílio de IA**, com escopo Menu principal/Tristram e tradução PT-BR/EN. Os créditos herdados e as ações do submenu não mudaram; não há mudança de contrato/cena Godot. O espaçamento entre parágrafos caiu de 8 para 6 px para acomodar a informação em 640×480, inclusive com erro de abertura de navegador. Build e **2.051 verificações nativas** passaram em quatro resoluções, com 34 capturas e quatro inspeções finais; os testes incluem conteúdo compilado, limites reais de texto, ausência de sobreposição, teclado/clique, foco, links simulados e retorno às telas herdadas. Não iniciou partida ou abriu URLs reais. Instalação e fonte da atribuição em [MUSIC.md](MUSIC.md#créditos-das-regravações-no-jogo--8-de-outubro).

## Arquitetura estática

Primeira versão para inspecionar e editar **arquitetura estática**. Godot é a ferramenta de autoria; o jogo continua no DevilutionX. O contrato de arquivos está em [GODOT-BRIDGE.md](GODOT-BRIDGE.md), e IDs, variantes e aprovação continuam em [assets/registry.json](../assets/registry.json). Esta entrega auxilia a revisão G1 da cabana; não encerra Tristram nem inicia o cenário procedural.

## Abrir e preservar o trabalho

Para compilar a ponte, siga os pré-requisitos de [BUILDING-D3D.md](BUILDING-D3D.md) e execute na raiz do workspace, com o `build.ps1` atualizado:

```powershell
.\build.ps1 -ExecutableName devilutionx-tristram-godot -WithSmoke -Targets devilutionx,town_view_smoke
```

`-WithSmoke` habilita o diagnóstico na configuração; `-Targets` solicita explicitamente a compilação do jogo e de `town_view_smoke`. A compilação comum conserva `devilutionx-tristram-v4` como nome padrão, mas este fluxo exige o candidato `build/devilutionx-tristram-godot.exe` com a ponte atualizada.

1. Na raiz do workspace, execute `Abrir-Editor-Godot.cmd`. O launcher usa o Godot portátil em `.tools/godot/` e prepara `devilutionx/editor/godot/local/town-snapshot.json` se esse arquivo ainda não existir. A preparação depende do diagnóstico/build da ponte, dos dados locais do Diablo completo e da baseline selecionada. Uma falha deve ser corrigida antes de reconstruir a cena.
2. O projeto é [editor/godot/project.godot](../editor/godot/project.godot); o plugin já vem habilitado. Também é possível abrir esse projeto diretamente no Godot 4. A preparação local usa Python 3.11 ou posterior com Pillow.
3. No dock **Tristram**, clique em **Abrir / reabrir cena salva**. Na primeira vez, ele cria `local/tristram.tscn` pelo snapshot. Nas próximas, reabre a cena salva e conserva as edições.
4. **Salvar cena** grava `local/tristram.tscn`. Salvar e exportar são ações separadas.

Para atualizar o snapshot, execute `.\Godot-Editor.ps1 -Acao Preparar` em PowerShell na raiz do workspace. Isso atualiza a entrada local; a cena editada só é refeita ao escolher **Reconstruir a partir do snapshot…** no dock. Salve antes: a reconstrução guarda uma cópia da cena salva em `local/backups/`, mas essa cópia não inclui alterações ainda não salvas.

A preparação monta e valida os arquivos primeiro em `local/preparation/<rodada>/` e publica `local/town-snapshot.json` por último. Uma preparação recusada não deve ser confundida com uma nova cena pronta; `local/tristram.tscn` permanece preservada.

Opções do launcher PowerShell:

| Opção | Uso |
| --- | --- |
| `-InstallGodot` | Baixar o Godot portátil oficial 4.7.2 quando ausente; o SHA-256 oficial é conferido antes de extrair e executar. Exemplo: `.\Godot-Editor.ps1 -Acao Abrir -InstallGodot`. |
| `-GodotPath "D:\Ferramentas\Godot.exe"` | Usar um executável Godot já instalado. |
| `-DataDirectory "D:\Jogos\Diablo"` | Informar a pasta dos dados locais licenciados; a ponte v1 exige `DIABDAT.MPQ` do Diablo completo. |
| `-AllowPrototypes` | Escolher explicitamente trabalhar com a arquitetura de protótipo quando a baseline privada da cabana não está disponível. Exemplo: `.\Godot-Editor.ps1 -Acao Preparar -AllowPrototypes`. |

Sem `-AllowPrototypes`, a preparação exige a cabana selecionada por hash, com auditoria correspondente e adjunto de interior/fogo. A opção permite a colaboração sem compartilhar o modelo privado; não gera outro modelo, não promove um asset a aprovado e não comprova a revisão visual da cabana selecionada. Para mudar uma entrada já preparada, use **Preparar** explicitamente antes de reconstruir a cena no dock.

Modelos, texturas, snapshots, cenas, pacotes e fixtures ficam em `editor/godot/local/`, ignorado pelo Git. O cache `.godot/` também é ignorado. Não publique esses arquivos privados.

## Inspecionar e editar

Use a lista de instâncias ou **Selecionar cabana**. O dock mostra `instanceId`, asset, variante, revisão, hashes e estado do catálogo. **HERDADA** significa que a baseline continua em uso. Estado técnico, seleção local e aprovação visual são informações distintas; uma exportação não promove o asset a `accepted`.

A hierarquia `Architecture/<instanceId>` contém a geometria do snapshot separada por material, função e detalhe. Abra a área nativa **3D** do Godot para transformar nós, ajustar materiais e organizar a cena. A área **Tristram 3D** oferece uma prévia separada, seleção por clique e enquadramento. Ela usa um clone: sua escala de visualização não altera a cena salva nem os vértices exportados.

| Modo da prévia | Conteúdo |
| --- | --- |
| Resultado da cena | Geometria visível editada, incluindo um substituto quando importado. |
| Snapshot herdado | Baseline montada pelo jogo, incluindo recortes e interior presentes no snapshot. |
| GLB fonte (PBR) | GLBs importados como referência, com os materiais que o Godot consegue apresentar. |

O snapshot contém os triângulos reais montados pelo jogo; o GLB fonte mostra o modelo anterior aos adjuntos do executável. **Inspecionar GLB fonte vinculado** confere `sourceSha256` e aplica `sourceFit` quando esses dados estão disponíveis. Assim é possível comparar o modelo fonte com a cabana que já recebeu interior e recortes no jogo. Importar a fonte não reconstrói esses adjuntos dentro do GLB.

**Corte para interior** mostra o snapshot herdado, oculta seu exterior e usa um corte da câmera para revelar as faces reais da sala selecionada. É uma ferramenta de inspeção: a cena e a exportação conservam a geometria completa. Desmarque para voltar à vista inteira.

### Câmera e atalhos

Com o ponteiro sobre a prévia:

| Controle | Ação |
| --- | --- |
| Clique esquerdo | Selecionar arquitetura. |
| Botão direito + arrastar | Orbitar. |
| Botão do meio + arrastar | Deslocar a câmera. |
| Roda | Aproximar/afastar. |
| `F` / Enquadrar | Enquadrar a instância selecionada. |
| `Home` / Ângulo original | Restaurar azimute 45°, inclinação 30° e magnificação de referência. |

**Proporção vertical do jogo** aplica `height × 0,8164965809` somente ao clone da prévia. Uma unidade da cena salva continua sendo uma célula nativa: X=x, Y=height, Z=y do mapa. A câmera é ortográfica; usa o foco de referência de aproximadamente 45,2548 pixels por unidade, ajustado ao viewport e ao zoom da prévia. O enquadramento segue o objeto selecionado: não reproduz o mesmo frame, âncora do jogador, zoom nativo ou painéis da partida. `Home` no jogo aciona o backend original; a comparação de meshes exige geometria forçada na partida e revisão em 360°.

**Mostrar colisão nativa** controla a sobreposição das células bloqueadas. Essa camada é uma referência bloqueada, excluída da exportação. Quando o snapshot fornece `ground`, a hierarquia também contém **GroundReference**: referência plana de chão com as texturas reais de piso e sua transparência RGBA, igualmente bloqueada e excluída do pacote. Isso auxilia a leitura da posição dos edifícios; não autoriza editar o terreno nativo.

Mover, girar ou escalar a geometria não move colisões, portas, NPCs ou triggers. Conserve o vínculo nativo da instância e transforme seus nós; a raiz `Tristram` deve permanecer sem transformação.

## GLB, override e exportação

1. Selecione a instância de destino. **Importar GLB como referência…** cria um vínculo local excluído da exportação. A cópia fica em `local/sources/`, identificada por SHA-256.
2. **Importar GLB substituto…** adiciona a nova geometria e oculta a baseline. O ajuste inicial uniforme ao footprint precisa de revisão manual de escala, orientação e altura no editor **3D**.
3. Para substituir no jogo, marque **Substituir esta instância no jogo** e informe uma revisão própria, por exemplo `cabin-west-editor-r1`. A importação isolada não marca o override.
4. Salve a cena e clique em **Exportar pacote**. Somente as instâncias marcadas entram em `local/export/`; as demais continuam herdadas. O pacote contém `d3d-maps/tristram.ini`, arquivos `d3d-models/editor/<instanceId>.d3d` e `receipt.json`, com hashes dos bytes realmente escritos e validação técnica.

**Restaurar geometria herdada** volta a mostrar a baseline e desmarca o override. Os GLBs locais permanecem disponíveis para inspeção. Salve essa restauração; para alterar um pacote já aplicado, exporte e aplique novamente o conjunto desejado. Se nenhuma instância estiver marcada, o exportador informa essa condição e conserva o pacote anterior.

Erros de validação não substituem o pacote anterior. Uma nova exportação válida também preserva o pacote anterior em uma pasta `export.previous-…`. A revisão da fonte vinculada é conferida por hash; trocar o arquivo exige reimportação explícita.

A fonte também gera um recurso binário local `.scn`, evitando embutir suas texturas grandes como texto na cena. Conserve `local/sources/` junto da TSCN ao transferir uma edição. O pacote D3DMESH1 exportado já contém seu atlas e não depende desses recursos Godot.

### Limites desta versão

- **Cabana leste com fogo/luz nativos:** pode ser inspecionada, mas seu override está bloqueado. D3DMESH1 ainda não transporta o adjunto de luz/fogo; exportá-la perderia comportamento do jogo.
- **Malha estática:** de 1 a 20.000 triângulos por instância; posições finitas e triângulos não degenerados. X/Z exportados são relativos a `nativeMin`, entre −64 e 128; altura entre 0 e 64.
- **Material básico opaco:** `StandardMaterial3D` com cor base e, opcionalmente, textura albedo. Um atlas RGB de até 2048 × 2048 reúne os materiais sem redução automática de textura. Albedo texturizado exige UV1 em 0..1; UVs irrelevantes de materiais constantes são convertidos para seu texel no atlas.
- **Conteúdo recusado:** animação, rig, skin, morph targets, transparência/recorte alpha, shaders, emissão, partículas, luzes autoradas, mapas PBR adicionais, repetição/triplanar e extensões incompatíveis. Um GLB pode servir à inspeção mesmo quando sua exportação é recusada. Dependências externas precisam estar incorporadas ao GLB.
- **Aparência auxiliar:** materiais nativos ausentes no snapshot recebem cores técnicas. O formato não leva normais suaves/tangentes; o jogo recalcula normais por triângulo. Paleta indexada, iluminação, sombras e amostragem precisam de revisão no DevilutionX, mesmo após todos os testes técnicos passarem.

IDs duplicados ou desconhecidos, variante de outra instância, limites nativos alterados, revisão inválida e hash divergente causam erro. Não crie IDs para contornar a validação.

## Aplicar e testar no jogo

Após exportar, execute `Aplicar-Mapa-Godot.cmd` na raiz do workspace. A aplicação valida o pacote pelo loader real antes de instalá-lo em **`perfil-godot-review`**, com backup e recibo. Depois execute `Testar-Mapa-Godot.cmd`, que exige `build/devilutionx-tristram-godot.exe` e abre esse candidato no perfil separado. Se ele estiver ausente, compile-o pelo comando acima antes de continuar. Aplique com a partida fechada; o launcher não encerra processos por conta própria.

**Aplicar** calcula o hash do candidato antes de qualquer escrita no perfil. Se o executável estiver ausente, a aplicação para sem instalar parcialmente a baseline, a configuração ou os saves.

Antes de abrir a partida, **Testar** confere os hashes do executável escolhido, do manifesto e de todos os modelos contra o recibo aplicado. Se o executável ou o pacote mudar, reaplique para renovar a validação. O launcher usa os argumentos suportados pelo jogo, sem `--assets-dir`; o log fica em `perfil-godot-review/prototipo.log`. Para colaboração com protótipos, aplique explicitamente por `.\Godot-Editor.ps1 -Acao Aplicar -AllowPrototypes`.

O aplicador Python também aceita `--profile perfil-tristram` para uma aplicação explicitamente autorizada no perfil habitual. O padrão continua sendo `perfil-godot-review`; são os únicos dois destinos permitidos. Configurações, saves e a cabana selecionada existentes são preservados. A aplicação grava os modelos do pacote e seu manifesto, com backup e recibo; o perfil habitual usa a entrada existente **Iniciar-Tristram.cmd**.

Para validar um pacote de produção enquanto a partida permanece aberta, execute na pasta `devilutionx`:

```powershell
python tools/godot_bridge.py apply --profile perfil-tristram --pack editor/godot/local/tristram-production/export --executable ../build/devilutionx-tristram-v4.exe --validate-only
```

`--validate-only` confere formato, hashes, vínculos e geometria pelo loader real em `editor/godot/local/validation/`, sem gravar em nenhum perfil de jogo. Depois de fechar a partida, repita o comando sem `--validate-only` para aplicar: a validação inteira é refeita e a presença de qualquer processo do jogo é conferida imediatamente antes de gravar no destino. A ferramenta não encerra processos. Informe o executável realmente escolhido com `--executable`; o diagnóstico compilado também precisa corresponder à ponte atual.

Confira a revisão carregada, a geometria forçada na câmera original, o giro completo, materiais, contato com o chão, seleção e colisão. O perfil habitual e a aprovação artística da baseline não são substituídos pela validação do editor.

## Verificação técnica

Na pasta `devilutionx`, com o runtime portátil instalado:

```powershell
$godotEditorQA = '..\.tools\godot\Godot_v4.7.2-stable_win64_console.exe'
& $godotEditorQA --headless --path editor/godot --script res://tests/snapshot_smoke.gd
& $godotEditorQA --headless --path editor/godot --script res://tests/exporter_smoke.gd
& $godotEditorQA --headless --path editor/godot --script res://tests/editor_roundtrip.gd
```

`snapshot_smoke` e `exporter_smoke` usam fixtures sintéticas identificadas. `editor_roundtrip` exige o snapshot real preparado: confere a cabana herdada, importa a fonte vinculada quando disponível, salva/reabre uma cena separada e exporta apenas uma caixa sintética de teste no vínculo do poço. Seus arquivos ficam em `local/roundtrip/`; ele preserva `local/tristram.tscn`, `local/export/` e os perfis de jogo. Sem snapshot real, o teste informa a ausência e termina com erro, sem substituí-lo por fixture.

Depois de gerar esse pacote sintético, o teste de integração com o loader C++ pode ser executado na pasta `devilutionx`:

```powershell
python tools/test_godot_bridge.py
```

Esse teste requer os dados locais licenciados, a baseline privada selecionada, o candidato Godot e o diagnóstico C++ compilados, além de `local/roundtrip/export/`. Aceita `--data "D:\Jogos\Diablo"` e `--pack "D:\Pacotes\fixture"` para entradas explícitas. Ele cria um workspace descartável em `diagnostics/godot-bridge/<rodada>/`, com saves e configuração sintéticos, aplica o pacote pelo loader real e verifica recusas, backup e preservação dos perfis. Não instala a caixa no perfil de revisão do usuário nem altera o perfil habitual. A evidência da execução fica em `results.json` nessa pasta; sua aprovação continua sendo técnica.

Para capturar a prévia geométrica e o GLB fonte, rode `editor_roundtrip.gd` com backend gráfico:

```powershell
& $godotEditorQA --path editor/godot --script res://tests/editor_roundtrip.gd -- --render-preview
```

As imagens ficam em `local/roundtrip/preview-cabin-snapshot.png`, `preview-cabin-source-glb.png` e `preview-cabin-interior.png`. Os testes cobrem identidade, preservação da baseline, rejeições, atlas/hashes e salvar/reabrir; não são aprovação visual nem comprovam uma partida inteira em 3D. Registre o resultado efetivamente executado junto da revisão testada.

Validação executada em **8 de outubro de 2026**, com Godot **4.7.2**: snapshot sintético e real passaram; exportador passou **185 verificações**; ciclo real de salvar/reabrir/exportar passou **21 verificações**, incluindo orientação das 12 faces da caixa no formato do jogo. O verificador Python independente confirmou os bytes, atlas, INI, recibo e hashes do pacote. A cena real contém 14 grupos, 11.361 triângulos de arquitetura e 12.544 células de chão agrupadas em 503 malhas. Foram renderizadas as três vistas na Radeon RX 570, com backend OpenGL; aparência final e aprovação artística continuam dependendo da revisão no jogo.

A cena preparada `local/tristram.tscn` conserva todas as instâncias herdadas, sem override e sem a caixa sintética. O pacote de integração atualizado está em `local/roundtrip/export/`; use somente num perfil de diagnóstico, não como edição artística do poço.

A aplicação pelo loader C++ passou **15 verificações** em workspace descartável, incluindo ausência do candidato antes de criar qualquer perfil, rejeição de pacote parcial/inválido e preservação dos saves/configuração. `tools/test_godot_launcher.ps1` passou **8 fixtures** no Windows PowerShell 5.1, com lançamentos interceptados: arquivo válido, hashes alterados, candidato ausente, somente v4 antigo, recibo ausente e dados somente shareware. Nenhuma fixture abriu uma partida. A regressão C++ completa passou em 140.704 ms. O candidato validado tem SHA-256 `7985f3863cf793faf0e2d39f6ec8d2de7f252183eb0c35dc23607d433f2c9472`; o normal e o de qualidade locais receberam os mesmos bytes, sem modificar o perfil habitual.

### Conferência do contrato

A primeira redação da ponte continha variantes genéricas que divergiam do catálogo. O editor usa os IDs existentes: tavern `main-with-wing`, smithy `main-with-open-bay`, casa sudeste `farnham`, Pepin `pepin-hip-roof`, Adria `adria-open-shed`, catedral `exterior-with-bell-tower` e os estados cadastrados do poço/catacumbas. A cena real também estabelece `smithy-house.nativeMin = [60,56]`, por seu telhado conectado, e catacumbas `[48,18] → [50,21]`. Essas correções foram conferidas no contrato compartilhado e no catálogo, sem novas reservas ou promoção artística.
