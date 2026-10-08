# Novidades por email — operação

Configurado em 8 de outubro de 2026. A inscrição e o pedido de cancelamento são recebidos de verdade pelo Google Forms da conta proprietária de Douglas Pan. Nenhuma campanha ou mensagem foi enviada nesta configuração.

## Links públicos para o site

O site deve abrir o formulário correspondente. A confirmação de recebimento é exibida pelo Google Forms somente depois do envio concluído; o site não deve simular essa confirmação.

```json
{
  "signup_url": "https://docs.google.com/forms/d/e/1FAIpQLSc9zEAtvSls3ISTBr0QYIrdBzlJCzfjLlSBldiDg3c_Ax1Uuw/viewform",
  "cancel_url": "https://docs.google.com/forms/d/e/1FAIpQLSdyOZkbGHfpVDpwUVnhyDPF00HU8w4be-i4Egh_y6nlSumNSQ/viewform"
}
```

São links de participante, não de edição ou de respostas. Não incluir links administrativos, planilhas, respostas, emails de inscritos ou capturas privadas nos arquivos públicos do site.

## Formulários e privacidade

- **Diablo 3D — Novidades por email / Email updates:** email, idioma (Português do Brasil ou English) e uma caixa obrigatória de consentimento voluntário para receber novidades do projeto. Descrição e confirmação em PT/EN, com links para cancelamento e para o site. Sem frequência fixa prometida.
- **Diablo 3D — Cancelar novidades / Unsubscribe:** email obrigatório para solicitar remoção. Descrição e confirmação explicam, em PT/EN, que Douglas Pan processará o pedido manualmente antes do próximo envio.
- Respostas ficam no Google Forms do proprietário. Nenhuma planilha foi criada ou vinculada, e nenhum destinatário/editor adicional foi concedido.
- Acesso de edição **Restrito**, somente o proprietário; acesso de participante por link público.
- Coleta automática do email da conta Google **Não coletar**; cópias das respostas por email desligadas; limite de uma resposta e exigência de login desligados; edição posterior de respostas desligada; resumo público de resultados desligado.
- O participante informa o próprio email. A interface pública avisa que a conta Google e o email da sessão não fazem parte da resposta. O Forms também registra o horário de envio.

## Rotina do responsável

Na conta proprietária, abrir o Google Forms e localizar os dois títulos acima. Consultar as abas **Respostas**, sem compartilhar edição ou publicar o resumo.

Antes de qualquer envio futuro autorizado:

1. Consultar primeiro todos os pedidos de cancelamento e suprimir os respectivos endereços da lista de envio. O formulário registra o pedido; não remove automaticamente uma inscrição.
2. Considerar apenas inscrições com consentimento marcado, separar pelo idioma escolhido, revisar endereços e eliminar duplicidades.
3. Excluir de qualquer campanha as respostas de teste com domínio reservado `example.com`. Os dois registros desta validação devem permanecer identificados como teste; o pedido de cancelamento de teste também deve ser tratado como suprimido.
4. Manter destinatários e registros de supressão privados. Não colocar a lista de emails em repositório, página pública, log ou captura destinada à publicação.
5. Incluir o link de cancelamento nas novidades. Não usar os dados para publicidade de terceiros ou outras finalidades.

Esta implementação oferece coleta e pedido de cancelamento. Não configura automação de envio, agenda de campanhas ou integração com serviço de marketing. Qualquer envio futuro depende de instrução própria.

## Limites conhecidos

A inscrição tem validação básica de presença de `@`; ela não comprova que o endereço existe ou pertence ao participante. Não há confirmação dupla por email. O cancelamento exige preenchimento do email e é processado manualmente. As configurações dispensam conta Google, mas os testes foram realizados na sessão já conectada do proprietário; não foi realizada uma sessão anônima separada.

## Validação realizada

Em 08/10/2026, horário de São Paulo:

- Inscrição sem consentimento foi rejeitada como campo obrigatório.
- Uma inscrição de teste com email de domínio reservado, idioma Português (Brasil) e consentimento marcado foi gravada às **07:55**; a aba individual do proprietário mostrou **1 resposta** e os três valores.
- Cancelamento sem email foi rejeitado como campo obrigatório.
- Um pedido de cancelamento do mesmo email de teste foi gravado às **07:56**; a aba individual do proprietário mostrou **1 resposta**.
- Ambas as páginas de confirmação reais foram verificadas. Os dois registros foram mantidos, sem exclusão permanente, exportação de dados ou envio de email.

Comprovantes privados locais: `D:\Diablo 1 3D\diagnostics\newsletter-20261008\`. Incluem configurações, acesso do proprietário, resposta persistida e confirmações. Não copiar essas capturas para `website/public/` ou para conteúdo publicado.
