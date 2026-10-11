# Instruções de desenvolvimento do Diablo 3D

## Começar pela etapa atual

Antes de implementar, leia [docs/PROJECT-EXECUTION.md](docs/PROJECT-EXECUTION.md), especialmente estado de execução, fila, contratos e rotina. Use esse guia para escolher a próxima entrega autorizada. Para caminhos reais, posse de estado e contratos init/update/reset/dispose, consulte [docs/CODE-ARCHITECTURE.md](docs/CODE-ARCHITECTURE.md); confira a versão no [checkpoint datado](docs/CHECKPOINT-2026-10-11.md) e os gaps em [docs/CODE-AUDIT.md](docs/CODE-AUDIT.md). Consulte os documentos específicos ligados nele conforme o componente afetado. Instruções explícitas do usuário prevalecem sobre este arquivo.

O objetivo é o Diablo 1 inteiro em 3D. Tristram é a primeira etapa; o primeiro nível procedural vem após seu gate. A expansão original da superfície é posterior. Não anunciar recursos planejados como implementados.

## Executar sem perder trabalho aceito

- Antes de editar, verifique o estado do Git e a responsabilidade dos agentes/chats concorrentes. Preserve mudanças alheias e faça staging de arquivos exatos.
- Escolha uma tarefa delimitada na fila: objetivo, dependência, arquivos, critério de conclusão e evidência. Ao terminar, atualize resultado, limitação e próxima ação no guia. Não criar outro plano concorrente para o mesmo escopo.
- Não reabrir etapas concluídas sem defeito reproduzido, requisito novo ou dependência alterada. Registre o motivo e o componente afetado.
- Preserve a simulação, mapa vivo, seed, colisão, interação e saves nativos. O renderizador não deve consumir o RNG da simulação.
- Identifique a closure de Source, executável, assets, perfil e gate antes de atribuir comportamento a uma versão. Código presente, candidato compilado/testado, instalado e aprovado visualmente são estados distintos. Não misturar WIP, fixture e build normal nem usar um teste anterior para qualificar outro checkout.
- Preserve estado persistente de câmera/input e invalide frames, picks e referências por época/identidade. Mudança de nível, perda de foco, recusa de frame e liberação de recursos são eventos diferentes; não resolver lifecycle resetando preferências indiscriminadamente. Documente donos e chamadas init/update/invalidate/dispose ao alterar uma fronteira.
- Mapa/exploração e estado de UI têm contratos próprios, separados de câmera, movimento e recursos de nível. Preserve capacidades anteriores na base limpa, sem controles por andar. O requisito de animar ataques em Tristram não autoriza dano/morte de NPCs amigáveis ou novo PvP; separar ação/animação, E/interação e política de hit/projétil, mantendo imunidades nativas.
- Antes de remover código, confira chamadas, registro de compilação, flags native/web/net, saves e ferramentas. Backend alternativo, fallback nativo, buffer GPU ou módulo preparado não são código morto por estarem inativos em um build. Limpeza exige evidência e patch pequeno em candidato isolado com gates pertinentes.
- Ao preparar o próximo perfil isolado da Catedral, preserve explicitamente a cadência habitual `Speed=32`; o default nativo é 20. Não sobrescrever configuração da partida em teste nem mascarar divergência de perfil com sprint ou alteração do relógio de animação.
- Por preferência humana, gameplay oferece terceira e primeira pessoa, com terceira como padrão. Home restaura terceira pessoa na sessão; F4 alterna 3D/nativo. Modos legados e `ResetTownViewCamera` permanecem para diagnóstico. Fidelidade da reconstrução exige geometria na mesma câmera e revisão em 360°; comparação nativa não prova qualidade das meshes.
- Para assets, reutilize IDs e reservas de `assets/registry.json`. Consulte `docs/ASSET-APPROVAL.md` antes de mudar seleção ou promover uma revisão. Aprovação parcial, teste técnico e aceitação integral são estados distintos.
- Identifique modelo, executável, conversão, aberturas em código e iluminação antes de atribuir uma regressão à arte. Nunca selecionar pelo arquivo mais recente nem regenerar um modelo aprovado para resolver erro de perfil.
- Novas gerações são candidatos. Preserve a revisão selecionada e só a substitua explicitamente após a validação e aprovação correspondentes ao escopo. Não pedir novamente aprovação que o usuário já forneceu.
- Preserve o master de maior qualidade. Reduções são LODs derivados, nunca substitutos definitivos automáticos. Valide silhueta, aberturas, UVs, normais e materiais antes de selecionar uma otimização. Ajustes automáticos usam escala uniforme; limites de um substituto procedural antigo não autorizam deformar o modelo em três eixos. Qualidade próxima da câmera é prioridade explícita do usuário.
- Exponha preferências apropriadas nas configurações do jogo. Declare persistência e aplicação imediata, reconstrução ou reinício; mantenha diagnósticos e seleção artística separados das opções gráficas.
- Faça build e testes proporcionais à mudança. Não interrompa partidas abertas para trocar executáveis; use candidato separado quando necessário e registre qual foi validado.
- Não publicar credenciais, arquivos MPQ, extrações proprietárias, saves, modelos privados ou dumps de engenharia reversa. Revise o diff e faça verificação de segredos antes de publicar. Nunca imprimir valores secretos nos relatórios.
- Preserve licença, autoria e créditos herdados. Belzebub é referência estudada; não declarar seu código integrado sem evidência e autorização aplicável.
- Todo áudio personalizado gerado ou convertido para distribuição deve conter **Douglas Pan** nas tags internas de artista/autoria, além do crédito no site. Verifique os metadados dos bytes finais antes de publicar ou liberar download; nomes de arquivo e texto da página não substituem essas tags. Preserve os masters fornecidos. A regra não reatribui a autoria da trilha original do jogo. Veja [docs/MUSIC.md](docs/MUSIC.md).

## Encerrar uma entrega

Registre o que mudou, a evidência técnica, o escopo de aprovação visual, limitações e próxima ação. Mantenha o detalhe em seu documento responsável e o estado operacional em `docs/PROJECT-EXECUTION.md`. O site/devlog recebe fatos entregues, com a revisão identificada; não promessas de funcionalidades prontas.
