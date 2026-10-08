# Instruções de desenvolvimento do Diablo 3D

## Começar pela etapa atual

Antes de implementar, leia [docs/PROJECT-EXECUTION.md](docs/PROJECT-EXECUTION.md), especialmente estado de execução, fila, contratos e rotina. Use esse guia para escolher a próxima entrega autorizada. Consulte os documentos específicos ligados nele conforme o componente afetado. Instruções explícitas do usuário prevalecem sobre este arquivo.

O objetivo é o Diablo 1 inteiro em 3D. Tristram é a primeira etapa; o primeiro nível procedural vem após seu gate. A expansão original da superfície é posterior. Não anunciar recursos planejados como implementados.

## Executar sem perder trabalho aceito

- Antes de editar, verifique o estado do Git e a responsabilidade dos agentes/chats concorrentes. Preserve mudanças alheias e faça staging de arquivos exatos.
- Escolha uma tarefa delimitada na fila: objetivo, dependência, arquivos, critério de conclusão e evidência. Ao terminar, atualize resultado, limitação e próxima ação no guia. Não criar outro plano concorrente para o mesmo escopo.
- Não reabrir etapas concluídas sem defeito reproduzido, requisito novo ou dependência alterada. Registre o motivo e o componente afetado.
- Preserve a simulação, mapa vivo, seed, colisão, interação e saves nativos. O renderizador não deve consumir o RNG da simulação.
- Home usa o backend original. Fidelidade da reconstrução exige geometria forçada na mesma câmera e revisão em 360°; identidade em Home não prova qualidade das meshes.
- Para assets, reutilize IDs e reservas de `assets/registry.json`. Consulte `docs/ASSET-APPROVAL.md` antes de mudar seleção ou promover uma revisão. Aprovação parcial, teste técnico e aceitação integral são estados distintos.
- Identifique modelo, executável, conversão, aberturas em código e iluminação antes de atribuir uma regressão à arte. Nunca selecionar pelo arquivo mais recente nem regenerar um modelo aprovado para resolver erro de perfil.
- Novas gerações são candidatos. Preserve a revisão selecionada e só a substitua explicitamente após a validação e aprovação correspondentes ao escopo. Não pedir novamente aprovação que o usuário já forneceu.
- Exponha preferências apropriadas nas configurações do jogo. Declare persistência e aplicação imediata, reconstrução ou reinício; mantenha diagnósticos e seleção artística separados das opções gráficas.
- Faça build e testes proporcionais à mudança. Não interrompa partidas abertas para trocar executáveis; use candidato separado quando necessário e registre qual foi validado.
- Não publicar credenciais, arquivos MPQ, extrações proprietárias, saves, modelos privados ou dumps de engenharia reversa. Revise o diff e faça verificação de segredos antes de publicar. Nunca imprimir valores secretos nos relatórios.
- Preserve licença, autoria e créditos herdados. Belzebub é referência estudada; não declarar seu código integrado sem evidência e autorização aplicável.

## Encerrar uma entrega

Registre o que mudou, a evidência técnica, o escopo de aprovação visual, limitações e próxima ação. Mantenha o detalhe em seu documento responsável e o estado operacional em `docs/PROJECT-EXECUTION.md`. O site/devlog recebe fatos entregues, com a revisão identificada; não promessas de funcionalidades prontas.
