# Continuidade do piloto da Catedral

Douglas relatou trechos em que o primeiro andar normal retorna automaticamente de 3D para 2D. Não conseguiu identificar um ponto específico. Uma causa foi confirmada no log e reproduzida: admissão de transparências por tela inteira apesar das cópias regionais. A correção está instalada; sair das nove regiões iniciais não explica o relato, porque o piloto seleciona novamente as regiões em torno do personagem. Outros motivos, incluindo a recusa inicial opaca de refresh, continuam separados.

O log anterior só descrevia o quadro ao final do desenho. Recusas anteriores a esse ponto podiam devolver o controle ao renderizador nativo sem registrar o contexto necessário. Um evento real de capacidade do cache GPU foi encontrado, seguido de recuperação; como essa linha não identifica o andar, não comprova a causa do problema na Catedral.

## Correção da admissão de transparências — 09/10/2026, 21:16 (Brasília)

Instalada no mesmo **Iniciar-Tristram.cmd**, SHA-256 `7896a85320d3cf71c6f9cef357bb7f29640dfd6f7a1b85ba92dbb165c279eb44`. O diagnóstico da partida registrou recusas por orçamento de overlays na seed `4241511855`. A admissão cobrava `width*height` por triângulo, apesar das cópias regionais; em Full HD, isso recusava o quadro após 258 overlays. Agora soma as mesmas caixas conservadoras armazenadas para `CopySubresourceRegion`, incluindo cópia completa quando necessária. Conserva **2.048 overlays/512 MiB**, ordem, shader, LUT e ownership de cor/depth/pick. `paletteBlendAdmittedBytes` mede o payload aceito; `paletteBlendCopyBytes` mede somente o emitido, sem representar VRAM total ou tráfego de barramento.

Na RX 570, seed `4241511855`, foco `[53,64]`, FOV 80 e quatro yaws diagonais com AA solicitado desligado/ligado: a baseline publicou seis casos e recusou dois no limite 258; o candidato publicou **oito de oito na GPU**, sem rasterização CPU/WARP. Chegou a 408 overlays com 19.556.601 bytes admitidos e emitidos iguais. O raster efetivo ficou 1× pelo limite anterior de 4 Mi pixels. São poses de estresse em fixture nativa parcial, sem alegar a pose humana exata, movimento físico ou FPS.

Passaram 3.428 checks sintéticos em nove frames e 2.095 checks do candidato nativo. Sete buffers sintéticos compartilhados de cor/pick/depth e as seis saídas nativas GPU comuns permaneceram byte-exatos. Witness/restauração conservaram mapa, SOL e RNG. A recusa inicial opaca de refresh e a composição artística das paredes continuam na fila. Evidência privada `diagnostics/camera-follow-root-integration-20261009-r1/palette-budget-root-execution-r5/execution-receipt.json`, SHA `34c53358e92b700f0caa4f0d2933aa581c029b88a87e5e76d46cd577396454d1`; instalação `diagnostics/camera-follow-root-integration-20261009-r1/install-preparation-r2/installed-20261010T001552Z-34e91bce3347499fa5f5e0133c2e4422/receipt.json`, SHA `136cdca5cae0db7be3099639b228683b28790d02fd209ffa7e198da19b18a1d0`. Não publicar os arquivos brutos.

## Diagnóstico antes da recusa

O diagnóstico acrescenta um registro em `Source/engine/render/town_view.cpp` e a interface de leitura `Source/engine/render/cathedral_fallback_diagnostic.hpp`. A política de fallback, os limites dos caches e a seleção de regiões permanecem os mesmos.

Cada observação conserva motivo e etapa, andar, seed, posição inicial e atual do episódio, epoch, estado do frame, limites e uso do cache de materiais e estado do backend GPU. Uma recusa na preparação de material inclui peça, eixo, coluna e célula. A mensagem do backend ocupa um buffer fixo de 192 bytes, com indicador de truncamento.

A captura usa uma estrutura trivial, consultável sem alocação por `GetTownViewCathedralFallbackDiagnostic()`. A formatação do log é separada e protegida contra exceções. Há no máximo uma tentativa de formatação por segundo; movimento e variação dos contadores não produzem uma linha nova para a mesma classificação. Transições muito rápidas podem ser agrupadas, mas a observação em memória continua atualizada.

`Published3D` significa que o desenho completo foi publicado. Preparação aceita não comprova recuperação. Home, F4 desligado e contextos intencionalmente nativos não são registrados como defeitos. `RefreshLiveFrameRefused` conserva a recusa pública sem inventar a causa interna de `cathedral_live`; uma eventual reprodução nessa etapa exige diagnóstico interno adicional.

## Teste e coleta

O executável é compilado e validado separadamente antes de substituir a versão do launcher habitual. A validação cobre falha de alocação na preparação, consulta do diagnóstico sob pressão de memória, formatação real da linha, repetição limitada e preservação do estado nativo. Dois pontos de falha controlada na GPU verificam captura anterior à limpeza e recuperação no quadro seguinte. Isso verifica o diagnóstico; não reproduz automaticamente o defeito observado durante gameplay.

A instalação histórica do diagnóstico em 9 de outubro usou SHA-256 `2ff14e24d597a667c794c82f448b218d37a44d9833409efff6ba2d6432dbe89a`; a versão atual é a correção identificada acima. Passaram 121 verificações CPU e 196 GPU na RX 570; os recibos e limites estão no guia de execução. O teste utiliza o mesmo **Iniciar-Tristram.cmd** e o primeiro andar normal da Catedral. Não é necessário memorizar porta, corredor ou coordenadas: os registros `Cathedral pilot diagnostic:` identificam o contexto disponível. O log local é `perfil-tristram/prototipo.log`, fora do repositório; a inicialização seguinte pode substituí-lo, portanto a coleta deve acontecer antes de abrir outra sessão. Não publicar o arquivo bruto.

## Paredes: investigação distinta

O material atual repete a primeira faixa MIN superior utilizável e pode recorrer a um doador por eixo. O oracle anterior verificou os texels desse material derivado, sem comprovar a composição completa da parede original. A frente procedural prepara um ledger privado por triângulo e material, conservando as 16 palavras MIN, as camadas ativas e as diferenças entre peça solicitada e fonte efetiva.

Um vínculo presente no frame não comprova submissão à GPU nem visibilidade de pixels. O ledger mantém esses campos desconhecidos e não distribui recursos ou pixels proprietários. A correção de composição terá validação própria por peça e face; não será anunciada como parte do diagnóstico de continuidade.

O estado de compilação, execução, instalação e próxima ação está registrado em [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md).

## Grades, candelabro e nomes de itens — instalação de 10 de outubro

**Instalado em 10/10/2026, 00:50:47 (Brasília)** no mesmo **Iniciar-Tristram.cmd**, SHA-256 `1fe70560e8eb50509ddb440deccba7d1afd0f7ae6dcb6027b2ae06d3379018d6`, 6.734.336 bytes. Build, legendas e validação nativa delimitada passaram. O conjunto estrito continua sem admissão, separado do domínio conhecido aprovado; detalhes no [guia de execução](PROJECT-EXECUTION.md).

### Escopo das grades

O caminho padrão reconhece somente RAW35/36 carregados do TIL nativo, com os quatro filhos corroborados pelas peças vivas do mapa. Não repete o decremento de índices já feito pela geração. Usa as duas colunas próprias não piso; filhos de piso não se tornam paredes.

A R4 posiciona os painéis pelo retângulo completo de uma travessa externa original. A pequena projeção dos barrotes internos não aumenta esse retângulo. Exige a transformação, UVs e ordem da geometria original completa; não inventa profundidade nativa ou altera a malha física.

As máscaras Left/Right preservam a combinação nativa de escrita opaca e mistura de paleta. Os passes visuais **Solid** e **PaletteBlend** recebem coberturas disjuntas cuja união equivale à cobertura bruta; a mistura usa a tabela destino/origem. Os códigos de paleta 0 e 255 não decidem se um texel existe.

Os painéis substituem visualmente as barras técnicas, conservando triângulos físicos, células de origem, IDs, picking, colisão e comandos. A aplicação revalida o conjunto e rejeita a revisão antiga da política. Contextos com objetos ou decoração especial nos filhos continuam exigindo fallback nativo completo. Outras famílias de grade e níveis de missão não estão abrangidos por essa regra.

### Candelabro e demais paredes

A supressão técnica do candelabro exige peça 269 viva e `OBJ_L1LIGHT` ativo na mesma origem, corroborados pela ocupação assinada `dObject` e pela lista nativa de objetos. Esconde somente a parede técnica duplicada. Objeto, animação, luz, colisão e comandos continuam nativos; o desenho conserva o sprite, sem uma nova malha completa de candelabro. `applyLighting` indica sombreamento do sprite, não emissão da luz.

Pilares e estruturas de arco (`Pillar`/`ArchFrame`) recebem vínculos Masonry por face, com célula, peça, coluna MIN, eixo e doador identificáveis. Isso não conclui sua arte. **Alvenaria restante, faixas repetidas, doadores, fundos, tampas e portas continuam aproximações; alturas e silhuetas não são finais.** A substituição integral deste incremento é delimitada às grades conhecidas. O experimento geral de parede integral não é ativado no caminho padrão.

### Escuridão e luz nativa

Masonry é sombreado na preparação usando `dLight`, as tabelas de paleta e a interpolação nativa. `Lightmap::buildLocal` usa armazenamento do chamador sem escrever no buffer global nativo. Com iluminação por pixel desligada, nível 0 conserva a cor bruta e nível 15 pinta preto nos texels cobertos. Com a opção ligada, utiliza a interpolação e o bleed nativos. Uma faixa ou coluna pode continuar legitimamente preta; não é clareada para esconder esse comportamento da referência.

O oracle real de luz local encontrou uma célula de luz 15 com 3.648 pixels cobertos pretos em OFF e um pixel coberto não preto em ON. Algumas colunas das grades permaneceram pretas mesmo em ON, coincidindo com o desenho original. Esses escopos são separados; não se exige contraste em toda coluna, nem se altera `dLight`, SOL ou `TransList` para fabricá-lo.

Materiais são cacheados na preparação, sem decodificação CEL por triângulo desenhado. Cache limitado, invalidação e fallback permanecem. Não há novo sistema de dia/noite, iluminação física geral ou ganho de FPS medido nesta entrega.

### Evidência executada e positivo ausente

Na seed 2588, o subgate de paredes publicou **12 quadros, seis na Radeon RX 570 sem WARP**, com 1.231.352 verificações de regras. Conferiu frame interno, fontes nativas, máscaras, pixels, repetição quente e witness físico/nativo. O gate geométrico de travessas verificou cada orientação RAW35/36: 566 checks, 84 triângulos originais e 177 entradas negativas rejeitadas por orientação. Esse gate geométrico, isoladamente, não prova pixels ou profundidade.

O gate delimitado passou **2.901.936 checks** em 28 colunas, 14 macros e 11 relatórios: 286.720 comparações de cor e 145.408 de operação. RAW35/36, `TransList` ON/OFF, iluminação por pixel ON/OFF, FullyDark OFF, piso/folhagem, repetição quente e texels brutos cobertos com código 0 foram alcançados. Materiais atuais foram comparados ao `DrawCell` original e ao trace independente; frame reconstruído separadamente não substituiu essa prova.

**O conjunto estrito permanece `UNSUPPORTED`: não apareceu texel bruto 255 coberto nas colunas próprias — peças 61/coluna 0, 62/0, 63/1 e 47/1.** Seu helper congelado ficou intacto. A execução final encerrou com sucesso para o domínio conhecido `PASS_KNOWN_RAW35_36_COLUMN_DOMAIN`, com `strictSuitePass=false`; isso não transforma o estrito em PASS.

Os histogramas completos das quatro chaves próprias, 5.120 pixels cada, confirmaram essa ausência sem nova decodificação. Esse resultado vale para essas colunas e revisão de dados, não para todos os recursos do jogo.

Separadamente, a unidade de decoder/cobertura sintética passou **44.300 checks**: dois fundos, 256 códigos literais e quatro mapas de operação nativos, com 20.480 pixels de partição e witness exato. Exercitou 0/255 sintéticos, **sem provar um texel 255 nativo real**. Comparar pintura sobre fundos 0/255 também não equivale a observar esse positivo. Histogramas, unidade sintética, admissão delimitada e estrito continuam registros distintos.

Build e legendas finais passaram: core 253 checks/14 quadros/três GPU/seis PNGs; FPP 3.851 checks totais, incluindo 47 do companheiro em três quadros CPU e um gesto de captura com serviços físicos simulados. Não comprovam captura física, coleta ou HUD completo. Veja [3D-ITEM-LABELS.md](3D-ITEM-LABELS.md).

A instalação preservou 639 arquivos protegidos por bytes/tamanho/data; renovou somente os três executáveis e `executable.sha256`/`recordedUtc` do recibo derivado. Os modelos selecionados, configurações, saves e launchers foram conservados. Nenhum processo foi encerrado. Recibo privado `diagnostics/item-labels-20261009-r1/install-preparation/installed-20261010T035047Z-34cf23dbd66a424f9da9f49b1e82e2c2/receipt.json`, SHA `0d710e4b476eb72766651d7c367f9faa54c212806c99e1ef9c6d8c3bdb7010ae`. Revisão física/visual continua pendente. Não há aprovação de toda Catedral, de suas alturas/silhuetas, de todos os contextos de fallback, demais andares, gameplay integral ou FPS sustentado. Os próximos passos permanecem na mesma fila G2/piloto G3. Recibos, MPQs, pixels brutos, perfis e saves ficam privados.
