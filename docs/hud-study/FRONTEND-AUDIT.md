# Auditoria das telas de entrada

Estudo de apresentação de 8 de outubro de 2026. Dados da galeria em [data/frontend.json](data/frontend.json). Esta entrega deriva de leitura do código e não modifica a interface do jogo. Exemplos são conceitos com nomes, endereços, senhas e métricas fictícios; não são capturas de execução.

O requisito autorizado é estudar todas as telas existentes antes da implementação. G1 da cabana continua sendo a etapa visual central do projeto. Não houve edição do guia operacional, `Source/`, opções, áudio, executáveis ou launchers, nem staging/commit.

## Cobertura

O JSON contém **20 entradas principais**: 15 telas do front-end, quatro famílias de diálogo/estado e uma família de carregamento. `variants` contém substituições de conteúdo para reaproveitar o layout da entrada principal. As variantes não possuem ID próprio: a galeria pode gerar exemplos individuais combinando o registro principal com cada substituição. Arrays presentes na variante substituem o array correspondente; propriedades omitidas herdam o registro principal.

| ID | Tela/componente | Conteúdo e ações existentes | Evidência |
| --- | --- | --- | --- |
| F01 | Título | Logo animado; crédito; tecla/clique/controle ou tempo avança | `Source/DiabloUI/title.cpp:67` |
| F02 | Diablo/Hellfire | Entrar em Hellfire; mudar para Diablo | `Source/DiabloUI/selstart.cpp:41` |
| F03 | Principal | Um jogador; multijogador; configurações; suporte; créditos; saída | `Source/DiabloUI/mainmenu.cpp:51` |
| F04 | Heróis | Lista + novo herói; retrato; nível e quatro atributos; OK/excluir/cancelar | `Source/DiabloUI/hero/selhero.cpp:484`, `:530` |
| F05 | Classes | Classes disponíveis, retrato e atributos iniciais; OK/cancelar | `Source/DiabloUI/hero/selhero.cpp:164`, `:250` |
| F06 | Nome | Um campo obrigatório de nome, limite 15; OK/cancelar | `Source/DiabloUI/hero/selhero.cpp:311` |
| F07 | Save existente | Carregar partida/nova partida; OK/cancelar | `Source/DiabloUI/hero/selhero.cpp:217` |
| F08 | Dificuldade | Normal/Pesadelo/Inferno; descrição; OK/cancelar | `Source/DiabloUI/multi/selgame.cpp:331`, `:396` |
| F09 | Conexão | Providers disponíveis; requisitos e jogadores suportados; OK/cancelar | `Source/DiabloUI/multi/selconn.cpp:51`, `:126` |
| F10 | Ações/partidas | Criar, criar pública, entrar; lista pública no ZeroTier; OK/cancelar | `Source/DiabloUI/multi/selgame.cpp:111` |
| F11 | Entrada manual | ID ZeroTier ou IP/hostname TCP, limite 128; OK/cancelar | `Source/DiabloUI/multi/selgame.cpp:354` |
| F12 | Velocidade | Normal/Rápida/Mais rápida/Máxima; descrição; OK/cancelar | `Source/DiabloUI/multi/selgame.cpp:483` |
| F13 | Senha | Um campo, limite 15; OK/cancelar | `Source/DiabloUI/multi/selgame.cpp:562` |
| F14 | Créditos | Rolagem automática de autoria; tecla/clique volta | `Source/DiabloUI/credits.cpp:151`, `:187` |
| F15 | Suporte | Rolagem de suporte, responsabilidade e licenças; tecla/clique volta | `Source/DiabloUI/credits.cpp:195`, `Source/DiabloUI/support_lines.cpp:8` |
| D01 | Exclusão | Confirmação com nome do herói; sim/não | `Source/DiabloUI/hero/selhero.cpp:613`, `Source/DiabloUI/selyesno.cpp:58` |
| D02 | Aviso grande | Logo e texto variável; OK | `Source/DiabloUI/selok.cpp:58` |
| D03 | Popup compacto | Título opcional, informação/erro; OK; fundo anterior opcional | `Source/DiabloUI/dialogs.cpp:77`, `:164` |
| D04 | Sincronização | Barra de progresso e cancelar | `Source/DiabloUI/progress.cpp:106`, `Source/msg.cpp:2741` |
| L01 | Carregamento | Fundo de transição e barra; onze famílias de fundo | `Source/interfac.cpp:99`, `:137`, `:241` |

Configurações detalhadas e painéis dentro da partida são auditados pelos outros responsáveis do estudo. Esta matriz não duplica esses inventários.

## Fluxos verificados

- **Um jogador:** principal → herói ou novo herói → classe/nome quando necessário → carregar/nova partida se houver save → dificuldade ao iniciar nova partida → jogo.
- **Multijogador:** principal → conexão → herói → ações/partidas → criação ou entrada. Escolher provider chama `SNetInitializeProvider`, que abre a seleção de herói (`Source/DiabloUI/multi/selconn.cpp:144`, `Source/storm/storm_net.cpp:135`).
- **Offline do fluxo multijogador:** conexão → herói → dificuldade → velocidade → jogo. Offline pula ações/lista, endereço e senha (`Source/DiabloUI/multi/selgame.cpp:116`, `:550`).
- **Criar pública:** dificuldade → velocidade → jogo. **Criar privada:** dificuldade → velocidade → senha → tentativa de criação.
- **Entrar manualmente:** ID ou endereço → senha quando há criptografia compilada → tentativa de entrada. Uma partida pública selecionada na lista tenta a entrada diretamente (`Source/DiabloUI/multi/selgame.cpp:311`, `:379`).

## Condições que a galeria deve mostrar

**Build local:** `NONET=ON` foi confirmado no cache local de build, em `cache local de build (CMakeCache.txt:647)`. O cache não foi copiado. O seletor executável oferece apenas **Offline**. TCP e ZeroTier são capacidades herdadas condicionais, sem validação nova de rede neste estudo. As flags podem retirar cada provider individualmente (`Source/DiabloUI/multi/selconn.cpp:55`). O máximo herdado de rede é quatro jogadores, e Offline informa um (`Source/multi.h:26`, `Source/DiabloUI/multi/selconn.cpp:126`).

**Hellfire/shareware:** o seletor de modo aparece ao descobrir Hellfire com preferência Ask (`Source/diablo.cpp:1350`, `:1378`). O fundo/identificação do menu varia por edição, e NOEXIT omite a saída em plataformas correspondentes (`Source/DiabloUI/mainmenu.cpp:57`). Monge requer Hellfire; Bardo/Bárbaro requerem assets ou opções de teste. Arqueira/Feiticeiro são bloqueados no shareware (`Source/DiabloUI/hero/selhero.cpp:179`, `:289`). A quantidade de classes vem dos dados carregados (`Source/tables/playerdat.cpp:413`), portanto não se presume disponibilidade universal de seis classes.

**Heróis e nomes:** solo e multijogador usam conjuntos de personagens diferentes; a lista vazia abre diretamente classes. Excluir desabilita em Novo herói. O campo Savegame é exclusivo de `_DEBUG`. Nome é obrigatório, com limite passado 15; validação de espaços/caracteres/palavras reservadas só ocorre em multijogador. Controle/gamepad ou PREFILL_PLAYER_NAME pode preencher um nome inicial (`Source/DiabloUI/hero/selhero.cpp:139`, `:267`, `:335`, `:518`, `:590`). Os atributos de exemplo do Guerreiro são nível 1, força 30, magia 10, destreza 20, vitalidade 25 (`Source/DiabloUI/hero/selhero.cpp:257`; `assets/txtdata/classes/warrior/attributes.tsv:3`).

**Dificuldade/velocidade:** Pesadelo exige nível 20 e Inferno nível 30 no multijogador. O fluxo solo encerra a preparação após dificuldade e não exibe a tela de velocidade. As quatro velocidades de criação correspondem a 20/30/40/50 ticks por segundo (`Source/DiabloUI/multi/selgame.cpp:415`, `:433`, `:506`).

**Rede e senha:** Criar partida privada aparece somente com PACKET_ENCRYPTION. Lista pública é exclusiva do ZeroTier; Carregando/Nenhuma/lista preenchida são estados do mesmo layout. Não há login, conta, lobby/chat, campo de nome da sala, botão nativo Atualizar nem campo de porta separado. O nome da partida é gerado pelo provider quando não fornecido (`Source/storm/storm_net.cpp:157`). Campo de senha usa `UiEdit` comum e **não é mascarado** (`Source/DiabloUI/ui_item.h:272`): os dados usam `type: text`. Uma visualização mascarada deve ser identificada como melhoria proposta. Senha pode ficar vazia ao entrar; criação privada exige conteúdo (`Source/DiabloUI/multi/selgame.cpp:585`).

**Créditos e suporte:** os dados fornecem trechos resumidos para estudar composição, sem substituir os créditos completos de pessoas, licenças e responsabilidade. Os endereços de suporte já existem como texto; a fonte não oferece botões de abrir site. Autoria e logo aprovado devem ser preservados em qualquer implementação.

**Carregamento:** L01 abrange início, cidade, Catedral, Catacumbas, Cavernas, Inferno, Cripta, Ninho, portal azul, portal vermelho e portão. Cripta/Ninho são condicionais Hellfire. A estrutura da barra é compartilhada, com posição/cor associadas ao fundo; não são menus com escolhas. O jogo não mostra porcentagem numérica nem estimativa de tempo nessa tela (`Source/interfac.cpp:59`, `:68`, `:241`). D04 é a espera cancelável de sincronização, com outro componente. Carregamentos rápidos podem ser omitidos pela preferência existente (`Source/interfac.cpp:289`).

## Limites e revisão

- A evidência é estática. Não foi feita observação automatizada das telas em execução; nenhuma captura do jogo completo foi declarada como obtida.
- O layout da galeria e a arte gerada representam propostas. Dimensões, fontes, áreas clicáveis e redistribuição responsiva ainda precisam ser implementadas e validadas conjuntamente.
- Logo, retratos, fontes e fundos próprios do jogo não foram extraídos ou copiados. O JSON contém referências textuais e metadados de código.
- Nomes, sala, senha, endereço e ping são fictícios. O endereço IP do exemplo usa o bloco de documentação 192.0.2.0/24; não é um destino de rede.
- A galeria deve tornar visíveis disponibilidade e condição. Variantes de erro, estados vazios, filtros de plataforma e campos de debug não devem ganhar novas funções para preencher espaço.
- O conjunto de arquivos sob responsabilidade desta auditoria é somente `data/frontend.json` e este documento.
