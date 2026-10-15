## O mapa editado no pacote

A local4 leva a cena de Tristram congelada em **14 de outubro, às 19h05**: posições, geometrias, remoções, 14 arquiteturas, 18 fontes de luz, 25 células ajustadas e quatro rotas. Edições posteriores a esse snapshot não estão incluídas. A iluminação é nativa; **18 ajustes de tamanho, especular e sombras do Godot permanecem pendentes**. A movimentação, colisão, personagens e saves continuam sob as regras nativas; o teste de carregamento não aprova todas as colisões das construções reposicionadas.

O download inclui executável, bibliotecas, recursos próprios, Warrior, HUD R4 e preparador interno. **Só é necessário fornecer o DIABDAT.MPQ de uma cópia original retail compatível do Diablo.** Não exige Python, compilador ou arquivos privados. Materiais originais são reconstruídos localmente; o pacote não contém MPQ, saves nem imagens originais extraídas.

## Warrior e correções

O Warrior Light com Short Sword e Buckler passou pelo desenho real em CPU em Tristram: corpo em terceira pessoa e braços/equipamento em primeira pessoa. O poço passou por quatro estados de materiais; o conjunto fez 130 verificações, com restauração do estado original da missão. Isso não prova a conclusão real da missão nem a aparência final em GPU.

A correção do cache da Catedral remove materiais fora do conjunto necessário ao quadro atual, mantendo o limite de 8 MiB. A candidata passou por **16 controles frios e 48 passagens/revisitas, com 2.023 verificações e nenhuma recusa**. A rota da versão de comparação não reproduziu a falha original: a reprodução específica do relato permanece inconclusiva.

As mudanças de controles preservam comandos nativos de caminhada, combate e interação e tratam a posse do mapa/UI. As etapas de Tristram passaram na fixture; **a sequência completa de controles na Catedral falhou com RefreshLiveFrameRefused** antes da avaliação desses controles. Essa recusa permanece em investigação. Não anunciamos a Catedral inteira como validada.

## Como jogar esta prévia

Baixe o ZIP completo na [release local4](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-preview.local4-tristram.full1), extraia tudo em uma pasta nova e abra **Iniciar-Diablo3D.cmd**. Selecione seu **DIABDAT.MPQ**. A primeira instalação reconstrói e confere os 19 arquivos da cena; instalação, reuso/preservação do perfil e recusa de cena alterada são verificados.

**K** alterna primeira/terceira pessoa, **Home** restaura terceira e **F4** alterna 3D/nativo. O perfil novo fica em `%LOCALAPPDATA%/Diablo3D-local4-Tristram/profile`, com Speed32; a local3 e seus saves não são substituídos.

Esta é uma **prévia**, com GPU obrigatória no launcher. Os resultados novos descritos acima são de CPU oculto e verificação de arquivos. GPU, FPS sustentado e gameplay físico da montagem não foram qualificados. HUD herdado não significa redesign nem baú novo.

Executável: `ffd8d95e0a610c7e64c04b0b7f4076b8064666af1cf969c33203ecbb3d1e533d`. Fonte privada de produção: `c433ca4137d248fdbbfcd462934786fd16aec6ef`; a string de versão embutida herdada ainda mostra `24080e8`. O código correspondente está no GitHub e na release; diferenças de CRLF/LF no repositório público não alteram o texto C++ correspondente.

Créditos e licenças originais preservados. Músicas personalizadas: **Douglas Pan**, também nas tags dos arquivos.
