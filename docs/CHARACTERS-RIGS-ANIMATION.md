# Personagens, rigs e animações do Diablo 3D

Atualização: **8 de outubro de 2026**. Documento responsável da frente de personagens, subordinado a [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md). O objetivo abrange o Diablo inteiro e os níveis procedurais; Tristram fornece o primeiro piloto. Não substitui a fila central, o catálogo ou a aprovação artística.

## Estado comprovado e responsabilidade

**Decisão vigente do usuário em 8 de outubro: migrar para [Tripo](https://developers.tripo3d.ai/) após um último lote com o saldo Meshy existente.** Após ordenar a suspensão, o usuário autorizou expressamente gastar os 258 créditos restantes, sem recarga. A divisão inicial foi 150 cenário / 108 personagens. O QA rejeitou dois rigs antes de enviá-los; o principal transferiu seus 10 créditos para o mundo. **Tetos finais: 160 cenário / 98 novos personagens**, sem contar novamente os 62 já debitados antes. Nenhum guerreiro, animal ou vista nova entrou nesse lote. As submissões desta frente foram encerradas após comprometer os 98; somente GET/download e revisão permanecem. O principal prepara integração/credencial segura e contrato Tripo; nenhuma capacidade ou equivalência foi presumida. O orçamento anterior de cobrir toda Tristram com Meshy foi substituído por essa decisão.

| Componente | Estado | Limite |
| --- | --- | --- |
| Simulação, estados, ações e tempos | Nativos, auditados por código/dados | A animação visual nunca determina dano, movimento, colisão, RNG, saves ou rede. |
| Atores na partida 3D atual | Proxies/projeções derivados dos sprites | Não há loader de skin/clips integrado nem suporte esquelético geral demonstrado. |
| Godot | Editor externo e ferramenta de revisão | Não é a engine do jogo; salvar uma cena não instala um personagem. |
| Ponte estática | D3DMESH1 preserva posições/UV/textura RGB | Não transporta ossos, pesos ou animações. O exportador Godot rejeita esses recursos. |
| Primeiro candidato | Griswold existente, reutilizado por tarefa/revisão/hash | Piloto técnico; revisão artística e integração na partida permanecem separadas. |
| Guerreiro | Próximo corte de herói depois do piloto e auditoria histórica | Nenhuma geração duplicada autorizada por simples ausência no lote atual. |

O chat principal coordena guia, runtime, CMake, build, instalação, Git e janela de integração. Esta frente escreve este documento e evidências privadas em `diagnostics/characters-rigs`, sem alterar renderer/import/options ou os perfis habituais. A entrada do jogador continua **`D:\Diablo 1 3D\Iniciar-Tristram.cmd`**. A cabana selecionada está congelada.

## Handoff, identidade e custos

O usuário autorizou os itens da lista de [TRISTRAM-MESHY-PRODUCTION.md](TRISTRAM-MESHY-PRODUCTION.md). Após alerta explícito sobre duplicação, a frente do mundo transferiu os personagens/animais/heróis e suas tarefas existentes. A transferência está em `diagnostics/tristram-production-20261008/actors/handoff.json`; a reserva desta frente, em `diagnostics/characters-rigs/ownership.json`. O mundo continua arquitetura/vegetação/cenário. Somente esta frente cria novos POSTs dos IDs de personagens, após conciliar tarefas, etapa e variante.

O handoff contém oito tarefas de vistas (Griswold, Cain, Pepin, Adria, Ogden, Gillian, Farnham, Wirt), **24 créditos reais**, e o modelo de Griswold, **30 créditos reais**: total herdado **54**, sem pagar novamente por esses resultados. Valores retornados por `consumed_credits` prevalecem sobre estimativas anteriores de nove créditos por vista. Nenhuma compra de créditos está autorizada; intenção de reposição pelo usuário não é saldo disponível.

Antes do primeiro rig, a auditoria percorreu todo `models/meshy` e todos os GLBs de `models`, incluindo as cabanas anteriores ao lote: 51 metadados de tarefa e 36 GLBs naquele instante, nenhum skin/clip local. As listagens remotas paginadas de rig/animação estavam vazias. Esses números são um retrato datado, não uma condição futura. Resultado privado: `local-inventory.json` e `account-rig-audit.json`. O saldo lido foi 299 créditos; outras frentes compartilham essa conta.

| Campo | Griswold fonte |
| --- | --- |
| ID / variante | `tristram.actor.griswold` / `default` |
| Revisão | `actor-griswold-meshy-r1` |
| Tarefa existente | `01a11a8c-388f-73c2-9875-b1601bbbca6f` |
| GLB SHA-256 | `33b11e3550f3a1bc2455b002dfcfea9b7d8f4ce172b8ccdf3b39b4e2d82258e8` |
| Geometria medida | 4.971 triângulos; 8.031 vértices com costuras; 1 material |
| Extensão glTF X/Y/Z | 1,070756 / 1,901734 / 0,518607 unidades |
| Fonte privada | `models/meshy/tristram-20261008/actor-griswold-model-r1/downloads/model.glb` |
| Aprovação | Candidato, não aceito nem instalado |

O volume medido afasta o defeito de folha encontrado em outro asset; não comprova anatomia ou fidelidade. A primeira inspeção identificou rosto e acessórios que exigem revisão. Manter o master pré-remesh, OBJ/MTL, texturas e GLB originais. Complementos laterais/traseiros são inferidos, não recuperados do original. ID, revisão e hash são obrigatórios em cada resultado; nunca escolher o arquivo mais recente.

### Último lote excepcional, teto final 98 novos créditos

| ID / variante | Modelo já submetido | Modelo / rig / clip reservados |
| --- | --- | --- |
| `tristram.actor.ogden` / `default` | `01a11a9b-7ffb-770d-a002-f99a1659bb9d` | 30 / 5 / 3 |
| `tristram.actor.gillian` / `default` | `01a11a9b-947e-720d-9834-f54811537eec` | 30 / 0 / 0 |
| `tristram.actor.adria` / `default` | `01a11a9b-ab67-7505-a9f8-6ca22223eb53` | 30 / 0 / 0 |

Novos modelos: **90 reais**; rig de Ogden: **5 reais**; único idle adicional Ogden: **3 reais**. Total do lote **98 consumidos**, sem tarefa pendente; os 62 anteriores ficam excluídos dessa soma. Total histórico desta frente: **160**, sem incluir cenário e sem descontar duas vezes o Griswold. Pepin com braços cruzados, Cain com cajado/chapéu inferido e Farnham/Wirt em poses/anatomias especiais continuam na fila para ferramentas adequadas. Não reenviar os oito conjuntos de vistas. Geometria usa vistas limpas já pagas; textura tem referência nativa primária. Tudo permanece candidato. `final-meshy-lot-ledger.json` soma uma única vez custo real ou reserva pendente por tarefa; `final-lot-authorization.json` fixa IDs e teto e foi fechado para novos POSTs.

**QA antes de gastar com rig:** Ogden tem mãos/braços/pernas/pés distinguíveis, 5.202 triângulos na fonte e passou salvar/reabrir em 21 verificações Godot; seu rig `01a11aa1-ebe2-765f-bf9f-63938bb5a100` passou validação de skin e revisão rig/walk/run: 24 bones, até quatro influências, 106 verificações e 24 capturas, sem falhas. O avental deforma como skin, sem cloth physics. A base só tem uma pose `clip0`; idle adicional `01a11aa9-5947-73a1-a7b0-0b57247e47ea` foi submetido após essa validação e está concluído/baixado. GLB idle SHA `ff6bed48aa22ea8cbd415e1ba5931a397189cb09b5b81acc61e826314f0e306f`, 121 keys originais, end time 4,033333 s, cerca de 30 Hz. Passou 36 verificações/8 capturas e 19/4 da cena interativa, sem falhas. A postura flexionada e viradas amplas demonstram skin, mas **a sequência nativa de Ogden (111 entradas / 16,65 s) continua não reproduzida**.

Gillian tem 4.688 triângulos e duas mãos/pés aparentes, mas cortes horizontais do GLB mostram ligação de uma extremidade da mão à saia e uma saia contínua que não demonstra pernas/joelhos internos. Adria tem mãos/robe unidas e provável braço ligado ao torso. Ambos ficaram **inelegíveis ao rig automático desta rodada** por defeito geométrico documentado; nenhum POST de rig foi enviado para eles. Elegibilidade inicial de Gillian foi corrigida antes de gastar, após inspeção mais precisa. Os masters/candidatos foram preservados para correção. Isso não equivale a aceitação artística nem impede uma correção futura adequada.

## Famílias e cobertura

| Família | Contrato de reuso | Ações/cobertura |
| --- | --- | --- |
| Heróis humanos | Um mapa semântico comum; proporção/bind pose próprios por classe; roupas compatíveis com a mesma variante do rig | Warrior/Rogue/Sorcerer; equipamentos, armas e estados nativos completos. Não criar um modelo por direção. |
| Moradores bípedes | Reaproveitar semântica humanoide onde a anatomia permitir | Griswold/Pepin/Ogden/Cain/Farnham/Adria/Gillian; identidade, postura, ferramentas e loop próprios. |
| Wirt / morador ferido | Variante anatômica e hierarquia próprias quando necessário | Não impor marcha simétrica, prótese ou transições inexistentes no original. |
| Vaca e demais quadrúpedes | Rig próprio de quadrúpede; reutilizar somente entre espécies/proporções verificadas | A vaca tem oito direções originais e três instâncias; não é elegível à API humanoide por suposição. |
| Monstros bípedes | Esqueleto por família anatômica compatível, não por nome isolado | Esqueletos, mortos-vivos, demônios e demais tipos precisam de comparação de ações e proporções. |
| Não humanoides, voadores e bosses | Rig por topologia locomotora, apêndices e comportamento | Caudas, asas, múltiplos membros, rastejamento e ações especiais não cabem automaticamente no humanoide. |
| Objetos animados / efeitos | Permanecem sistemas de objetos/mísseis/efeitos | Não transformar porta, cadáver, portal ou projétil em herói esquelético. |

Hellfire é cobertura condicional: Monk/Bard/Barbarian e Lester/Celia/Nut continuam separados da validação do Diablo original. Bard/Barbarian reutilizam arte-base Rogue/Warrior antes das traduções; não pagar duplicação cega. A relação completa de monstros e ações deve derivar dos TSV/estados reais, inclusive ações especiais, e ampliar os IDs do catálogo em coordenação quando a fase correspondente começar. Não declarar o inventário de Tristram como biblioteca completa do jogo.

## Contrato de rig humanoide v1 — proposta para o piloto

Este contrato é uma decisão de produção a validar no candidato, não uma capacidade já implementada no renderer.

- **Unidades/eixos:** GLB glTF 2.0 em metros, Y para cima, frente +Z, mão direita; escala positiva aplicada uma única vez. Pivô de instância no chão entre os pés, separado do quadril. O rig de Griswold usa altura de autoria 1,8 m como normalização, sem alegar medida canônica. Escala no mundo será calibrada contra sprites/mapa reais.
- **Bind pose:** guardar a pose recebida e as matrizes inversas de bind exatas. Medir A/T pose, rotações e proporções antes de retarget; um nome de osso igual não prova compatibilidade. O master deve permitir abrir braços/pés e corrigir cotovelo/joelho sem fundir arma ao corpo.
- **Semântica mínima:** `root`, `pelvis`, coluna, pescoço/cabeça; clavícula, braço, antebraço e mão L/R; coxa, canela, pé e ponta do pé L/R. Registrar correspondência dos nomes reais (por exemplo Mixamo) num mapa versionado. Dedos, face, cabelo, roupa e ossos auxiliares são opcionais por família; não inventar correspondência ausente.
- **Attachments:** sockets de mão esquerda/direita, escudo, arma guardada, costas/cintura e olhos/cabeça. Armas, escudos e peças rígidas independentes mantêm pivô/transform próprio. Visual equipado deriva da classe e slots nativos, sem acrescentar ataques ou alcance. Acessórios já fundidos ao candidato exigem separação/correção no master.
- **Retarget:** alinhar bind, proporções, pole vectors de joelho/cotovelo e pés; preservar comprimento de membros. Validar idle, contato dos pés, punhos, arma/escudo e morte em frente/perfil/costas, incluindo câmera baixa. Compartilhar clips apenas após esse teste.
- **Skin:** até quatro influências normalizadas por vértice, índices válidos, matrizes finitas e hierarquia acíclica; nenhuma escala negativa ou correção escondida em um pai. Limite inicial proposto de 96 joints por skin, medido antes de ampliar; não é um limite comprovado da engine.
- **Orçamento inicial proposto:** NPC até 5 mil triângulos, herói 8–12 mil incluindo vestimenta base, anexos até 3 mil, textura base 1–2K por família/peça quando a revisão permitir. Masters 4K ficam privados; redução é derivada e identificada por hash. Bosses e não humanoides precisam de orçamento próprio. Medir memória/CPU/GPU em cena antes de tornar esses valores uma promessa de desempenho.
- **Materiais:** albedo sem sombras/luz direcional pintadas que conflitem com a iluminação do jogo; validar normal/roughness/metallic e partes ocultas em 360°. Não promover material PBR só porque a API o devolveu: a ponte atual usa textura RGB.
- **Exportação:** um GLB com mesh, skin, IBMs, joints, materiais/texturas e animações nomeadas; conservar master e versão da ferramenta. Animações separadas são permitidas como fontes de retarget, mas o candidato revisável precisa de dependências explícitas e abrir sem URLs remotas.

## Autoridade, sincronização e eventos

A leitura visual contém entidade nativa, classe/tipo, equipamento, estado, direção, posição/subpasso, frame efetivo, comprimento/delay e flags de velocidade. Não escrever de volta nos dados nativos. O frame de renderização já pode ser interpolado pela engine; ler a API efetiva em vez de inventar um contador de segundos independente.

O jogo usa ticks, não um único FPS universal de animação. Na configuração padrão são 20 ticks/s; idle/walk/attack/hit/block/spell/death têm delays e regras diferentes. Walking tem frames iniciais/distribuição especiais; ataques/recuperação mudam com flags. As tabelas por classe e transições devem ser auditadas antes de calcular durações. Não usar simplesmente `número de frames / FPS` como duração autoritativa de toda ação.

### Dados nativos auditados

Fonte: `assets/txtdata/classes/<classe>/animations.tsv` e `Source/player.cpp`, `player.h`, `engine/animationinfo.*`. Os números abaixo são os valores-base ativos nesses TSV; variantes e modificadores vêm a seguir. A auditoria privada preserva caminho/linha/hash por célula. **20 ticks/s é o padrão, não a duração de todo clip.** `gnTickDelay` efetivo é calculado por divisão inteira; pausa e troca de estado conservam autoridade.

| Classe | Idle dungeon/town N | Walk dungeon/town N | Hit N | Block N | Death N | Spell N / índice SF |
| --- | --- | --- | --- | --- | --- | --- |
| Warrior | 10 / 20 | 8 / 8 | 6 | 2 | 20 | 20 / 14 |
| Rogue | 8 / 20 | 8 / 8 | 7 | 4 | 20 | 16 / 12 |
| Sorcerer | 8 / 20 | 8 / 8 | 8 | 6 | 20 | 12 / 8 |
| Monk, condicional Hellfire | 8 / 20 | 8 / 8 | 6 | 3 | 20 | 18 / 13 |
| Bard, condicional Hellfire | 8 / 20 | 8 / 8 | 7 | 4 | 20 | 16 / 12 |
| Barbarian, condicional Hellfire | 10 / 20 | 8 / 8 | 6 | 2 | 20 | 20 / 14 |

Ataques abaixo são **N / ActionFrame A**, não duração / FPS. Melee/arco: swing em A−2 quando aplicável, impacto/flecha em **A−1**, encadeamento em **A**. Magia em **SF**, sem subtrair 1. Warrior sword: 16 / 9 → swing índice 7, impacto 8, encadeamento 9. Não usar a mesma convenção para magia.

| Classe | Unarmed | +shield | Sword | +shield | Bow | Axe | Mace | +shield | Staff |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Warrior | 16/9 | 16/9 | 16/9 | 16/9 | 16/11 | 20/10 | 16/9 | 16/9 | 16/11 |
| Rogue | 18/10 | 18/10 | 18/10 | 18/10 | 12/7 | 22/13 | 18/10 | 18/10 | 16/11 |
| Sorcerer | 20/12 | 16/9 | 16/12 | 16/12 | 20/16 | 24/16 | 16/12 | 16/12 | 16/12 |
| Monk | 12/7 | 12/7 | 16/12 | 16/12 | 20/14 | 23/14 | 16/12 | 16/12 | 13/8 |
| Bard | 18/10 | 18/10 | 18/10 | 18/10 | 12/11 | 22/13 | 18/10 | 18/10 | 16/11 |
| Barbarian | 16/9 | 16/9 | 16/9 | 16/9 | 16/11 | 20/8 | 16/8 | 16/8 | 16/11 |

| Estado | Ticks/frame | FPS nominal a 20 ticks/s | Extensão nominal N×delay a 20 ticks/s | Loop, transição e interrupção |
| --- | --- | --- | --- | --- |
| Stand | 4 | 5 | 0,2 N segundos | Loop; ação pode trocar enquanto parado. Fase de entrada randomizada nativamente, sem novo RNG visual. |
| Walk | 1 | 20 | 0,05 N segundos | Passo com confirmação de tile/retorno no fim; começa em −2 (+1 conforme fase do dispatcher), ou 2 ao correr em town. |
| Attack / ranged | 1 | 20 | 0,05 N segundos | Não loop; último retorna Stand; encadeia em frame≥A; impacto é anterior. Durabilidade/interrupções nativas podem encerrar. |
| Spell | 1 | 20 | 0,05 N segundos | Não loop; som na entrada, emissão SF, encadeia em frame≥SF. |
| Hit / recovery | 1 | 20 | 0,05 N segundos | Último retorna Stand; recovery impede troca voluntária, flags pulam 1/2/3 frames. Som pode ocorrer sem iniciar recovery. |
| Block | 3 | 6,6667 | 0,15 N segundos | Último detectado sem consumir o delay final completo; FastBlock começa N−2; som na entrada. |
| Death | 2 antes do hold | 10 antes do hold | 0,1 N segundos antes do hold | Pose final retida; delay passa a 100, UI de morte depende de contador nativo, não do GLB. |

As extensões são a soma nominal de slots; o dispatcher testa saídas antes de avançar, pode avançar no tick de entrada e encadear ações no mesmo tick. Exemplo derivado, não captura de combate: Warrior sword sem flags e com avanço na entrada tem impacto 400 ms após o tick de entrada, encadeamento 450 ms e saída 750 ms; **não impor os 800 ms nominais ao modo**. FastestAttack pula quatro frames quando inclui o primeiro; a distribuição visual nativa também preserva cauda de ataque repetido. Idle dungeon do Warrior/Barbarian com arco tem 8 frames e morte com arco/armadura pode ter 15. Morte/feitiço em town também mudam postura gráfica. Consulte valores ativos do ator, não uma cópia congelada dos TSV.

### NPCs e vacas

Fontes: `assets/txtdata/towners/towners.tsv:2–13`, `Source/towners.cpp:818–844`. Esses atores usam `_tAnimCnt`, `_tAnimDelay`, `_tAnimFrame` e `_tAnimFrameCnt`, **não AnimationInfo**. A ordem inclui repetições/reversões quando presente. São ciclos estáveis exatos ao padrão, salvo freeze/quest/mudança de animação.

| Ator | Frames disponíveis | Ticks/entrada | Entradas do ciclo | Ciclo estável a 20 ticks/s |
| --- | --- | --- | --- | --- |
| Griswold | 16 | 3 | 130 ordenadas | 390 ticks / 19,5 s |
| Pepin | 20 | 6 | 108 ordenadas | 648 / 32,4 s |
| Ogden | 16 | 3 | 111 ordenadas | 333 / 16,65 s |
| Cain | 25 | 3 | 59 ordenadas | 177 / 8,85 s |
| Farnham | 18 | 3 | 54 ordenadas | 162 / 8,1 s |
| Adria | 19 | 6 | 141 ordenadas | 846 / 42,3 s |
| Gillian | 18 | 6 | 18 sequenciais | 108 / 5,4 s |
| Wirt | 20 | 6 | 20 sequenciais | 120 / 6 s |
| Wounded Townsman | 8 | 6 | 8 sequenciais | 48 / 2,4 s; quest pode congelar e trocar para cadáver de 1 frame |
| Vaca, cada uma das três instâncias | 12 | 3 | 12 sequenciais | 36 / 1,8 s; direção/fase inicial nativas |

Griswold inicia frame 0, mas a primeira troca após três ticks busca `order[1]=5`; a ordem completa de 130 entradas está em `griswold-native-sequence.json`. Não chamar a animação recebida da biblioteca de reprodução da forja. Mudar a duração de um idle genérico para 19,5 s também não recupera suas poses.

### Monstros e ações especiais

A auditoria cobre **112 tipos base em 34 famílias de sprites**, 138 registros Hellfire em 57 famílias e 100 mapeamentos de monstros únicos para seus tipos-base. Não são 250 monstros distintos, nem esqueletos aprovados. Os 1.500 descritores privados (duas configurações × seis slots) distinguem N=0, slot preenchido e ação realmente habilitada. `availability=Never` não exclui Butcher, Skeleton King, Golem ou Diablo: têm rotas especiais.

As famílias base por `assetsSuffix` são: `acid`, `bat`, `bigfall`, `black`, `darkmage`, `demskel`, `diablo`, `falspear`, `falsword`, `fat`, `fatc`, `fireman`, `gargoyle`, `goatbow`, `goatlord`, `goatmace`, `golem`, `mage`, `magma`, `mega`, `rhino`, `scav`, `skelaxe`, `skelbow`, `skelsd`, `sking`, `snake`, `sneak`, `succ`, `thin`, `tsneak`, `unrav`, `worm`, `zombie`. Reuso de rig exige avaliar anatomia e ações dentro/entre esses grupos.

| Slot/modo | Tempo e marcador | Loop/transição relevante |
| --- | --- | --- |
| Stand / Delay / Talk | N/rate por tipo; Golem Stand usa Walk | Stand loop; Delay tem contador próprio e conserva frame; Talk segue áudio/quest. |
| Walk, três modos | Walk N/rate; entrada −1 | Confirma tile no fim e pode encadear com AI no mesmo tick. |
| Melee / ranged | Attack N/rate; ação/som A−1 | Último retorna Stand; ranged pode criar múltiplos projéteis. |
| Special melee / stand / ranged | Special N/rate; SA−1 quando alcançável | Ranged requer tickCounter=0; Mega segura a pose até seu contador permitir. |
| Fade / heal | Special normal/reversa e flags nativas | Fade altera visibilidade; Gargoyle cura com reversa/hold em frame 0. |
| Hit / death | GotHit/Death N/rate | Hit pode interromper; morte cria cadáver/remove entidade no marco nativo. Diablo usa sequência distinta. |
| Petrified | Pose visual anterior recuperada do efeito | Congela avanço e fração; não iniciar idle independente. |
| Charge | Fase/posição do míssil Rhino e estado original | Rhino usa Special; Snake Attack com entrada 7; Gloom Walk. Não substituir por root motion. |

Magma adiciona impacto no índice 8; Storm no 12; Snake som no 0. Diablo Death depende de `var1=140` ticks (7 s no padrão), não do fim do clip. SA=0 gera comparador −1 normalmente inalcançável; não inventar evento no primeiro frame. Os CSV/JSON privados guardam cada N, delay, A/SA, fonte/linha e exceções. Fontes principais: `Source/monster.cpp`, `monster.h`, `missiles.cpp`, `tables/monstdat.cpp` e `assets/txtdata/monsters`.

| Ação visual | Loop/transição | Evento a seguir |
| --- | --- | --- |
| Idle | Loop nativo; transição conforme novo estado | Sequência/frame nativo, inclusive sequências não lineares de NPCs. |
| Walk | Fase da marcha vinculada ao subpasso/frame nativo | Posição e deslocamento de caminhada já calculados pela engine. |
| Ataque corpo a corpo / arco | Não loop; encadeamento/interrupção somente onde o estado nativo permitir | Contato/projétil nos frames nativos; a animação nunca chama dano. |
| Magia | Não loop; separar preparo/soltura/recuperação visual | Disparo de magia usa o marcador próprio do código, não o mesmo índice do ataque. |
| Hit / recovery / block | Usar duração/flags reais; sem atrasar o retorno à simulação | Reação segue evento nativo; blend não esconde bloqueio ou antecipação. |
| Death / corpse | Não loop; manter pose final quando a engine mantiver cadáver | Morte/remoção continuam nativas; sem reviver por término do clip. |
| Especiais de monstro | Mapeamento por estado/tipo | Charge, fade, heal, summons, ataques adicionais e sons permanecem no código original. |

Marcadores de impacto/projétil/som dos clips são metadados de alinhamento e revisão. Os eventos reais continuam disparados pela simulação; não reproduzir som duas vezes. Resampling de um clip de 30/60 FPS serve à interpolação visual, sem alterar ticks ou protocolo.

**Root motion:** a instância recebe a posição autoritativa e `GetOffsetForWalking` existente. Remover deslocamento planar acumulado da raiz do clip ou usar clips in-place, preservando o offset de bind; nenhuma animação move colisão/pathfinding. Pés e quadril ainda precisam de revisão após essa remoção. **Direção:** a direção nativa de oito setores continua autoritativa; yaw pode interpolar visualmente entre leituras, sem mudar disparo, facing de gameplay ou geometria de seleção nativa.

## Câmeras e corpo do jogador

O chat de horizonte/câmera deve receber o mapa semântico de cabeça/olhos e os attachments por revisão. Altura dos olhos é medida no modelo normalizado e transformada pelo fit de mundo; não hardcode uma medida lore ainda não comprovada. Em primeira pessoa, ocultação de cabeça/capacete e eventual corpo próprio é uma decisão de desenho da instância visual; preservar sombras, outros jogadores, seleção e simulação. Nenhuma câmera passa a controlar dano ou alcance por orientação do mesh. A entrega inicial precisa ser legível na câmera original, em 360°, de frente e em ângulo baixo.

## API Meshy e candidato externo

A [API de rigging](https://docs.meshy.ai/en/api/rigging) documenta humanoides bípedes texturizados, frente +Z, input por ID ou GLB URL e saída GLB/FBX com skin. Rig custa 5 créditos. Caminhada/corrida básicas são saída opcional; verificar a resposta antes de pagar novamente por elas. Não há idle garantido. A [API de animação](https://docs.meshy.ai/en/api/animation) custa 3 créditos por ação; o catálogo real é consultável gratuitamente. IDs não devem ser inferidos de exemplos ou comentários desatualizados. [Preços oficiais](https://docs.meshy.ai/en/api/pricing).

O [Help Center](https://help.meshy.ai/en/articles/16231707-how-to-create-3d-animation-with-auto-rigging) descreve quadrúpedes na interface web apenas com caminhada e Smart Rig sem biblioteca. Isso não demonstra suporte quadrúpede pela API humanoide. Combate personalizado e anatomias especiais precisam de authoring/retarget e validação; Text to Motion não recupera automaticamente tempos do Diablo. Resultados têm retenção limitada: baixar outputs próprios, preservar hashes e nunca repetir POST incerto. Detalhes oficiais, custos, rate limits e schemas consultados ficam no relatório privado `meshy-api-and-diablo-license.md`.

O [Diablo indicado no Sketchfab](https://sketchfab.com/3d-models/diablo-fbd864f53f514b179a8d21579ca915f0) tem uploader **Vasian-Digital3D**, CC BY 4.0 listada, 60.174 faces e uma animação segundo a [API pública](https://api.sketchfab.com/v3/models/fbd864f53f514b179a8d21579ca915f0). Download está habilitado, mas a rota legítima exige autenticação (401 sem sessão). Nenhum arquivo foi baixado. Descrição vazia não comprova autoria original ou licença de componentes; rig, ação e distribuição continuam pendentes de inspeção/proveniência. Conservar autoria/link/licença/modificações se o uso vier a ser validado, conforme [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Não selecionar o modelo apenas por seu nome ou data.

## Próximo incremento de integração

A ponte concreta proposta começa com **skinning CPU de um ator**, produzindo triângulos deformados para os rasterizadores já existentes. O backend D3D11 atual recebe vértices projetados, sem joints/weights; esse piloto pode manter sua API. Primeiro estabelecer uma representação de rig/clip separada de D3DMESH1 e um adaptador de leitura de estado, com fallback para o ator atual quando faltar asset ou validação.

Antes de editar arquivos compartilhados, enviar ao principal a lista exata e o contrato: fontes/limites, cache por hash, posição/frame/direção, picking/depth e fallback. Nenhuma ampliação de importação, renderer ou opções foi feita por este documento. A aprovação do piloto no Godot não prova sincronização no jogo, saves, rede, desempenho ou aceitação artística. O principal atualiza a fila central após receber evidência e alinha a janela depois de seus escopos de UI/integração.

### Módulos isolados autorizados pelo principal

O principal autorizou avançar os arquivos próprios, mantendo `town_view.cpp`, `town_gpu`, options, menus e CMake com seus responsáveis. Foram preparados:

- `Source/engine/render/actor_model.hpp/.cpp`: loader candidato **D3DACTR1 v1**, independente de D3DMESH1, com hierarquia/TRS/IBMs, quatro influências, clips STEP/LINEAR, índices e textura RGB; validação atômica, limites de tamanho e rejeição de dados inválidos.
- `Source/engine/render/actor_pose.hpp/.cpp`: avaliação pura de bind/clip, slerp de menor arco, matrizes na ordem de pais real, skin CPU e normais pela inversa transposta. Não avança relógio, não cancela root motion implicitamente e não dispara eventos.
- `Source/engine/render/actor_visual_snapshot.hpp/.cpp`: **DTO e validação de entradas fornecidas pelo futuro adaptador**, identidade/índice nativo, footpoint, direção, frames, ticks, fase de sequência NPC, flags e visibilidade local em FPP. Não há ainda captura automática dos globais Player/Towner/Monster nem conexão ao desenho/picking.
- `tools/export_actor_model.py`: GLB local → formato candidato, sem fit arquitetônico nem instalação; PBR/emissão/normais avançados exigem opção explícita `--diffuse-only` e ficam registrados como perda de material. Recusa recursos não suportados, sobrescrita de candidato, alpha/morph/CUBICSPLINE e hierarquia inválida.
- `tools/actor_pose_smoke.cpp`: fixture autoral independente para loader/pose/snapshot, destinada à compilação coordenada pelo principal. Não é um alvo CMake registrado nesta frente.

O exportador gerou do idle real de Griswold um candidato privado com 26 nodes, 24 joints, 8.143 vértices, 4.971 triângulos, 72 tracks e textura derivada RGB 1024; 3.728.788 bytes. Arquivo SHA-256 `6c667bc3b06b86bb943f436cf9874a7fa269be63ffb9306db05896f5bbb1da99`; fonte GLB permaneceu igual. **37 verificações de exportação com fixture GLB autoral passaram**, incluindo controles negativos e overflow float32. A opção diffuse-only é uma limitação artística registrada, não uma conversão fiel dos materiais do rig. Compilação/validação C++ e integração na partida continuam responsabilidade/janela do principal; não foram executadas builds compartilhadas por esta frente.

## Evidências privadas e reprodução

Fontes e outputs ficam fora do Git em `D:\Diablo 1 3D\diagnostics\characters-rigs` e `D:\Diablo 1 3D\models\meshy`. O relatório de auditoria nativa, o relatório da ponte, o ledger e os resultados Godot devem preservar data, tarefa, revisão, SHA e limitações. Nenhum MPQ, extração, save, GLB privado, URL assinada ou segredo pertence a este documento público.

Estado de produção: fonte Griswold e outputs de rig/idle preservados, inspecionados e revisáveis no Godot. Nenhum loop autônomo de submissão ficou ativo na suspensão. A exceção posterior do último lote tem IDs/teto separados acima; não reativa produção ilimitada. Prosseguir com Tripo somente conforme contrato fornecido pelo principal.

### Resultado do primeiro ciclo completo de candidato

Griswold rig e idle foram baixados das tarefas pagas, sem repetição. **62 créditos reais consumidos na frente até esse ponto**: 54 herdados + 5 rig + 3 idle. Custo direto desse Griswold: **41** (3 vistas + 30 modelo + 5 rig + 3 idle); os 21 das sete outras vistas não pertencem ao seu custo individual. O último lote excepcional acima é contabilizado separadamente.

| Output | Revisão / SHA-256 | Conteúdo medido |
| --- | --- | --- |
| Rig | `griswold-rig-r1` / `4d0c9112dedc072d42da33157222d55ee6ff54cdd5b6e035b56dfc54ce2cc473` | 1 skin, 24 joints, 8.143 vértices, 4.971 triângulos, máximo 4 influências; hierarquia/IBMs válidos. |
| Walking, básico do rig | SHA `6ea36f19d87ecb784f50a11cc6880853c7196e7aab781a64b42255e0600c1dce` | `Armature\|walking_man\|baselayer`; keys de 0,033333 a 1,066667 s; stress técnico, não comportamento novo do NPC. |
| Idle, action 0 do catálogo efetivo | `griswold-idle-r1` / `b00a265b3e6c8ab1e52e234468102b97206910657e256ea594f60e47b55ad266` | `Armature\|Idle\|baselayer`; keys de 0,033333 a 4,033333 s (intervalo 4 s). Genérico, não a sequência da forja. |

O rig base tem `clip0` com **um único keyframe em 0,3 s**: é uma pose, não um idle animado. Caminhada e corrida vieram incluídas no rig; nenhum POST extra foi feito por elas. Os arquivos de personagem animado preservam mesh/skin; armature-only também foi guardado. O validador privado compara hierarquia/IBMs, pesos, finitos, canais, tempos, poses e bounds deformados; os quatro GLBs passaram. Onze controles com corrupção intencional também passaram. Isso é validação estrutural do asset, não teste de combate.

**Salvar/reabrir e visualizar:** Godot 4.7.2 real, RX 570, importou GLB → `.scn` → wrapper `.tscn`, reabriu e preservou arrays de mesh/pesos; bones/binds/keys passaram com tolerância de float explícita 1e−5. Rig/walk/run/idle somaram **142 verificações, zero falhas e 32 capturas**. Idle/walk/run tiveram quatro hashes distintos de poses e pixels; não foi usado fundo vazio como prova. Capturas incluem iso conforme parâmetros atuais, frente, costas e câmera baixa; o fit na câmera original ainda não é comparação de pixels da partida.

Uma falha inicial de prévia foi atribuída ao **enquadramento**, não à geração: `Armature.scale=0.01` faz parte do GLB; `MeshInstance3D.get_aabb()` descreve recurso estático e dava altura aparente 0,018, enquanto a skin avaliada mede cerca de 1,8. O reviewer passou a enquadrar pelo bound deformado. Nenhum GLB foi reescalado/regenerado para corrigir isso.

**Limites observados:** rosto e ferramentas precisam de revisão; material/luz produzem brilho forte e albedo deve ser inspecionado separadamente antes de culpar sombras pintadas. Walk/run têm pés até cerca de 0,10 m abaixo do pivô; idle tem oscilação planar do quadril. Root/Armature é estacionário, mas isso não prova foot locking correto. `headfront` fica aproximadamente a 1,396 m na bind pose; não é uma altura dos olhos validada. O rig não tem dedos. Coluna real é `Hips → Spine02 → Spine01 → Spine`, portanto nomes numéricos não devem determinar retarget sozinhos. Nenhum desses resultados promoveu `accepted`, mudou registry, instalou actor ou alterou gameplay.

Relatórios privados: `griswold-rig-and-idle-r1-skin-validation.json`, `griswold-{rig,walk,run,idle}-r1/review.json`, `native-animation-audit.json`, `native-animation-tables.md`, `native-monster-clips.csv`, `native-animation-audit-integrity.json` e `skeletal-bridge-audit.md`. A auditoria de fontes passou **2.175 verificações de integridade**, sem executar combate. O próximo incremento de runtime exige os pontos de integração acima e a janela do principal; completar assets novos não elimina essa dependência.
