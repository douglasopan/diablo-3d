# Preparação local do primeiro lote de inventário

Atualização: **8 de outubro de 2026**. **Entregue: metadados e receitas de isolamento de três candidatos. Ícones individuais prontos: zero. Modelos 3D: zero. Instalação: nenhuma.**

O pedido humano de começar a substituir imagens do inventário foi repassado pelo coordenador, evento `Sentinel_b8acba09eaec8191987339e698dfcd9c`. Ele motivou esta preparação dentro da fila existente de Itens. O direcionamento posterior suspendeu o lote 3D e limitou a coordenação central ao preflight de um único modelo existente, sem execução ou consumo. Este pacote não altera a fila geral, a seleção de conceitos, reservas, código ou a partida.

## O que está disponível

O [manifesto prep-r1](inventory-prep-r1.json) liga três conceitos aos IDs nativos, hashes, aliases, variantes, dimensões e estados separados. As pranchas foram inspecionadas visualmente e seus bytes preservados. A auditoria local encontrou 142 PNGs de conceitos, todos RGB sem alpha, e nenhum master de item nos diretórios auditados. As 193 entradas do overlay continuam com `visualApproval=false`. Não foi localizada aprovação específica dessas três revisões nas fontes auditadas; esta constatação é delimitada e não substitui uma escolha humana registrada em outro histórico.

A [validação local](inventory-prep-validation-r1.json) passou: hashes/fontes, 29 aliases, offsets dos cursores, células, limites das regiões propostas, estados separados e ausência de caminhos privados. Ela não testou a partida nem aprovou arte.

| Candidato e origem | ICURS → cursor de inventário → índice zero | Canvas lógico | Células | Preparação proposta |
| --- | --- | --- | --- | --- |
| [Short Sword r1](../../assets/concept-art/items/r1/short-sword-r1.png) | 64 → 76 → 75 | 28 × 84 px | 1 × 3 | Vista FRONT grande; conservar ponta, guarda, couro e pomo, com escala uniforme. |
| [Scroll r1](../../assets/concept-art/items/r1/scroll-r1.png) | 1 → 13 → 12 | 28 × 28 px | 1 × 1 | Objeto da miniatura existente; retirar moldura, fundo e sombra, preservando fita e orientação diagonal. |
| [Potion of Healing r1](../../assets/concept-art/items/r1/potion-of-healing-r1.png) | 32 → 44 → 43 | 28 × 28 px | 1 × 1 | Vista frontal grande; conservar vidro, líquido vermelho, reflexos, rolha e cordão. O fundo transmitido pelo vidro exige revisão específica. |

As dimensões vieram dos metadados da referência nativa privada, conferidas contra `GetInvItemSize` e a célula de 28 px do inventário. Não são dimensões da prancha nem da animação de chão. O catálogo estrutural mantém seus campos originais; o manifesto guarda apenas o novo snapshot de metadados, sem publicar sprites licenciados ou caminhos privados.

As regiões `proposedRegionXYWH` são receitas em pixels da prancha original, com origem no canto superior esquerdo. **Nenhum recorte ou remoção de fundo foi executado.** Um retângulo isolado ainda teria fundo opaco e não seria um ícone RGBA. O canvas de revisão 4× é uma proposta para avaliação; não representa um formato já suportado pelo jogo.

## Identidade, variantes e fallback

- Short Sword liga três registros base: espada inicial do Warrior, espada inicial do Bard e Short Sword comum. A escolha equipada por `Sword/SwordShield` não seleciona esta espada individualmente. Não substituir uma categoria inteira nem sobrescrever identidades únicas.
- Scroll liga 24 registros base ao cursor compartilhado. Manter nome, spell ID, `SCROLL`/`SCROLLT` e disponibilidade nativos. A arte representa a família física; não define marcas novas por magia nem 24 meshes independentes.
- Healing liga dois registros base ao cursor 32. Cura completa usa 35; mana usa 39. Essas e as demais poções conservam seus próprios recursos e fallback. Não recolorir um arquivo e declarar todas as variantes prontas.
- As três aparências de ouro dependentes de quantidade ficam preservadas. Todo item não listado, recurso ausente, inválido ou sem aprovação continua usando cursor e animação de chão originais.

O manifesto mapeia **29 registros base**, preservando atributos, nomes, slots, cargas, regras de quantidade, animações e picking. Ele não implementa o adaptador. Os números de frames de chão também estão registrados separadamente: espada 13, pergaminho 13 e poção 16; não são pranchas de inventário intercambiáveis.

## Condições para entregar um ícone ao integrador

1. Identificar a escolha humana exata: revisão, arquivo/hash e escopo de aprovação.
2. Produzir e revisar um PNG individual RGBA, sem texto, moldura, fundo ou sombra aderida, mantendo a silhueta completa. Revisar Healing em fundos claros e escuros para evitar apagar vidro/líquido ou reter o fundo da prancha.
3. Avaliar legibilidade e proporção nas células nativas, no inventário, loja e cinto quando aplicável. A ocupação segue os dados nativos; o recorte não muda slots.
4. Para o ícone definitivo, ligar o mesmo master/material aceito usado nas outras apresentações, conforme o [contrato de produção](../ITEM-PRODUCTION-CONTRACT.md). Um candidato 2D provisório precisa de escopo explícito e não comprova esse contrato.
5. O principal integra, compila, instala e registra testes reais da partida e fallback. Aprovação artística, preparação de arquivo, geração de modelo, instalação e teste são estados independentes.

## Limite do teste Studio

O teste de **um modelo com a opção Smart/topologia a confirmar** pertence ao operador e pesquisador centrais já existentes. Não há master de item neste pacote para um teste de pós-processamento. Não converter essa falta em geração automática dos três itens.

Os 3.200 créditos são uma declaração humana; plano/saldo/custo autenticados não foram conferidos nesta frente. Este pacote não acessou API, sessão Studio, credenciais ou ledger remoto, nem gerou, enviou, reservou, renderizou ou consumiu créditos. O operador central apresenta opção/modelo/custo real e teto ao usuário antes de qualquer execução.

**Recibo posterior do preflight público:** o [candidato condicional único](studio-preflight-candidate-r1.json) registra Scroll r1 e a captura recebida do operador `01a11d7e-ac7e-767d-9bd1-1d98b81d601b`, inspecionada por Itens. Ela mostra Malha Smart/P2.0, Quad, contagem 5.000, uma geração e botão Gerar com 100 créditos, além de Registrar/Entrar, Apenas para assinantes e privacidade Público. São opções/custo visíveis com sessão deslogada; não confirmam plano, saldo, orçamento autenticado ou teto aprovado. A prancha continua sem aprovação e não está pronta para upload. Não existe master de item para pós-retopologia, nem geração/reserva ativa desta frente; ausência global no serviço não foi comprovada por ledger remoto.

**Steering posterior — padrão e revisão completa:** a [lista Malha Smart](SMART-PRODUCTION-REVIEW.md) registra a escolha humana do método para todos os itens e os cenários de orçamento antes de qualquer geração. O operador agora reporta sessão autenticada/header 3.200; saldo não é teto e o preço autenticado por referência segue pendente. O novo teste manual Great Sword está excluído de novas gerações até QA/reuso, com Shirotachi bloqueado por dependência. Os zeros deste handoff descrevem entregáveis desta preparação, não negam o candidato manual remoto posteriormente relatado. Nenhuma arte, GLB ou instalação foi aprovada por essas mensagens.

**Experimento atual, posterior a esta preparação:** o [estudo agrupado](GROUPED-ITEMS-STUDY.md) considera Scroll + Healing e Mana opcional numa imagem/geração, para futura separação local. Short Sword está excluída desse experimento; os metadados e receitas deste handoff continuam preservados. Nenhuma imagem composta ou GLB filho foi produzido, e os valores individuais anteriores servem somente de comparação.

Responsável pelos assets e manifesto: chat Itens `01a11b58-80a9-7e63-973b-47d72752ccc7`. Único integrador de core/build/instalação/Git: `01a1153a-cad1-7fb1-8326-225511ee918d`. `assets/registry.json` permanece a autoridade de reserva e aceitação.
