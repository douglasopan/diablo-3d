# Continuidade do piloto da Catedral

Douglas relatou trechos em que o primeiro andar normal retorna automaticamente de 3D para 2D. Não conseguiu identificar um ponto específico. O defeito está aberto: sair das nove regiões iniciais não explica o relato, porque o piloto seleciona novamente as regiões em torno do personagem.

O log anterior só descrevia o quadro ao final do desenho. Recusas anteriores a esse ponto podiam devolver o controle ao renderizador nativo sem registrar o contexto necessário. Um evento real de capacidade do cache GPU foi encontrado, seguido de recuperação; como essa linha não identifica o andar, não comprova a causa do problema na Catedral.

## Diagnóstico antes da recusa

O diagnóstico acrescenta um registro em `Source/engine/render/town_view.cpp` e a interface de leitura `Source/engine/render/cathedral_fallback_diagnostic.hpp`. A política de fallback, os limites dos caches e a seleção de regiões permanecem os mesmos.

Cada observação conserva motivo e etapa, andar, seed, posição inicial e atual do episódio, epoch, estado do frame, limites e uso do cache de materiais e estado do backend GPU. Uma recusa na preparação de material inclui peça, eixo, coluna e célula. A mensagem do backend ocupa um buffer fixo de 192 bytes, com indicador de truncamento.

A captura usa uma estrutura trivial, consultável sem alocação por `GetTownViewCathedralFallbackDiagnostic()`. A formatação do log é separada e protegida contra exceções. Há no máximo uma tentativa de formatação por segundo; movimento e variação dos contadores não produzem uma linha nova para a mesma classificação. Transições muito rápidas podem ser agrupadas, mas a observação em memória continua atualizada.

`Published3D` significa que o desenho completo foi publicado. Preparação aceita não comprova recuperação. Home, F4 desligado e contextos intencionalmente nativos não são registrados como defeitos. `RefreshLiveFrameRefused` conserva a recusa pública sem inventar a causa interna de `cathedral_live`; uma eventual reprodução nessa etapa exige diagnóstico interno adicional.

## Teste e coleta

O executável é compilado e validado separadamente antes de substituir a versão do launcher habitual. A validação cobre falha de alocação na preparação, consulta do diagnóstico sob pressão de memória, formatação real da linha, repetição limitada e preservação do estado nativo. Dois pontos de falha controlada na GPU verificam captura anterior à limpeza e recuperação no quadro seguinte. Isso verifica o diagnóstico; não reproduz automaticamente o defeito observado durante gameplay.

A instalação validada em 9 de outubro usa SHA-256 `2ff14e24d597a667c794c82f448b218d37a44d9833409efff6ba2d6432dbe89a`. Passaram 121 verificações CPU e 196 GPU na RX 570; os recibos e limites estão no guia de execução. O teste utiliza o mesmo **Iniciar-Tristram.cmd** e o primeiro andar normal da Catedral. Não é necessário memorizar porta, corredor ou coordenadas: os registros `Cathedral pilot diagnostic:` identificam o contexto disponível. O log local é `perfil-tristram/prototipo.log`, fora do repositório; a inicialização seguinte pode substituí-lo, portanto a coleta deve acontecer antes de abrir outra sessão. Não publicar o arquivo bruto.

## Paredes: investigação distinta

O material atual repete a primeira faixa MIN superior utilizável e pode recorrer a um doador por eixo. O oracle anterior verificou os texels desse material derivado, sem comprovar a composição completa da parede original. A frente procedural prepara um ledger privado por triângulo e material, conservando as 16 palavras MIN, as camadas ativas e as diferenças entre peça solicitada e fonte efetiva.

Um vínculo presente no frame não comprova submissão à GPU nem visibilidade de pixels. O ledger mantém esses campos desconhecidos e não distribui recursos ou pixels proprietários. A correção de composição terá validação própria por peça e face; não será anunciada como parte do diagnóstico de continuidade.

O estado de compilação, execução, instalação e próxima ação está registrado em [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md).
