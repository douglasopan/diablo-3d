# Renderização 3D na GPU

O piloto Windows usa **Direct3D 11 dentro do DevilutionX**. A simulação, o mapa, a colisão, os saves e a seleção dos modelos continuam no mesmo projeto. Não há migração para outra engine. O caminho original permanece autoritativo em Home e nos níveis ainda não reconstruídos.

## Usar e comparar

Na revisão atual, durante a partida: **Esc → Configurações → Gráficos → 3D GPU Rendering** (em inglês: Settings → Graphics). O menu também contém **3D Frustum Culling**, **3D Edge Smoothing** e a correção de brilho. A paginação é responsiva: até oito entradas em 960×540 e 18 em 1920×1080, com navegação num rodapé horizontal. GPU e suavização são independentes, persistem no INI do perfil e mudam no próximo desenho. Fora da partida, ambos estão em Settings → Graphics. O padrão público da GPU é desligado; a preparação de um perfil existente preserva suas escolhas. O antigo caminho Options → Video Options pertence às revisões anteriores.

F4 habilita a visualização de Tristram. Gire a câmera para ver a reconstrução; Home restaura a imagem original. O HUD identifica GPU ativa, CPU selecionada ou fallback. Quando o backend falha, a imagem inteira é refeita pela CPU; nunca se publica cor de um frame com seleção/profundidade de outro. Desligar e ligar a opção permite uma nova tentativa; recarregar os recursos também libera o bloqueio.

O recibo do iniciador inclui `3D GPU Rendering` entre dez solicitações gráficas. Ele registra a preferência, não prova qual adaptador desenhou o quadro.

## Fronteira implementada

Esta fronteira inclui a integração de malhas residentes validada e instalada em 8 de outubro no iniciador habitual. O recibo desse incremento está na seção de volumes residentes; os resultados anteriores permanecem como histórico.

1. `town_view` lê a mesma cena e prepara câmera isométrica ou perspectiva, materiais, luzes e IDs. Os quatro modos estão documentados em [Câmeras e horizonte](TRISTRAM-HORIZON-CAMERAS.md). Arquitetura, cenário individual e atores conservam a preparação de triângulos recortados pela CPU. Props e vegetação usam geometria residente somente nas câmeras em perspectiva; a isométrica mantém o caminho anterior.
2. `town_gpu` mantém buffers imutáveis de vértices/índices para esses volumes residentes e executa câmera, projeção, recorte e shade no shader. A geometria restante continua no buffer de triângulos por frame, com comandos consecutivos compatíveis agrupados. Os dois caminhos preservam a ordem de submissão, materiais, sombras e IDs.
3. Shaders produzem cor indexada, ID e profundidade em três alvos, com depth test e recortes de opacidade. Índice zero pintado continua opaco quando uma máscara o declara assim. Iluminação importada, fogo e sombras usam as mesmas tabelas e coordenadas da referência CPU.
4. A ponte atual lê os três alvos de volta. A composição da interface e os acessores de seleção continuam usando os buffers herdados. Em 2×, a redução traz ID e profundidade da mesma subamostra mais próxima; o compositor SDL pode preservar a cor 2× até a saída.

Uma textura de paleta compartilhada elimina trocas de textura para cada cor constante. Nas faces de volumes, o shader une a base sólida e seu sprite recortado em um único passe, incluindo o shade da base. O caminho CPU conserva seus dois passes. Isso reduz comandos sem ordenar novamente faces sobrepostas.

Uploads têm identidade/revisão explícita e copiam os dados emprestados durante a submissão. Texels/máscaras e LUTs de iluminação têm caches separados: a LUT global importada é compartilhada sem duplicar os atlas quando cresce. Recursos usados no quadro ficam protegidos até sua conclusão; descarte respeita o orçamento de payload de 256 MiB. A geometria indexada possui um cache independente de 256 MiB, com descarte por uso recente e proteção das malhas referenciadas no quadro. Esses orçamentos não representam toda a memória do dispositivo. Reset libera dispositivo, caches e alvos. O mapa estático de sombras ainda é construído/cacheado pela CPU; a amostragem e a iluminação por pixel ocorrem no shader.

## Validação reproduzível

Compile com `build.ps1 -WithSmoke`. O Windows SDK fornece os headers e as bibliotecas Direct3D; não há pacote bgfx neste piloto.

```powershell
build\town_view_smoke.exe --gpu-fixtures diagnostics\gpu-fixtures
build\town_view_smoke.exe <dados-do-jogo> build\assets <diagnostico-com-baseline-local> --gpu
build\town_view_smoke.exe <dados-do-jogo> build\assets <diagnostico-com-baseline-local> --resident-meshes
build\town_view_smoke.exe <dados-do-jogo> build\assets <diagnostico-com-baseline-local> --resident-fullhd
```

O primeiro comando usa dados sintéticos. Os demais exigem GPU de hardware e dados locais do jogador. Para testar o modelo selecionado, o diretório diagnóstico precisa conter `d3d-models/cabin-east.d3d` e `d3d-lighting.ini` verificados, sem publicá-los. WARP só é permitido explicitamente nos fixtures sintéticos; produção não o usa como fallback silencioso.

Os testes cobrem readback com pitch, máscaras, cor zero, repetição de UV, iluminação, IDs, decal preservando seleção, recursos temporários, resize/reset, persistência dos callbacks reais do menu e retorno CPU → GPU → CPU. A comparação real registra câmeras, densidade, adaptador, comandos e o custo completo de `DrawTownView`, incluindo readback/redução. Esse tempo exclui a interface e a apresentação SDL; não é FPS sustentado da partida.

## Resultado histórico do piloto em 8 de outubro de 2026

Windows, Intel Core i7-14700, Radeon RX 570 (hardware, sem WARP), mesma cabana/luz locais selecionadas e fogo congelado. Medianas de cinco chamadas mornas por backend naquela rodada do piloto:

| Viewport/qualidade | CPU | GPU | Limite da comparação |
| --- | ---: | ---: | --- |
| 640×352, 1×, geometria forçada na câmera original | 24,8 ms | 8,1 ms | Mundo, sem UI/SDL. |
| 960×540, 2× por eixo | 191,8 ms | 40,9 ms | Inclui redução de cor e seleção. |
| 1920×1080, 1×, zoom nativo ligado | 154,0 ms | 29,6 ms | Cerca de 5,2× menos tempo no desenho do mundo. |

Os tempos variam entre rodadas na máquina compartilhada; não são uma garantia de FPS. Na comparação Full HD, o primeiro piloto enviava 38.633 comandos para 38.978 triângulos; o caminho final envia 4.313 comandos para 27.386 triângulos, com a fusão dos passes. As seis capturas GPU permaneceram exatamente iguais após essas otimizações.

Os seis casos incluem órbita de 90°, painel/pan/zoom, dimensões ímpares e mundo 2×. Zero divergências sem explicação geométrica: 108 diferenças de ID vieram de bases de chão coplanares, confirmadas por raios no plano y=0 e diferenças de profundidade ≤0,0001; uma subamostra de árvore foi explicada pela cobertura de vértices quantizados a 1/256 de pixel de raster, confirmando os dois mínimos de profundidade por raios independentes. Entidades/arquitetura não recebem uma tolerância genérica de ID. Imagem e seleção retornaram exatamente à referência CPU após desligar GPU; Home manteve o backend original exato.

A rejeição real de um viewport 2304×2048 excedendo o orçamento foi exercitada: nenhum triângulo enviado, quadro CPU completo idêntico, bloqueio repetido seguro e recuperação do hardware após OFF/ON. Passaram também fixtures sintéticos, regressão, qualidade, compositor SDL e os 25 testes do launcher em PowerShell 5.1. Evidências privadas: `diagnostics/gpu-pilot/final`, `fixtures-fused.log`, `quality`, `regression`, `presentation-layers.log` e `runtime-baseline-gpu-tests`.

O executável habitual e o alias de qualidade receberam o mesmo build. O perfil local permanece em 1920×1080, Zoom ligado e suavização desligada; sua única mudança foi `3D GPU Rendering=1`. Modelo, luz e save conservaram hash, tamanho e data. Isso não promove o modelo a aceitação artística integral. Após os testes técnicos, o usuário experimentou a versão GPU na partida habitual e relatou “melhorou muito!!”. Isso confirma uma melhora percebida de desempenho, sem acrescentar uma medição de FPS ou aprovar integralmente os modelos. A medição de FPS sustentado, a revisão específica da interface e o acompanhamento em uso prolongado continuam pendentes; o diagnóstico não abre a janela da partida.

## Limites do incremento

- Backend de hardware implementado para Windows/Direct3D 11; outras plataformas continuam na CPU.
- A leitura síncrona dos três alvos e a composição lógica ainda têm custo. Apresentação direta, seleção assíncrona e novos passes de sombra são possíveis incrementos posteriores, após medir o gargalo restante.
- D3D usa cobertura top-left e comparação de profundidade exata; a CPU aceita pequenas tolerâncias. Bordas, subamostras e bases de chão coplanares precisam ser classificadas separadamente na comparação. Não se afirma identidade de todos os pixels/tiles entre rasterizadores.
- A suavização conserva o orçamento de 4.194.304 amostras; 1920×1080 recua a 1×. O backend GPU não altera essa política.
- O alvo GPU também tem limite de 4.194.304 pixels; áreas lógicas maiores usam CPU com motivo explícito. Não se promete suporte GPU a 4K neste incremento.
- Não adiciona arte HD do Belzebub, novos modelos, escala independente de menus ou renderização dos níveis procedurais. Esses trabalhos seguem seus marcos no [guia de execução](PROJECT-EXECUTION.md).

## Cache e visibilidade em primeira pessoa — 8 de outubro

O diagnóstico com o pacote atual de Tristram reproduziu `GPU texture-cache budget exceeded; reset required` em 1920×1080/FOV 80. A opção solicitava GPU, mas o bloqueio mantinha os quadros seguintes na CPU. A causa era a duplicação da LUT global por textura e dos texels imutáveis quando a revisão da LUT crescia. A correção separa essas identidades, preserva snapshots referenciados pelos comandos do quadro e prepara o albedo visível antes de abrir o frame GPU. Não reduz malhas nem texturas.

O fixture isolado na RX 570 passou 239 verificações, incluindo crescimento/revisões de LUT durante o quadro, máscaras, namespaces privados, pressão de memória, descarte protegido e falha atômica. No caso sintético de nove texturas, LUTs que totalizariam 288 MiB passaram a compartilhar 32 MiB; quadros aquecidos não fizeram uploads de texels/LUT. O orçamento contabiliza payload com padding do cache, não toda a memória do dispositivo. A regressão de perspectiva passou 49.007 verificações.

O descarte conservador de arquitetura usa limites reais do modelo antes de preparar texturas ou percorrer faces; volumes também são rejeitados em perspectiva. A opção **3D Frustum Culling** está ligada por padrão e permite comparação. O cache de limites acompanha a revisão da cena. Sombras direcionais continuam incluindo estruturas fora da câmera. A travessia dos tiles mantém a ordem anterior sem alocar e ordenar a grade a cada quadro.

Na rodada atual **`runtime-r3`**, passaram os **16 casos em 960×540**: quatro modos de câmera × CPU/GPU × suavização desligada/solicitada. Cor, profundidade, seleção e mapa de sombras permaneceram exatos com o descarte ligado/desligado; estado nativo, RNG e reconstrução após reset também foram preservados. Nos quadros GPU aquecidos, os uploads de texels e LUT foram **zero bytes**.

A rodada atual **`first-person-fullhd-r4`** passou em primeira pessoa, **1920×1080/FOV 80**, com hardware efetivo e sem fallback. Os dois casos de suavização tiveram zero upload de texels/LUT nos quadros aquecidos e **191.379.586 bytes de payload no cache**. Nessa câmera, as visitas a triângulos da arquitetura caíram de 82.626 para 54.579. O orçamento mede o payload do cache, incluindo padding, e não toda a memória do dispositivo.

As medianas exploratórias de quatro pares AB/BA nessa rodada ficaram em **419,455 → 377,280 ms** com suavização desligada e **1.125,800 → 985,236 ms** com ela solicitada. A amostragem efetiva foi 1× nos dois casos; Full HD limita a solicitação 2×. A rodada histórica `first-person-fullhd-r3` havia medido 275,8 → 240,4 ms e 263,4 → 229,9 ms, respectivamente. A variação entre rodadas na máquina compartilhada impede tratar essas medidas como uma comparação controlada de versões. São tempos do desenho isolado, sem UI/apresentação SDL; não comprovam FPS sustentado ou uma meta de desempenho atendida.

Evidências privadas dessa rodada: `diagnostics/render-optimization-20261008/runtime-r3` e `diagnostics/render-optimization-20261008/first-person-fullhd-r4`. Evidências históricas preservadas: `runtime-r2`, `first-person-fullhd-r3`, `diagnostics/gpu-shared-lut/20261008-071507` e `perspective-regression-r1`. A revisão foi instalada às 10:45:49 UTC, SHA-256 `a1e218fab604517f409557a5863352633b632f1f83e3f3c449b06393418140c9`, no mesmo iniciador habitual. Recibo: `diagnostics/render-optimization-20261008/installed-20261008T104549Z/receipt.json`; modelos, luz, músicas e saves preservados. O profiling independente confirmou a prioridade seguinte: buffers persistentes e agrupamento de volumes. Readback e hashing das sombras continuam custos conhecidos. LOD, malhas persistentes e materiais novos ainda não integravam aquela entrega; a integração posterior de volumes residentes está descrita a seguir.

## Malhas residentes de props e vegetação — 8 de outubro

O incremento conecta `town_gpu_mesh.hpp` e `town_gpu_mesh_backend.inc` ao backend e à cena real. Props e árvores em perspectiva são enviados uma vez ao cache de vértices/índices e desenhados com comandos indexados, sem percorrer e submeter cada face pela CPU a cada quadro. O shader conserva a paleta por face, o sprite frontal com sua base sólida, o shade legado, a rejeição de faces traseiras, as sombras e a seleção. A projeção nativa preserva a ordem aritmética da câmera CPU. Não houve redução de malha, alteração de modelos, texturas, luzes, colisão ou saves para obter os resultados abaixo.

O cache de geometria admite 256 MiB de payload. Nas cenas medidas, ocupou aproximadamente 45–79 MiB; os quadros aquecidos tiveram **zero bytes de upload de geometria**, além de zero upload de texels/LUT. Sair do enquadramento permite rejeitar o desenho sem destruir imediatamente a malha: o cache a retém até pressão de orçamento ou reset. Isso não implementa LOD, oclusão, materiais PBR nem carregamento de masters mais densos.

O escopo é deliberadamente limitado a **props e vegetação em perspectiva**. Construções importadas, cenário individual e atores continuam no caminho projetado anterior. A isométrica também permanece nele: a rodada `r8-dense-guard` encontrou um pixel ortográfico com cor/seleção/profundidade diferentes que não foi explicado pelas referências subpixel. A causa exata permanece em investigação; a paridade desse caminho não foi declarada nem se aumentou a tolerância para habilitá-lo.

A rodada `r9-perspective-scope` passou os quatro modos em 960×540, com suavização desligada e solicitada. Nos oito casos, a imagem, IDs e profundidade residente foram exatos entre cache frio e aquecido, e o estado nativo, RNG e mapa de sombras foram preservados. Contra o caminho projetado, sete casos tiveram cores e IDs centrais exatos. Na terceira pessoa sem suavização, **um pixel** teve diferenças centrais e foi classificado pela referência antiga deslocada em no máximo **1/256 de pixel de raster por eixo**. A classificação exige que **o mesmo pixel** de uma única referência corresponda conjuntamente em cor, identidade semântica e profundidade (tolerância de profundidade 0,002); não aceita um vizinho ou uma porcentagem genérica de erros. Restaram zero pixels sem classificação. Pequenas diferenças numéricas de profundidade, registradas separadamente, permanecem dentro dessa tolerância.

Na rodada final em Full HD, `r10-final-fullhd` teve cores e IDs centrais exatos em terceira e primeira pessoa, com profundidade dentro da mesma tolerância. Medianas de quatro pares alternados AB/BA na Radeon RX 570, com suavização desligada e amostragem 1×:

| Câmera, 1920×1080 | GPU projetada por frame | GPU com volumes residentes | Upload projetado por frame | Comandos de desenho |
| --- | ---: | ---: | ---: | ---: |
| Terceira pessoa | 229,183 ms | 77,821 ms | 101.756.400 → 21.794.160 bytes | 74.369 → 8.912 |
| Primeira pessoa | 214,149 ms | 68,647 ms | 93.369.360 → 18.293.520 bytes | 68.310 → 7.269 |

São tempos de **`DrawTownView` isolado**, incluindo a ponte de readback, sem simulação, interface/apresentação SDL. Não medem FPS sustentado da partida e não comprovam que a meta final de desempenho foi atingida. A leitura síncrona dos três alvos continua; em primeira pessoa, sua mediana nessa rodada passou de 9,057 para 15,607 ms, mesmo com a redução do tempo total do mundo. O ganho não elimina esse custo nem os caminhos que ainda preparam geometria na CPU.

O teste sintético final passou **1.768 verificações**, incluindo volumes agrupados, paleta/sprite frontal/base/shade, faces traseiras, recortes, profundidade, seleção e reutilização do cache. Evidências privadas: `diagnostics/gpu-static-mesh/resident-volume-final-20261008/run.log` e `diagnostics/resident-scene-20261008/{r8-dense-guard,r9-perspective-scope,r10-final-fullhd}/run.log`. A próxima ampliação exige medir o gargalo restante e demonstrar paridade dos objetos importados e da isométrica antes de habilitá-los.

A revisão foi instalada com backup no iniciador habitual, SHA-256 `7f277f06c06edc339ad39bc17c555cba61d435d359b635d9fa75a4dab44c8ff4`. Recibo privado: `diagnostics/resident-menu-installed-20261008-150101/receipt.json`. Os 40 arquivos do perfil verificados preservaram bytes e datas; a preparação alterou somente o recibo de runtime esperado. A instalação técnica não substitui o teste prolongado da partida pelo jogador nem comprova 60 FPS.

## Qualidade dos masters e evolução de desempenho

A qualidade próxima deve vir do master preservado. Reduzir permanentemente todos os objetos para o orçamento atual não satisfaz esse objetivo. O formato D3DMESH1 atual admite até 20.000 triângulos por arquivo e atlas RGB de até 2048 pixels; não transporta normais suaves, normal maps ou PBR. O caminho GPU das construções importadas ainda transmite triângulos não indexados por quadro e tem seu próprio limite; o novo caminho residente atende volumes de props e vegetação, sem ampliar o contrato desses assets. Portanto, carregar diretamente os 3,49 milhões de triângulos da geometria pré-remesh de Adria exige evolução real do contrato e do renderer; essa fonte também precisa de UV/material antes de substituir o modelo texturizado.

A execução segue estas dependências, sem declarar prontas opções ainda inexistentes:

1. **Rejeição fora da câmera:** implementada, validada e instalada nas rodadas acima, com limites reais por construção antes de preparar texturas e percorrer triângulos, preservando sombras de estruturas fora do quadro. Cor, profundidade e seleção foram comparadas com a opção desligada, em CPU/GPU e nos quatro modos.
2. **Malhas estáticas na GPU:** integração parcial validada e instalada para props e vegetação em perspectiva, com buffers persistentes de vértices/índices, normais planas e identidade por revisão. Os quadros aquecidos medidos não reenviam essa geometria. Construções, cenário individual, atores e isométrica ainda exigem integração e validação próprias; o suporte da API a atributos por vértice não torna os materiais importados mais completos. Medir CPU, upload, memória e desenho separadamente antes de ampliar o escopo.
3. **Contrato de assets e LOD:** master com materiais preservados mais derivados explícitos. Selecionar detalhe por tamanho/erro na tela, com histerese para evitar trocas contínuas. Manter âncora, proporções, aberturas e seleção entre versões; colisão continua nativa. Testar perto/longe e primeira pessoa com imagens comparáveis.
4. **Carregamento e orçamento de memória:** cache com margem de antecipação e descarregamento gradual por orçamento. Tirar algo do enquadramento não deve provocar sua destruição/reimportação imediata a cada giro da câmera. Oclusão por outros objetos é uma etapa distinta, dependente de medição.
5. **Controles reais de qualidade:** expor distância/erro de detalhe, texturas, sombras e orçamento somente quando cada recurso existir e tiver limites/fallback verificados. Comparar qualidade e tempo no hardware do jogador antes de escolher padrões.

O [guia oficial do Godot sobre LOD](https://docs.godotengine.org/en/stable/tutorials/3d/mesh_lod.html) descreve o critério em pixels; o runtime DevilutionX precisa implementar seu próprio transporte e seleção. Abrir a cena no Godot não transfere automaticamente esses recursos para o executável do jogo. Essas técnicas viabilizam mais detalhe com trabalho proporcional ao que aparece, sem prometer desempenho ou qualidade de uma produção AAA por uma única opção.
