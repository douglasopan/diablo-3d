# Renderização 3D na GPU

O piloto Windows usa **Direct3D 11 dentro do DevilutionX**. A simulação, o mapa, a colisão, os saves e a seleção dos modelos continuam no mesmo projeto. Não há migração para outra engine. O caminho original permanece autoritativo em Home e nos níveis ainda não reconstruídos.

## Usar e comparar

Durante a partida: **Esc → Options → Video Options → 3D GPU Rendering**. O menu também contém **3D Edge Smoothing** e a correção de brilho. GPU e suavização são independentes, persistem no INI do perfil e mudam no próximo desenho. Fora da partida, ambos estão em Settings → Graphics. O padrão público é desligado; a preparação de um perfil existente preserva suas escolhas.

F4 habilita a visualização de Tristram. Gire a câmera para ver a reconstrução; Home restaura a imagem original. O HUD identifica GPU ativa, CPU selecionada ou fallback. Quando o backend falha, a imagem inteira é refeita pela CPU; nunca se publica cor de um frame com seleção/profundidade de outro. Desligar e ligar a opção permite uma nova tentativa; recarregar os recursos também libera o bloqueio.

O recibo do iniciador inclui `3D GPU Rendering` entre dez solicitações gráficas. Ele registra a preferência, não prova qual adaptador desenhou o quadro.

## Fronteira implementada

1. `town_view` lê a mesma cena e prepara câmera, triângulos recortados, UVs, materiais, luzes e IDs. A projeção continua ortográfica.
2. `town_gpu` grava esses triângulos em um buffer por frame e agrupa comandos consecutivos compatíveis. Normais, intensidade direcional, parâmetros de sombra, shade e ID são dados de cada triângulo.
3. Shaders produzem cor indexada, ID e profundidade em três alvos, com depth test e recortes de opacidade. Índice zero pintado continua opaco quando uma máscara o declara assim. Iluminação importada, fogo e sombras usam as mesmas tabelas e coordenadas da referência CPU.
4. A ponte atual lê os três alvos de volta. A composição da interface e os acessores de seleção continuam usando os buffers herdados. Em 2×, a redução traz ID e profundidade da mesma subamostra mais próxima; o compositor SDL pode preservar a cor 2× até a saída.

Uma textura de paleta compartilhada elimina trocas de textura para cada cor constante. Nas faces de volumes, o shader une a base sólida e seu sprite recortado em um único passe, incluindo o shade da base. O caminho CPU conserva seus dois passes. Isso reduz comandos sem ordenar novamente faces sobrepostas.

Uploads têm identidade/revisão explícita e copiam os dados emprestados durante a submissão. Caches não usados expiram entre frames. Reset libera dispositivo, texturas e alvos. O mapa estático de sombras ainda é construído/cacheado pela CPU; a amostragem e a iluminação por pixel ocorrem no shader.

## Validação reproduzível

Compile com `build.ps1 -WithSmoke`. O Windows SDK fornece os headers e as bibliotecas Direct3D; não há pacote bgfx neste piloto.

```powershell
build\town_view_smoke.exe --gpu-fixtures diagnostics\gpu-fixtures
build\town_view_smoke.exe <dados-do-jogo> build\assets <diagnostico-com-baseline-local> --gpu
```

O primeiro comando usa dados sintéticos. O segundo exige GPU de hardware e dados locais do jogador. Para testar o modelo selecionado, o diretório diagnóstico precisa conter `d3d-models/cabin-east.d3d` e `d3d-lighting.ini` verificados, sem publicá-los. WARP só é permitido explicitamente nos fixtures sintéticos; produção não o usa como fallback silencioso.

Os testes cobrem readback com pitch, máscaras, cor zero, repetição de UV, iluminação, IDs, decal preservando seleção, recursos temporários, resize/reset, persistência dos callbacks reais do menu e retorno CPU → GPU → CPU. A comparação real registra câmeras, densidade, adaptador, comandos e o custo completo de `DrawTownView`, incluindo readback/redução. Esse tempo exclui a interface e a apresentação SDL; não é FPS sustentado da partida.

## Resultado em 8 de outubro de 2026

Windows, Intel Core i7-14700, Radeon RX 570 (hardware, sem WARP), mesma cabana/luz locais selecionadas e fogo congelado. Medianas de cinco chamadas mornas por backend na última rodada:

| Viewport/qualidade | CPU | GPU | Limite da comparação |
| --- | ---: | ---: | --- |
| 640×352, 1×, geometria forçada na câmera original | 24,8 ms | 8,1 ms | Mundo, sem UI/SDL. |
| 960×540, 2× por eixo | 191,8 ms | 40,9 ms | Inclui redução de cor e seleção. |
| 1920×1080, 1×, zoom nativo ligado | 154,0 ms | 29,6 ms | Cerca de 5,2× menos tempo no desenho do mundo. |

Os tempos variam entre rodadas na máquina compartilhada; não são uma garantia de FPS. Na comparação Full HD, o primeiro piloto enviava 38.633 comandos para 38.978 triângulos; o caminho final envia 4.313 comandos para 27.386 triângulos, com a fusão dos passes. As seis capturas GPU permaneceram exatamente iguais após essas otimizações.

Os seis casos incluem órbita de 90°, painel/pan/zoom, dimensões ímpares e mundo 2×. Zero divergências sem explicação geométrica: 108 diferenças de ID vieram de bases de chão coplanares, confirmadas por raios no plano y=0 e diferenças de profundidade ≤0,0001; uma subamostra de árvore foi explicada pela cobertura de vértices quantizados a 1/256 de pixel de raster, confirmando os dois mínimos de profundidade por raios independentes. Entidades/arquitetura não recebem uma tolerância genérica de ID. Imagem e seleção retornaram exatamente à referência CPU após desligar GPU; Home manteve o backend original exato.

A rejeição real de um viewport 2304×2048 excedendo o orçamento foi exercitada: nenhum triângulo enviado, quadro CPU completo idêntico, bloqueio repetido seguro e recuperação do hardware após OFF/ON. Passaram também fixtures sintéticos, regressão, qualidade, compositor SDL e os 25 testes do launcher em PowerShell 5.1. Evidências privadas: `diagnostics/gpu-pilot/final`, `fixtures-fused.log`, `quality`, `regression`, `presentation-layers.log` e `runtime-baseline-gpu-tests`.

O executável habitual e o alias de qualidade receberam o mesmo build. O perfil local permanece em 1920×1080, Zoom ligado e suavização desligada; sua única mudança foi `3D GPU Rendering=1`. Modelo, luz e save conservaram hash, tamanho e data. Isso não promove o modelo a aceitação artística integral. Observação do FPS e da interface na janela habitual ainda depende de uma partida; o diagnóstico não abre essa janela.

## Limites do incremento

- Backend de hardware implementado para Windows/Direct3D 11; outras plataformas continuam na CPU.
- A leitura síncrona dos três alvos e a composição lógica ainda têm custo. Apresentação direta, seleção assíncrona e novos passes de sombra são possíveis incrementos posteriores, após medir o gargalo restante.
- D3D usa cobertura top-left e comparação de profundidade exata; a CPU aceita pequenas tolerâncias. Bordas, subamostras e bases de chão coplanares precisam ser classificadas separadamente na comparação. Não se afirma identidade de todos os pixels/tiles entre rasterizadores.
- A suavização conserva o orçamento de 4.194.304 amostras; 1920×1080 recua a 1×. O backend GPU não altera essa política.
- O alvo GPU também tem limite de 4.194.304 pixels; áreas lógicas maiores usam CPU com motivo explícito. Não se promete suporte GPU a 4K neste incremento.
- Não adiciona arte HD do Belzebub, novos modelos, escala independente de menus ou renderização dos níveis procedurais. Esses trabalhos seguem seus marcos no [guia de execução](PROJECT-EXECUTION.md).
