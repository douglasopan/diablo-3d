# Universal Modder e personagens — incremento de estudo, 12/10/2026

Pedido humano reabre ferramentas de autoria, movimento e criação de personagem na fila de PROJECT-EXECUTION. Esta frente reaproveita o pipeline de heróis existente; não substitui o plano, rig r3, masters ou build R17. Nenhuma nova release foi criada.

## Entregue

- `tools/art_studio`: recorte opt-in de fundo sólido, alpha HD, registro comum de animação, prévia/receipt de textura3×3, testes de preservação. Universal Modder MIT pin5ba8150e27b4f1bf17ae19dcecdb2341e467885a; só duas bibliotecas de preparação, sem instalação global/hooks/gastos.
- MakeHuman core data CC0 pin a8bc2d54ff0ac92e78ff71431b1023eda42bf482 importado em `assets/character-sources/makehuman-r1`, com licença/proveniência; receita OBJ de NPC parametrizada por age/muscle/weight/feminine, mesma topology/weights. É autoria, não ActorModel/gameplay. 163ossos de origem excedem96 do loader; falta rig reduzido, anchors, roupa/skin/rig/animação corretos.
- Catálogo navegável `games.html`/`games.json`:132 notas de jogos, nomes adicionais/aliases mantidos. Status são relatos upstream. Motor/playbook suportado não prova mod funcionando, nem port automático. Skyrim/Doom são referências de direção e playbooks; não inferir que têm um pacote de personagem licenciado neste índice.
- Auditoria local encontrou Warrior r3 rigged52ossos/zero clips e GLB humano local fantasy+warrior65ossos/25clips. Todos52ossos do alvo reparado têm correspondência exata no donor e os dois binds passaram. Nome de clip não comprova a ação realizada.
- Dois probes reaproveitam o retarget existente: target mesh/material/IBMs/nós/bytes antigos preservados, movimentos reais. Attack índice0 mínimoY−0,005000575; walk índice1 mínimoY−0,003436194, ambos recusados pelo critério vigente−0,002. Não reinterpretar como centímetros; escala gameplay não calibrada. Attack24renders/8direções/3fases não vazios/sem corte, mas inspeção de amostras mostra variação pequena e sem golpe claramente demonstrado: classificação/contato/arma/timing permanecem pendentes. Recibos privados `diagnostics/universal-modder-20261012/*probe-r1/receipt.json`. Fonte local/hashes preservados; licença do donor não estabelecida pela inspeção. Zero animações novas qualificadas.

## Direção humana de personagens

Warrior mantém aparência/mesh/texture selecionadas, com rig/retarget corrigidos; não convertê-lo automaticamente em corpo MakeHuman. MakeHuman priorizado para NPCs. Player e moradores compartilham linguagem de materiais, roupa, escala, contraste e iluminação. Identidade depende de silhueta, postura, ferramentas, rosto/cabelo e vestimenta, além da skin. Wirt exige anatomia/marcha próprias. As famílias existentes em CHARACTERS-RIGS-ANIMATION continuam autoritativas. Gameplay segue sprites até rig/animações corretos; still não vale clip.

## Criador de personagem / New Game — requisito registrado, não implementado

Manter modo clássico e acrescentar criação personalizada. Separar aparência de atributos/progressão. Parâmetros técnicos do estudo de NPC não são sliders finais do jogador.

1. Aparência: preset/classe, corpo, proporções dentro de bounds revisados, rosto/cabelo, skin, cores de roupa; Warrior como referência de estilo. Cada preset tem hash/source, rig semântico e roupas compatíveis. Não aplicar nonuniform scale a um master selecionado para simular morph.
2. Partida personalizada: classe, atributos, nível inicial, itens/equipamento/inventário, spells disponíveis, acessos a andares/atalhos e flags de progresso nativas. Opções vêm das tabelas/classdat/attributes/itemdat/spells e do modo Diablo/Hellfire efetivamente ativo, não IDs livres inventados.
3. Validar caps/classe, requisitos dos itens, ocupação da grade, mão dupla/escudo, counts e efeitos de itens; nunca preencher structs com valores arbitrários. A escolha humana de livre parametrização autoriza modo custom, mas não mudar RNG/comandos/saves através do renderizador.
4. Receita de início pertence ao perfil/formulário; aplicar uma vez, após a inicialização nativa do novo herói, antes de salvar. Derivar HP/mana/defesa corretamente e preservar XP/level/ValidatePlayer. Unlock é acesso/progresso escolhido, não conclusão automática de quests inconsistente. Expor limites claramente na UI.
5. Reabrir/save-load deve manter resultado, sem reaplicar receita nem conceder itens novamente. Marcar partida custom para comparação de feedback público; não inferir compatibilidade de multiplayer.

## Movimento livre, arma na câmera e confirmação de hit — estudo pendente de implementação

Hoje `StepTownCameraMovement` calcula vetor relativo à câmera e quantiza para oito direções nativas. GZDoom/Skyrim servem de referência de sensação: strafe independente do olhar, reversão imediata, transições suaves e câmera estável. Não chamar suavização visual de locomotion contínua. Modo realmente contínuo exige estudo explícito de posição/ocupação/path, interação/combat range, paredes/escadas, saves e rede; não adicionar um ghost visual divergente do player. Primeiro candidato fica opcional, para comparar com movimento clássico sem mudar Speed32 ou ataque nativo. Testar diagonais/quinas, mudar mira durante hold, paredes, HUD, loot, portas, save-load e troca de nível. Nenhum código proprietário Skyrim foi importado.

Referências de arma na câmera: [GZDoom PSprites](https://github.com/ZDoom/gzdoom/blob/master/src/playsim/p_pspr.cpp), [ioquake3 cg_weapons](https://github.com/ioquake/ioq3/blob/master/code/cgame/cg_weapons.c). Estudo de comportamento; nenhum trecho GPL foi incorporado ao runtime Sustainable Use. Implementação própria deve desenhar mesh/arms em pass separado com FOV/config offsets independentes do world, respeitar depth/near plane, iluminação consistente e HUD/pause/fade. Arma vem do equipamento real e fase/frame nativos; troca/unarmed/mão dupla/escudo/spells precisam cobertura, sway/bob opcionais e limitados. Não usar uma arma falsa ou fazer animação autorizar dano.

Confirmação de hit segue acerto da simulação (`PlrHitMonst` e resolução de projéteis/spells), com identidade do atacante/alvo/evento, época do nível e id deduplicado. Exibir após resultado confirmado atribuível ao player, distinguindo miss/block/imunidade quando apropriado; não usar raycast visual para declarar dano. Indicador breve na mira e opcional áudio, de autoria nossa; limpar na troca de nível e respeitar pause/menus e acessibilidade. Sem mudar imunidade de NPCs, chance/dano, vida do alvo ou RNG. Fonte de estudo adicional: [Xonotic](https://gitlab.com/xonotic/xonotic-data.pk3dir/-/blob/master/xonotic-client.cfg). Falta implementação, captura e gate no jogo.

## Próximo trabalho no mesmo fluxo

Corrigir/classificar os clips Warrior, contato/IK e socket de arma, provar timing/ações nativas, alinhar NPCs ao mesmo estilo e rig compatível; só depois promover revisões. Preparar o criador personalizado e modo experimental de movimento/viewmodel/hit com contratos acima em candidato de Source isolado. Ferramentas/testes de arte entregues não qualificam esses recursos como já integrados.
