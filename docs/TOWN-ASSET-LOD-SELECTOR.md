# Seleção de detalhe dos assets de Tristram

Este incremento atende ao requisito explícito de 8 de outubro de 2026: preservar detalhe perto da câmera e usar versões leves somente quando o erro e o tamanho projetados permitem. Ele pertence à evolução do contrato descrita em [GPU-RENDERER.md](GPU-RENDERER.md), dentro da revisão atual de [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md). Não seleciona outra revisão artística, altera o modelo instalado ou regenera a cabana congelada.

## Implementado

O módulo independente [town_asset_lod.hpp](../Source/engine/render/town_asset_lod.hpp) / [town_asset_lod.cpp](../Source/engine/render/town_asset_lod.cpp) recebe metadados e decide o detalhe desejado por instância. Não depende de globals, mapa, RNG, renderer, carregador ou arquivos do jogador. Não aloca, carrega modelos, desenha, altera colisão nem aplica um orçamento de triângulos. Seus inputs emprestados são consumidos durante a chamada.

```cpp
TownAssetLodSelection SelectTownAssetLod(
    std::span<const TownAssetLodLevel> levels,
    const TownAssetLodIdentity &identity,
    const TownAssetLodProjection &projection,
    const TownAssetLodPolicy &policy,
    TownAssetLodState &state);
```

Os índices retornados apontam para o span recebido. `TownAssetNoLod` significa ausência de candidato. O chamador mantém um `TownAssetLodState` por instância e invalida sua identidade junto com a cena. Um estado não deve ser compartilhado entre dois objetos.

### Metadados por nível

| Campo | Contrato |
| --- | --- |
| `stableKey` | Identificador não nulo e único entre os níveis drawable da revisão. |
| `geometricError` | Limite conservador, finito e não negativo da distância à referência drawable, em unidades do objeto. Não é porcentagem de triângulos. |
| `maxProjectedDiameterPixels` | Maior diâmetro recomendado para usar este nível. A referência finest usa `+infinity`; cada nível coarse usa um valor finito positivo. |
| `drawable` | O adaptador do formato confirmou geometria, UV, materiais, normais, equivalência de aberturas e demais requisitos necessários ao backend. Uma fonte geométrica sem material utilizável usa `false`. |
| `errorVerified` | A medida de erro é conhecida e validada. `false` permite inspeção, nunca seleção automática, inclusive como fallback. |
| `available` | Os bytes/requisitos desta revisão estão disponíveis. |
| `resident` | Os recursos necessários já estão prontos para desenho. `resident=true` não supera `available=false`. |

Os níveis vêm do mais fino para o mais leve. Masters preservados que só servem de fonte podem aparecer em qualquer posição com `drawable=false`; nunca são candidatos. O primeiro nível drawable é a referência, com erro zero verificado e limite de diâmetro infinito. A ausência de uma referência válida retorna `NoDrawableReference`, sem propor desenho de uma malha aproximada.

Entre níveis drawable, o erro deve ser não decrescente e o limite de diâmetro não crescente; chaves repetidas, valores inválidos ou ordem incompatível produzem `InvalidLevels`. Nesse caso, a referência válida continua exigida e os fallbacks coarse ficam desabilitados. Os campos de níveis drawable ainda não verificados também precisam respeitar a ordem estrutural; o seletor não inventa uma medida ausente.

O erro zero da referência é relativo a ela mesma. Isso não afirma que a referência recuperou a fidelidade do master geométrico, nem aprova sua silhueta, textura, normais ou aberturas. No caso de Adria, a fonte densa sem UV/material permanece uma fonte até que a recuperação e a revisão demonstrem um drawable utilizável.

### Projeção e política

`TownAssetLodProjection` contém `pixelsPerWorldUnit`, `boundingRadius` em unidades do objeto, `worldUnitsPerObjectUnit`, `valid` e `intersectsNearPlane`. A esfera deve envolver todas as versões em torno da mesma âncora. O adaptador fornece o maior fator de projeção sobre esses limites. Na perspectiva, a escala medida só no centro pode subestimar o erro na parte mais próxima: o cálculo conservador é responsabilidade do adaptador da câmera. Transformações não uniformes exigem um limite conservador de escala no adaptador; o seletor não deforma modelos.

Para `s = pixelsPerWorldUnit * worldUnitsPerObjectUnit`, calculam-se:

```text
diameterPixels = 2 * boundingRadius * s
errorPixels[i] = geometricError[i] * s
```

Valores inválidos, nulos, negativos, overflow/underflow da projeção ou `valid=false` exigem o finest. Limites que tocam/cruzam o plano próximo também exigem finest, independentemente do tamanho informado. O chamador precisa preencher `intersectsNearPlane` usando os mesmos limites e plano do renderer; omitir esse teste não produz uma proteção automática no seletor.

`TownAssetLodPolicy` traz tolerância positiva finita em pixels e duas margens independentes entre zero, inclusive, e um, exclusive. Os defaults do módulo são tolerância de 1 pixel e margens de 0,15. São valores para o incremento isolado, sem configuração exposta ou padrão do jogo instalado.

## Algoritmo e histerese

Na primeira chamada válida, escolhe-se o nível mais leve verificado para o qual **ambos** os critérios nominais passam:

```text
errorPixels <= pixelTolerance
diameterPixels <= maxProjectedDiameterPixels
```

Depois, a decisão considera o nível **desejado** anterior:

1. Um nível coarse atual permanece enquanto ambos os limites relaxados passam, multiplicados por `1 + refineMargin`. Se qualquer limite for ultrapassado, escolhe-se o nível mais leve entre os anteriores que restaure os critérios nominais.
2. Para entrar num nível mais leve, ambos os limites precisam passar pela margem estrita `1 - coarsenMargin`. Entre os candidatos elegíveis, escolhe-se o mais leve.
3. Uma mudança grande de câmera pode atravessar vários níveis numa chamada. Dentro das bandas, a história da instância conserva o detalhe e evita alternância por pequenas oscilações.
4. `assetKey`, `revision` ou `instanceKey` diferentes descartam a história e aplicam a seleção nominal. Um índice/chave de nível guardado que deixou de ser válido também reinicializa o estado. O chamador altera `revision` sempre que descritores ou limites mudarem.

As margens atuam no erro **e** no diâmetro. Durante retenção por histerese, um nível pode exceder o limite nominal, ainda dentro da banda autorizada. `hysteresisApplied` revela quando o resultado difere da seleção nominal; `drawableMeetsQuality` continua indicando o critério nominal, sem esconder esse excesso.

## Disponibilidade, residência e fallback

`desiredLevel` é a solicitação de qualidade, preservada no estado. `drawableLevel` é uma **proposta residente**, separada dessa solicitação:

| `fallback` | Proposta |
| --- | --- |
| `None` | O próprio nível desejado está disponível e residente. |
| `FinerResident` | O nível mais próximo entre os anteriores residentes. Consultar `drawableMeetsQuality`, pois a banda atual pode exceder o critério nominal. |
| `CoarserResidentAdequate` | Não existe anterior residente; um nível posterior residente passa pelos critérios nominais. Pode acontecer quando a histerese conserva um desejado mais fino. |
| `CoarserResidentInadequate` | Só há um nível posterior que não passa pelos critérios. O chamador precisa decidir explicitamente entre aceitar a perda e adiar o desenho/carregar a referência. |
| `NoResidentLevel` | Nenhuma proposta segura de metadados está residente; `drawableLevel=TownAssetNoLod`. |

Fallbacks não alteram o nível desejado nem a história da histerese. Assim que o nível desejado se torna residente, ele volta imediatamente como proposta. Não existe parâmetro de orçamento de triângulos capaz de forçar detalhe inferior perto da câmera. Um master source-only ou um nível sem erro verificado nunca resolve falta de residência.

O retorno informa também motivo (`reason`), reinicialização do estado, diâmetro projetado e erros projetados. A referência zero-error é conservadora mesmo com projeção inválida; um coarse proposto nesse caso é sempre inadequado. Metadados estruturais inválidos impedem qualquer fallback coarse.

## Validação isolada

[town_asset_lod_smoke.cpp](../tools/town_asset_lod_smoke.cpp) passou **6.913 verificações, zero falhas por configuração**, compilado com MSVC x64, C++20, `/W4 /WX`, tanto sem otimização (`/Od /Z7`) quanto com `/O2`, em 8 de outubro de 2026. Cobre seleção analítica nos dois sentidos, limites inclusivos, histerese assimétrica nos dois critérios, oscilações repetidas, saltos perto/longe, escala, identidades, referência source-only, erro não verificado, residência, fallback inadequado, plano próximo, valores inválidos e overflow. Os testes não precisam de MPQ, perfil, GPU ou janela.

Para reproduzir num Developer PowerShell com MSVC, a partir da raiz do repositório:

```powershell
$taskRepo = (Get-Location).Path
$taskDiagnostic = Join-Path (Split-Path $taskRepo) 'diagnostics/asset-lod-20261008/lod-core'
New-Item -ItemType Directory -Path $taskDiagnostic -Force | Out-Null
Push-Location -LiteralPath $taskDiagnostic
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 "/I$taskRepo/Source" `
        "$taskRepo/Source/engine/render/town_asset_lod.cpp" `
        "$taskRepo/tools/town_asset_lod_smoke.cpp" /Fe:town_asset_lod_smoke.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compile failed' }
    & './town_asset_lod_smoke.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Smoke failed' }
} finally {
    Pop-Location
}
```

Logs e binários desta validação ficam privados em `diagnostics/asset-lod-20261008/lod-core`, fora do repositório. Nenhum CMake, cache, executável habitual ou arquivo do perfil foi modificado por esta entrega.

## Integração pendente

O seletor está executável isoladamente. Ainda não seleciona LODs na partida. O responsável pela integração precisa adaptar o contrato v2 para estes descritores, filtrar `openingEquivalence` e a compatibilidade antes de marcar o nível como drawable, calcular limites/projeção conservadores nos modos de câmera, manter os estados por instância, coordenar carregamento/cache, definir a política explícita de fallback inadequado e ligar a seleção ao caminho de desenho com o mesmo ID/âncora/aberturas por revisão.

Permanecem fora deste módulo: reconstrução/transferência de UV/material, prova artística de Adria, medição conservadora do erro geométrico, preservação das aberturas entre derivados, upload estático indexado, normais e materiais no backend, revisão Godot, configuração de qualidade, orçamento de memória e comparação de imagens perto/longe na partida. Aberturas, materiais, normais e compatibilidade exigem validação do formato/asset além deste critério geométrico. O guia operacional continua sendo a fonte da fila; este documento registra somente o contrato e a evidência do seletor.
