# Música de fundo do site

Entrega solicitada pelo usuário em 8 de outubro de 2026. A faixa fornecida como `06 (1).mp3` toca em repetição nas páginas PT-BR e EN, com um player compacto de tocar/pausar, silêncio e volume. O usuário confirmou a reprodução durante a revisão.

O player pronto é [Plyr 3.8.5](https://github.com/sampotts/plyr/tree/v3.8.5), com JavaScript, CSS, ícones e licença MIT auto-hospedados em `public/vendor/plyr/`. A revisão upstream é `de3eeb6b60fbcb592a5961731183be7f603b5ddf`. Os ícones usam URL local explícita; anúncios e integrações com provedores externos não são habilitados. O elemento HTML de áudio conserva os controles nativos como fallback sem JavaScript ou se a melhoria falhar.

## Faixa publicada

Arquivo: `public/assets/audio/background-music.mp3`. MP3 estéreo, 48 kHz, **233,32 segundos** e **5.484.096 bytes**. SHA-256: `2e0b364e5f12991871e48c1fc9116e15e6b2a6d3d35e64ec5677506424830f73`.

O áudio foi preservado sem recompressão. Capa incorporada, capítulos e metadados privados foram removidos; a leitura integral passou e o PCM decodificado corresponde ao original. O master fornecido permanece preservado. A publicação decorre da solicitação explícita de colocar essa faixa no site, sem atribuir um título, autor ou licença musical que não foram informados. Esta integração não modifica as trilhas do jogo.

## Comportamento e limites

- Volume inicial de 25%, repetição ligada e tentativa normal de início automático. Se o navegador bloquear áudio, o visitante usa o botão de tocar; não há tentativa de contornar essa política.
- Pausa, volume, silêncio e posição ficam no armazenamento local do navegador, quando disponível. Ao abrir outra página, a integração tenta retomar a reprodução a partir da posição salva. A navegação completa entre páginas pode produzir uma breve interrupção e o navegador pode exigir outro clique.
- O estado também é reconciliado ao voltar para uma página restaurada pelo cache de navegação. Sem armazenamento local, os controles continuam funcionando na página atual.
- Controles e mensagens são traduzidos para PT-BR e EN. Os atalhos do player atuam somente quando ele tem foco. O player permanece disponível no rodapé da tela; há espaço abaixo do conteúdo para acessar o fim da página.

O publicador permite somente esse MP3 específico e a licença do player além dos tipos públicos anteriores. A troca de faixa deve substituir o arquivo autorizado, atualizar este registro e repetir a leitura do áudio, a revisão dos metadados e a verificação no navegador. O parâmetro de versão do áudio e dos scripts acompanha seus bytes.
