# G1 — revisão reproduzível da cabana leste

Esta entrega continua a fila de [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md): revisar a cabana selecionada e corrigir somente defeitos reproduzidos. Exterior, tonalidade e direção de luz já têm aprovação parcial. O conjunto completo e Tristram continuam em revisão; nenhum asset foi regenerado ou promovido a `accepted`.

## Identidade antes da revisão

Em 8 de outubro de 2026, o modelo fonte e a cópia do perfil habitual coincidiram com a seleção explícita de `assets/runtime-baseline.json`:

| Componente | Identificação |
| --- | --- |
| Asset / variante / revisão | `tristram.arch.cabin` / `east-long` / `meshy-v2-multiview` |
| SHA-256 do modelo | `d12cc57e798151fb4abf3173149a4d7cf1da2cc8bdbc6ac39439aee3fe9069f9` |
| SHA-256 do perfil de luz | `844f90ac50449287e6314419de1c2945587d9b84688b88fd0292b3781cffca8a` |
| Adjuntos do executável | `cabin-east-openings-fire-v3`: interior, dois vãos redondos, janela da porta, piso e duas velas |
| Geometria montada | 5.783 triângulos exteriores, 838 interiores e 144 de fogo |
| SHA-256 da geometria JSON montada | `44a52611a708010b1f770eb8ece9fea49b777eaf2230481611ef083e4e37ff8b` |

O snapshot registra o SHA dos bytes efetivamente decodificados, além do recibo do launcher. A regressão anterior à correção passou com dez poses da cabana (`-5`, `0`, `5`, `45`, `90`, `135`, `180`, `225`, `270`, `315` graus), geometria forçada, câmera e fogo congelados. Não houve buracos de cobertura nas amostras independentes. Isso verifica a rasterização e a identidade; não é aceitação artística da mesh. Home continua sendo somente o controle do backend original.

Evidência local anterior: `diagnostics/tristram-g1/baseline-20261008` e `snapshot-20261008`. O snapshot tem SHA-256 `2b0a26c6d03f273f70c9b1a8de4cdb490539889a87d057b10a013a4da6676d3a`. Esses diretórios estão fora do repositório público e contêm arte privada.

## Defeito registrado: madeira que deixava passar a luz

As divisões das duas janelas redondas já eram caixas de madeira reais. Elas bloqueavam a visão, porém a iluminação das velas consultava somente a caixa do quarto e seus portais. Um receptor no túnel podia receber luz através de uma dessas barras opacas.

Caso reproduzível no objeto realmente carregado:

- Fonte: vela frontal em `(70.8499985, 1.60500002, 70.8880005)`.
- Receptor no túnel: `(71.80229, 2.26367034, 71.59660148)`.
- Interseção anterior ao receptor com a barra real: `(71.70879118, 2.19900012, 71.52702892)`.
- Controle livre no mesmo triângulo do túnel: `(71.79857893, 2.28710413, 71.53200023)`.

A auditoria identificou 24 amostras com esse problema. A normal do receptor recebe a luz e a distância fica dentro do raio da vela; a contribuição não era apenas uma possibilidade teórica da API. Nos testes adicionais, 911 amostras contra o exterior importado não revelaram bloqueios omitidos pelo modelo, e 988 amostras superiores não ultrapassaram o envelope do telhado por mais de 0,01 unidade. Não havia justificativa para gerar outra cabana.

## Correção e fronteira entre componentes

`town_scene` registra as mesmas quatro caixas usadas para construir as barras como shells opacos, depois do shell do quarto. As posições, a geometria, os materiais e os UVs permanecem iguais. `town_view` passa essa lista ao sampler CPU e ao material GPU. O backend Direct3D copia os limites durante `Submit`, testa o mesmo segmento com as mesmas tolerâncias e rejeita limites inválidos, quantidade excessiva ou aberturas declaradas numa barra opaca.

O contrato GPU comporta até 31 bloqueadores adicionais ao quarto, dentro do limite CPU de 32 oclusores. O caso atual usa quatro. Nenhuma busca por triângulos ou alocação foi adicionada ao loop de pixels; as caixas vêm da construção da cena. O formato de modelo continua sendo D3DMESH1 e a pipeline v3 recebe uma correção de comportamento, sem troca de revisão artística.

Os detalhes importados da porta continuam bloqueando a visão. Suas sombras individuais ainda não são representadas por esses quatro bloqueadores; esta correção se limita às barras autoradas das janelas redondas. Também permanecem pendentes os demais móveis/props, luzes autoradas pelo Godot e aberturas inclinadas de telhado.

## Diagnóstico de revisão

O novo modo `town_view_smoke ... --cabin-review` exige a baseline importada pelo hash real e produz:

1. Casos CPU/GPU de bloqueio e passagem livre, incluindo os dois receptores acima, contato com a superfície, limites e validade dos dados.
2. Dez poses em CPU e GPU de hardware, sempre com geometria forçada e fogo em tempo zero.
3. A/B na mesma câmera com sombras direcionais físicas ligadas/desligadas. Cor muda; profundidade, dono arquitetônico e seleção devem permanecer exatamente iguais. A alternância não remove sombras nativas pintadas nem modifica a iluminação das velas.
4. Retorno exato à imagem congelada, invariantes de mapa/colisão/RNG/atores e proteção das linhas da interface.
5. Opacidade RGBA da textura de chão efetiva, comparada à cobertura independente das primitivas originais. As oito máscaras e 80 controles mantêm os pixels não selecionados, incluindo preto opaco e transparência.

`tools/check_ground_shadow_pixels.py --require-effective-opacity` exige essa leitura RGBA e falha se ela estiver ausente ou divergente. Capturas indexadas antigas continuam utilizáveis, com a ausência de evidência completa de opacidade declarada no relatório.

Não ampliar máscaras de chão apenas porque uma área é escura. O painel `diagnostics/shadow-audit/cabin-eight-mask-review/ground-shadow-mattes.png` é histórico anterior à correção das oito máscaras: sua coluna de resultado esperado não é uma captura do runtime. A transição de terra/grama junto ao poço continua preservada.

## Resultado e próxima ação

A rodada final passou em 8 de outubro de 2026: **2.407 verificações** do modo de revisão, 18 cenários válidos e oito rejeições de bloqueadores CPU/GPU, 20 poses A/B efetivamente capturadas em 640×640, zero buracos de cobertura nas amostras, retorno exato de cor/depth/picking e invariantes de mapa/colisão/atores/RNG. O fixture GPU anterior também passou em **101 verificações**, na Radeon RX 570, com WARP desligado. Os três controles negativos do verificador de opacidade passaram: um buraco artificial de alpha é recusado mesmo com os índices de cor intactos, evidência RGBA ausente é recusada quando exigida, e `source-only` não pode declarar leitura de opacidade efetiva.

As oito máscaras e 80 controles têm **zero divergência de cor ou opacidade** na leitura RGBA efetiva. Não foi acrescentada nenhuma máscara nem removido terreno escuro sem defeito demonstrado. Desligar as sombras físicas muda entre 815 e 6.254 pixels de chão próximo da cabana, conforme a pose; os mesmos pixels mantêm profundidade e seleção. Essa contagem mostra que o A/B exercitou sombra real; não aprova todo material escuro da cidade.

O snapshot completo anterior e posterior é byte a byte idêntico. Na comparação CPU da mesma câmera com a baseline, oito poses permaneceram idênticas; a pose de 90° mudou cinco pixels e a de 270° mudou um pixel, nos túneis das janelas. Fonte, UVs, telhado, base, luz ambiente, velas e demais objetos permanecem preservados. A leitura do quarto iluminado/desligado e a janela da porta também conservaram seus controles.

O HUD integrado passou a expor mais mundo abaixo dos controles. Por isso o fixture declara explicitamente seu recorte de 640 linhas, mantendo a câmera e o recorte históricos sem alterar a política de viewport do produto. A prévia `window-bars-20261008` tem 640×768 e fica identificada como preliminar; a evidência final é `diagnostics/tristram-g1/window-bars-final-20261008`, especialmente `validation-summary.json`, `cabin-review.json`, `ground-pixel-check.json` e os diretórios `cpu`/`gpu`. A regressão sintética anterior está em `gpu-fixtures-final-20261008`, e os controles negativos em `opacity-negative-checks.json`.

| Binário da compilação integrada validada | SHA-256 |
| --- | --- |
| `town_view_smoke.exe` | `c0343990a2e2f8bcb90571cb06ce270957e0ff673ea53313c941d1cbf8413961` |
| Candidato `devilutionx-tristram-godot.exe` | `ef9a20c7e6b9cb5b700a24d469427e535f78bde2bc51e358d2c975567cc4c2e7` |

Os testes são offscreen no código real da engine. Não validam uma janela física, FPS sustentado ou aprovação artística integral. Compilações e testes de outras frentes ocorreram no mesmo período; a duração dos diagnósticos não é uma medição de desempenho. Este chat não instalou aliases, alterou perfis habituais, fez staging ou publicou arquivos; a integração e seus recibos pertencem ao chat do jogo.

Depois da validação técnica, revisar com o usuário as capturas da mesma câmera e do giro, concentrando a aprovação restante em base, interior, janelas, porta e telhado. Não declarar G1 ou Tristram concluídas com base nos fixtures técnicos.
