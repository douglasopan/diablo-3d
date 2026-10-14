# Montagem de Tristram por instância

O addon `editor/godot/addons/map_builder/` acrescenta o dock **Montagem Tristram** ao editor já existente. Seu escopo é posição, orientação, escala uniforme, limites e revisão da montagem fixa. Reutiliza a **Tristram 3D**, os nós `d3d_instance` e os nomes de `assets/registry.json`; não cria outro editor ou catálogo. Personagens e níveis procedurais continuam em suas próprias frentes.

Preparado em 9 de outubro de 2026 como candidato local. A configuração compartilhada `project.godot` permanece preservada: a habilitação foi validada em uma cópia privada, e há um patch delimitado para integração pelo responsável do jogo. Os contratos de [edição](GODOT-EDITOR.md), [ponte](GODOT-BRIDGE.md) e [aprovação](ASSET-APPROVAL.md) continuam vigentes.

## Abrir e identificar a base

Habilite **Tristram Map Builder** em Projeto → Configurações do projeto → Plugins, mantendo o plugin **Tristram** habilitado. Abra a cena já existente, inclusive a cena de revisão de produção quando for essa sua edição. O addon não reconstrói nem salva a cena automaticamente.

Busque pelo nome do catálogo, ID, asset ou revisão. Selecionar um item sincroniza o Inspector e a prévia existentes. **Enquadrar** foca o item; **Ângulo original** usa a câmera de referência da prévia. Botão direito orbita, botão do meio desloca e roda aproxima/afasta.

| Origem selecionada | O que a prévia mostra | Edição |
| --- | --- | --- |
| **Cena em edição — candidato** | Nós visíveis da cena aberta, incluindo sua escolha local de GLB | Transformação na cena, com desfazer do Godot |
| **Base — snapshot identificado** | Snapshot existente escolhido explicitamente ou vinculado à cena | Somente leitura; carga em memória |
| **Aplicado em disco — recibo verificado** | D3D indicados pelo recibo, montados sobre o snapshot escolhido | Transformação candidata em memória; não muda a cena nem o perfil |

**Base** não significa arte original. O antigo `local/town-snapshot.json` auditado contém arquitetura procedural e a cabana importada, anterior ao lote de 12 vínculos. A cena de produção também contém candidatos e placeholders, conforme [TRISTRAM-MAP-BATCH.md](TRISTRAM-MAP-BATCH.md). Nenhum deles deve ser apresentado como reprodução automática do pacote hoje instalado.

**Snapshot de comparação…** lê outro snapshot já preparado sem substituir a cena aberta. O painel identifica seu caminho/hash e avisa quando a cena e a comparação têm bases diferentes. O leitor existente exige texturas PNG; dumps diagnósticos com PPM precisam de uma derivação local sem perda e com linhagem, preservando triângulos e colisão. A derivação privada validada nesta entrega registra os hashes de entrada/saída e a igualdade dos bytes RGB das 13 texturas.

Para comparar origens na mesma perspectiva, alterne o seletor sem clicar novamente em Enquadrar: yaw, pitch, zoom e alvo da mesma prévia são preservados. Enquadre quando precisar localizar outra instância. A proporção vertical auxiliar pertence à prévia, não à cena exportada.

## Conferir o que está aplicado

**Carregar recibo aplicado…** pede um `godot-pack-receipt.json` existente. Antes de declarar **aplicado em disco**, a ferramenta verifica:

- SHA-256 do recibo, manifesto e de todos os D3D vinculados;
- cobertura exata das chaves `Scene.instance`, seções e entradas do recibo, sem duplicatas;
- asset, variante e limites nativos contra os IDs da cena;
- caminhos restritos a `d3d-models/editor/`, sem travessia de diretórios.

A leitura do D3D reutiliza o leitor do inspetor de modelos. Recibo, manifesto e modelos são revalidados antes de comparar novamente ou salvar uma proposta de aplicado. Alterações exigem recarga explícita; uma verificação recusada não conserva o rótulo aplicado anterior.

Isso comprova os **arquivos em disco**, sem comprovar a cena em cache da partida aberta. O executável registrado no recibo pode ser anterior ao instalado. O painel não troca executáveis, não encerra processos e não atualiza recibos. Luz/paleta do Godot são auxiliares; resíduos de cenário nativo fora do snapshot podem não aparecer. A revisão na partida exige geometria forçada, perspectiva equivalente e órbita. **Home no jogo usa o backend original**, portanto não prova fidelidade da malha.

Na taverna auditada, o modelo aplicado corresponde às duas partições disjuntas do mesmo conjunto:

| Instância | Triângulos | SHA-256 do D3D |
| --- | ---: | --- |
| `tavern-main` | 7.361 | `11cf73e7ea0b2779e8a6c8b989ea5ad0c062c518ece1e2674bc776d2093d88a0` |
| `tavern-wing` | 4.081 | `a0adba330232fcd785e0640dcc76a11067ace1347cf326eac6870def0c7efc62` |

Não aplicar o master inteiro aos dois vínculos. A correção delimitada dos resíduos diante da taverna pertence ao candidato do renderizador preparado pelo responsável do mapa; este addon não oculta células ou triângulos nativos por aproximação.

## Transformar e restaurar

O painel mostra identidade, revisão, hashes, vínculo nativo, transformação, limites visuais e altura mínima em relação ao chão `Y=0`. Uma unidade corresponde a uma célula: X=x, Y=altura, Z=y.

Informe posição, rotação em graus e **uma escala uniforme**, depois escolha **Editar transformação**. Valores não finitos, escala negativa/espelhada ou fora de 0,01–100 são recusados. Propostas também recusam escala anisotrópica herdada de ancestrais. Não há ajuste automático para todos os edifícios.

**Restaurar transformação** volta ao transform de base da instância. Conserva modelos, materiais, revisão escolhida e os demais nós. Se um ancestral tiver sido transformado manualmente, seu efeito continua identificado nos limites e no aviso; restaurar só a instância não desfaz uma edição de ancestral.

Mover a malha **não move colisão/SOL, portas, triggers, NPCs ou navegação**. O aviso aparece quando a transformação visual efetiva diverge do vínculo nativo. A raiz Tristram deve permanecer sem transformação. A cabana leste congelada e nós marcados somente leitura têm esses controles bloqueados.

As aprovações parciais existentes da cabana e do exterior da Catedral são preservadas nos documentos responsáveis. A ausência de aceitação integral ou o estado candidato de outra revisão não cancela esse escopo. Novas escalas, alturas e limpeza de resíduos precisam de revisão delimitada; não autorizam regeneração de modelos.

## Salvar e revisar uma proposta

Informe revisão própria (`a-z`, números e hífen, até 64 caracteres), nota e estado de revisão. **Salvar proposta local** grava um JSON novo em `local/map-builder/proposals/<instanceId>/`, com hash do arquivo final. Não sobrescreve propostas anteriores.

O documento `d3d.map-builder-proposal` versão 1 contém identidade/revisão, snapshot e cena de base identificados por hash, linhagem de preparação, transformação local e efetiva, limites, hash da montagem, colisão fixa, seleção visível e recibo aplicado quando houver. O fingerprint inclui transformações/visibilidade ancestrais, geometria, transformação dos nós e propriedades do material básico/textura, incluindo dimensões/formato e bytes. Uma mudança invalida a revisão associada ao hash anterior.

| Estado da revisão | Regra |
| --- | --- |
| Pendente | Padrão; salvar não cria aprovação |
| Montagem revisada | Responsável identificado; use a nota para escopo/evidência |
| Ajuste solicitado | Responsável identificado; use a nota para o defeito |
| Montagem aprovada | Responsável e escopo/evidência obrigatórios; vale para o hash exato |

A aprovação é de **montagem por instância**, separada da aceitação artística de `assets/registry.json`. Trocar instância/origem ou editar a transformação volta à revisão pendente. Se a malha/material mudar após selecionar um estado de revisão, o salvamento exige nova revisão explícita.

Mesmo com montagem aprovada, o documento continua `state: candidate`, `exported: false`, `installed: false`. Não altera `d3d_override`, o catálogo ou a seleção da baseline. **Ctrl+S** salva a cena atual; a proposta em JSON é um registro separado. O JSON registra transformações e hashes, **não serializa a malha ou os materiais editados**. Salve a cena separadamente antes de registrar uma proposta que dependa dessas edições.

Exportação e aplicação seguem os controles e contratos existentes, com revisão explícita, validação, backup e recibo. O addon não exporta/aplica propostas automaticamente, e o exportador existente **não consome este JSON**. No modo **Cena**, salve a cena correta, marque explicitamente o override, informe sua revisão e use a exportação correspondente. No modo **Aplicado**, a transformação candidata pertence ao clone em memória: confira os hashes de base e transponha deliberadamente essa transformação para a cena conferida antes de salvar/marcar/exportar. O botão de exportação existente continua operando sobre a cena aberta, não sobre o clone aplicado da prévia.

## Validação e limites

A fixture `addons/map_builder/smoke.gd` exige a cópia privada `diagnostics/tristram-map-builder-20261008/tool/`. Lê snapshot e recibo reais, verifica as duas partições da taverna e cria propostas somente na cópia. Foram **50 verificações sem falha**: escala uniforme/finita, ancestrais, textura, restauração, hashes, estados, colisão fixa, cobertura e rejeição de recibo/caminho divergentes, incluindo preservação byte a byte da cena e snapshot de entrada.

O teste complementar do editor executou **22 verificações sem falha**, usando o plugin real e os callbacks dos controles em modo headless: seleção, enquadramento, transformação, aviso de colisão fixa, restauração, troca de origens, proposta local e bloqueio da cabana. Logs e recibos ficam em `diagnostics/tristram-map-builder-20261008/tool/`, junto do patch de habilitação, hashes/base e derivação PNG. Nenhum teste altera build compartilhado, perfil de jogo, seleção artística ou colisão.

O renderer dummy headless não produz screenshot visual válido do dock. Esta validação comprova integração/callbacks e dados; a legibilidade física do dock, a inspeção artística por órbita e a comparação final na partida permanecem para revisão humana. O painel não mostra todos os resíduos nativos, não oferece edição de malha/triângulos e não promete equivalência da iluminação do jogo.

A fixture cobriu os **14 grupos de arquitetura** do snapshot, não a cena ampla de produção com suas centenas de âncoras/candidatos. O fingerprint lê geometria e bytes de textura; o custo de abrir/revisar essa cena ampla ainda não foi medido. Não há benchmark, teste GPU ou comprovação de fluidez dessa carga nesta entrega.
