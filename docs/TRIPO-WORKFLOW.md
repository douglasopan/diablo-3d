# Tripo v3: preparação local e candidatos para revisão

Contrato verificado em **8 de outubro de 2026**. O usuário escolheu Tripo como próximo provedor. O conhecimento, scripts e candidatos Meshy são preservados; a instrução mais recente permite aos responsáveis existentes concluir **somente o último lote global Meshy de 258 créditos**, antes da pausa. Este documento não reserva nem submete esse lote. O estado operacional e as responsabilidades permanecem no [guia central](PROJECT-EXECUTION.md).

`tools/tripo_assets.py` implementa uma ferramenta pontual, com a biblioteca padrão do Python, para preparar um plano, enviar referências locais, criar uma tarefa, consultar seu estado e baixar o GLB principal. **A entrega foi validada somente offline:** nenhuma credencial foi criada/lida, nenhuma chamada autenticada ou geração foi executada, e nenhum modelo, rig, animação ou integração artística Tripo foi validado.

## Contrato consultado

O host é fixo em `https://openapi.tripo3d.ai/v3`; não há parâmetro para substituir a origem. Não usar os endpoints v2 nem transportar opções de Meshy por semelhança de nome.

| Operação | Contrato usado pela ferramenta | Fonte primária |
| --- | --- | --- |
| Referência local | `POST /files`, multipart `file`; PNG/JPEG, até 20 MB | [File Upload](https://developers.tripo3d.ai/en/docs/files) |
| Uma imagem | `POST /generation/image-to-model`; `input` recebe o token do upload | [Image to Model — H Series](https://developers.tripo3d.ai/en/docs/generation-image-to-model/standard) |
| Várias vistas | `POST /generation/multiview-to-model`; `inputs` com chaves `front`, `left`, `back`, `right`; frente obrigatória e ao menos duas vistas distintas | [Multiview to Model — H Series](https://developers.tripo3d.ai/en/docs/generation-multiview-to-model/standard) |
| Saldo prévio | `GET /account/balance`; `balance` disponível, `frozen` reservado | [Account](https://developers.tripo3d.ai/en/docs/account) |
| Consulta e custo real | `GET /tasks/{task_id}`; `queued`, `running`, `success`, `failed`, `cancelled`, `banned`, `expired`; `credits_consumed` quando informado | [Task Query](https://developers.tripo3d.ai/en/docs/task-query), [Task Lifecycle](https://developers.tripo3d.ai/en/docs/task-lifecycle) |

As formas do envelope `code/data`, multipart, entradas e tarefas também foram conferidas no [cliente JavaScript oficial](https://github.com/VAST-AI-Research/tripo-js-sdk/blob/main/src/client.js), nos [utilitários](https://github.com/VAST-AI-Research/tripo-js-sdk/blob/main/src/utils.js) e no [transporte oficial](https://github.com/VAST-AI-Research/tripo-js-sdk/blob/main/src/http.js). As fontes são referências de contrato, sem dependência desse SDK. O cliente local não faz retries de POST e mantém o download separado da autenticação da API.

## Opções preparadas e orçamento

A geometria usa **`model: v3.1-20260211`**, qualidade `standard`, triângulos, limite solicitado de 6.000 faces, `auto_size: false` e UVs. O limite solicitado ajuda a preparar um candidato; a malha efetiva ainda precisa ser medida. A ferramenta admite outro `--face-limit` dentro do contrato H3.1, sem afirmar que o loader do jogo aceita esse número.

A textura é uma opção separada: **`texture_version: v3.5-20260815`**, qualidade `standard`, PBR e `texture_alignment: original_image`. `delight: true` só é enviado junto dessa versão de textura, que o documenta; isso não prova remoção perfeita da iluminação pintada. `--geometry-only` envia **`texture: false` e `pbr: false`** e omite as opções de textura. PBR sozinho força textura no serviço. A ferramenta não solicita quad, compressão, partes, rig nem mudança do eixo exportado; mantenha o master original antes das adaptações locais. As opções de qualidade e versões são descritas nas páginas de geração acima.

**Não há preço fixo no código.** Confira a [tabela oficial atual](https://developers.tripo3d.ai/en/pricing) para a operação, geometria, textura e opções exatas, e informe uma estimativa manual positiva em `plan --estimated-credits`. Um plano sem estimativa serve somente à inspeção offline; prepare o plano de execução com a estimativa antes dos uploads. `create --max-credits` exige teto local suficiente e consulta o saldo atual antes do POST. `balance` significa créditos disponíveis: não somar os congelados nem usar a intenção de compra como saldo.

Não foi verificado um endpoint de cotação exata nem um teto de cobrança aplicado pelo servidor. Portanto `--max-credits` compara **a estimativa manual**, e não garante o preço real; nenhuma compra ou recarga é implementada. O custo efetivamente informado pelo serviço é registrado depois em `task.json` e `result.json`. Orçamentos de outros chats e da exceção Meshy continuam coordenados pelo guia central.

## Credencial local

O cliente lê `TRIPO_API_KEY` do processo ou `C:\Users\dougl\.codex\secrets\diablo-tripo.dpapi` no Windows atual. O caminho efetivo usa o perfil do usuário, sem depender do diretório do jogo. O arquivo é protegido por **DPAPI do usuário corrente**, sem chave simétrica escrita em disco. Nunca versionar a credencial nem inseri-la em argumentos, prompts de agentes, capturas ou logs.

Quando o usuário decidir configurar o acesso, executar manualmente numa sessão PowerShell confiável:

```powershell
.\tools\Set-TripoCredential.ps1
```

O script recebe a chave por `Read-Host -AsSecureString`, sem eco e sem parâmetro contendo o valor. Grava somente o blob protegido em UTF-8 e restringe o arquivo ao usuário corrente. Ele não abre navegador nem testa a API. O script foi analisado sintaticamente nesta entrega; seu prompt e a gravação de uma chave real não foram executados. DPAPI protege o arquivo em repouso; o processo Python precisa do valor em memória durante uma chamada autenticada.

## Preparar, enviar e acompanhar uma única revisão

Antes de preparar produção, consultar as reservas de [assets/registry.json](../assets/registry.json), o [contrato de aprovação](ASSET-APPROVAL.md) e o responsável da família no guia central. Reutilizar `assetId` e variante existentes, escolher uma revisão explícita e conferir tarefas anteriores. O cliente registra essas identidades, mas **não edita nem valida reservas do catálogo**: usar outro diretório não autoriza duplicar uma tarefa já paga em outro chat.

Os exemplos abaixo usam nomes ilustrativos; substituir pelo ID/variante já reservados e pelas referências locais aprovadas. `models/` e o conteúdo privado permanecem fora do Git.

```powershell
# Offline: não exige chave e não faz requests. Vistas inferidas precisam de revisão antes do envio.
python tools/tripo_assets.py plan --asset-id ID_EXISTENTE --variant VARIANTE_EXISTENTE --revision tripo-r1 --front frente.png --back costas.png --estimated-credits ESTIMATIVA_ATUAL --out-dir models/tripo/objeto-tripo-r1

# Alternativa para uma única imagem: usar --image referencia.png, sem os parâmetros de vistas.
# Alternativa sem textura: acrescentar --geometry-only ao plan.

# Somente depois de conferir entradas, opções, custo, reserva e autorização do responsável:
python tools/tripo_assets.py balance
python tools/tripo_assets.py upload --out-dir models/tripo/objeto-tripo-r1
python tools/tripo_assets.py create --max-credits TETO_APROVADO --out-dir models/tripo/objeto-tripo-r1

# Consultas pontuais; não há loop nem submissão automática.
python tools/tripo_assets.py status --out-dir models/tripo/objeto-tripo-r1
python tools/tripo_assets.py download --out-dir models/tripo/objeto-tripo-r1
```

`ESTIMATIVA_ATUAL` e `TETO_APROVADO` são números informados após conferir o preço, não valores literais para executar. `plan` mantém caminhos, tamanho, MIME e SHA-256 de cada entrada, hash do conjunto de vistas, ID, variante, revisão, provedor, versão e opções. PNG/JPEG são as entradas locais deste primeiro cliente; WebP aceito pela geração não está no contrato de upload usado aqui. Recomenda-se pelo menos 256 × 256; a ferramenta confere assinatura de formato e tamanho, sem analisar resolução ou qualidade artística.

## Retomada sem submissão duplicada

Cada diretório de asset contém um plano e no máximo uma tentativa de geração. Cada upload tem seu próprio diretório `uploads/front`, `uploads/left`, `uploads/back`, `uploads/right` ou `uploads/image`, com uma tentativa de POST. A geração tem `generation/`. Cada tentativa grava `request.json` e `post-state.json` atomicamente **antes** de chamar a API; `create-state.json` e `task.json` ligam a geração à identidade local. O lock exclusivo `.operation.lock` impede concorrência no mesmo diretório.

`upload` conserva os tokens já retornados e conclui apenas vistas ainda sem tentativa. Os tokens ficam somente nos recibos privados de upload; não aparecem em stdout. Alterar os bytes de uma referência depois do plano interrompe o fluxo. `create` exige esses uploads concluídos e não os refaz. Consultar/baixar usa o task ID salvo, sem novo envio pago.

Timeout, desconexão, HTTP 5xx, envelope inválido, identidade ausente ou interrupção depois de iniciar um POST ficam **`UNKNOWN`**; um `SUBMITTING` deixado por processo interrompido também exige reconciliação. Mesmo tentativas explicitamente rejeitadas não são reenviadas pelo comando. **Não apagar estados, trocar de diretório ou repetir a geração para resolver uma dúvida.** Conferir a conta e o recibo original com o responsável; recuperar manualmente o token/task ID quando identificável. Este primeiro cliente não implementa descoberta de tarefas nem adoção automática de resultados ambíguos. Se existir lock órfão, confirmar que o processo terminou antes de removê-lo; removê-lo não resolve o estado do POST.

O bloqueio é por diretório, sem transação distribuída entre chats ou reservas automáticas no catálogo. Por isso a responsabilidade única e a conferência prévia de tarefa/ID/variante/revisão/hash continuam necessárias.

## Download e integração visual

O download consulta o task ID para obter um link atual e faz um GET HTTPS com cliente separado, **sem Bearer nem cookies**. Redirecionamentos da API são recusados; redirecionamentos do download precisam continuar em HTTPS e retiram headers sensíveis. URLs de resultado não são gravadas nos metadados nem impressas; mensagens de erro não incluem bodies do servidor ou headers. Nenhum `Idempotency-Key` foi presumido para v3.

Todos os sete estados oficiais são conservados em `task.json` e `result.json`. Somente `success` permite baixar; `banned` e `expired` retornam seu estado sem acessar um asset nem submeter nova geração. `expired` significa que o serviço já não disponibiliza os arquivos de saída; preserve qualquer master previamente baixado e reconcilie com o responsável, sem regeneração automática.

O GLB é escrito temporariamente, limitado a 1 GiB, conferido por tamanho HTTP quando informado, header `glTF`, versão 2, tamanho declarado, limites dos chunks e JSON glTF 2.0. Depois é promovido atomicamente para `downloads/model.glb`, com SHA-256 e tamanho em `downloads.json`. Reexecutar `download` reutiliza o arquivo quando tamanho/hash e contêiner batem; um arquivo divergente é baixado novamente sem criar tarefa. A validação do contêiner não substitui inspeção de meshes, texturas, dependências externas ou topologia.

Manter o GLB master e importá-lo no Godot como **instância individual**, com o ID/variante nativos, revisão e hash. Seguir [GODOT-EDITOR.md](GODOT-EDITOR.md) para salvar/reabrir, transformar, revisar 360° e exportar explicitamente. Não fundir Tristram numa única malha nem selecionar automaticamente o arquivo mais recente. A cabana congelada e sua seleção atual permanecem preservadas.

A ponte estática atual aceita **1–20.000 triângulos por instância**, atlas RGB de até **2048 × 2048**, posições finitas e não degeneradas, X/Z relativos a `nativeMin` de **−64 a 128**, altura **0–64** e UVs **0–1**. Materiais adicionais precisam caber no atlas básico opaco; o loader não aplica todos os mapas PBR. Rig, skin, animação, morph, transparência, emissão e dependências incompatíveis são recusados pela exportação v1. O tamanho/qualidade do pedido Tripo não altera esses limites. Rigs e clips exigem a via animada descrita em [MODEL-ANIMATION-CONTRACT.md](MODEL-ANIMATION-CONTRACT.md); sucesso da geração não demonstra compatibilidade, sincronização nativa ou aceitação artística.

## Evidência técnica desta entrega

```powershell
python tools/test_tripo_assets.py
```

Passaram **31 testes offline**, com transportes falsos e sem rede: planejamento sem credencial, labels/identidade, separação geometria/textura, retomada de uploads, imagem alterada, saldo/estimativa, duplicação, concorrência, lock interrompido, timeout/5xx sem retry, rejeição de identidade, persistência dos sete estados oficiais, bloqueio de download/geração em `banned` e `expired`, envelopes inválidos, redaction, redirecionamento/autenticação, custo retornado, hash/retomada e GLBs truncados ou inválidos. O helper PowerShell passou análise sintática, sem executar seu prompt.

Continuam pendentes: chave/saldo reais, disponibilidade das versões para a conta, expiração/renovação de tokens de upload, preço exato com as opções escolhidas, aceitação do serviço ponta a ponta e inspeção visual de um candidato. O guia central registra a próxima ação autorizada; este documento não cria fila concorrente de produção.
