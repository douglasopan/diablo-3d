# Revisão de master, materiais e LOD no Godot

A cena [`lod_review.tscn`](../editor/godot/review/lod_review.tscn) compara revisões GLB por identidade exata, sem alterar o inspector existente, o catálogo ou a seleção da partida. É um incremento independente de revisão do [contrato de modelos v2](TOWN-MODEL-V2.md). A aprovação artística permanece explicitamente **pendente** em tela e nas notas exportadas.

## Abrir e comparar

1. Abra `editor/godot/project.godot` no Godot 4.7 ou compatível.
2. Abra `editor/godot/review/lod_review.tscn` e execute com **F6**.
3. Use **Abrir manifesto…** para escolher seu JSON privado v2. Cada GLB precisa existir e corresponder ao SHA-256 declarado. Caminhos relativos são resolvidos junto ao manifesto; caminhos absolutos locais também são aceitos.
4. O lado esquerdo começa no primeiro master e o direito no `drawable-lod` de índice 0. Os seletores permitem inspecionar outras fontes explicitamente, inclusive outro master com material transferido ou um candidato coarse ainda não verificado.
5. Arraste para orbitar, use o botão direito para deslocar e a roda para aproximar. **Reenquadrar** restaura a câmera comum. Compare frente, costas, laterais, teto, base e detalhes próximos.
6. Escreva sua observação e use **Salvar nota…** para criar um JSON separado. A nota conserva revisão, hashes, âncora, aberturas, transformações e câmera, sem caminhos de fonte. Não muda o manifesto nem concede aceitação ao asset.

Também é possível abrir diretamente uma revisão local:

```powershell
& 'CAMINHO_DO_GODOT.exe' --path editor/godot res://review/lod_review.tscn -- --manifest=CAMINHO_DO_MANIFESTO.json
```

Os modelos e as imagens de revisão ficam fora do Git. A ferramenta não baixa dados, não faz geração paga e não escreve cache de importação ou cópias dos GLBs. Não selecione uma fonte pela data de modificação. Mantenha as regras de [aprovação e baseline](ASSET-APPROVAL.md).

## Identidade, materiais e alinhamento

O manifesto usa `schemaVersion: 2`, `assetId`, `variant`, `revision`, `geometricReferenceSha256`, `anchor`, `openings` e `sources`. Consulte o [contrato v2](TOWN-MODEL-V2.md) para o esquema compartilhado com exportação, validação e runtime. O viewer exige pelo menos um `master-geometry`, que é **apenas para inspeção**, e um `drawable-lod` 0. Masters originais e experiências de transferência podem coexistir; nenhum deles pode declarar `runtimePath`/`runtimeSha256`. `geometricReferenceSha256` deve corresponder ao hash do drawable 0. Seu erro zero identifica a referência de comparação; não comprova que ela equivale à geometria do master.

`anchor.position` é uma posição finita em unidades locais do objeto, com `upAxis: "Y"`. O objeto opcional `fit` contém somente uma `uniformScale` positiva, `yawRadians` e `translation` comuns às fontes. A transformação da apresentação é `translation + yaw * uniformScale * (posição - anchor)`. Os GLBs continuam intactos; não existe ajuste independente em três eixos nem normalização de cada lado aos seus próprios limites. O enquadramento utiliza os limites do primeiro master e fica estável ao trocar a fonte da comparação.

Antes da importação, o loader confere o hash dos bytes e recusa dependências externas em `images` ou `buffers`. O `GLTFDocument` recebe os mesmos bytes conferidos, com texturas incorporadas. Materiais importados, UVs e normais por vértice são conservados; os fixtures verificam albedo, normal map e metallic/roughness. Um master sem `TEXCOORD_0` aparece cinza, com aviso explícito. A ferramenta não recupera UVs ou texturas ausentes. Um master com material transferido pode ser inspecionado com seu material, mantendo o papel de inspeção.

As duas vistas usam a mesma câmera em perspectiva, luzes e ambiente. Câmeras/luzes embutidas no GLB são removidas da instância de apresentação, sem editar o arquivo, para não introduzir iluminação diferente entre fontes. Animações ficam paradas; esta revisão estática não valida rigs ou clipes. Extensões e recursos do glTF continuam sujeitos ao suporte do importador e do renderer do Godot; isto não prova equivalência visual com a iluminação da partida.

## Erro em pixels e aberturas

O HUD apresenta `geometricError` em unidades locais e sua projeção conservadora em pixels, usando a altura real de cada viewport, FOV vertical, escala comum e profundidade até a esfera que envolve o master. Aproximar, deslocar ou redimensionar a vista atualiza esse valor. Quando a esfera cruza o plano próximo, o HUD identifica a projeção não limitada e a necessidade de usar o drawable mais fino, sem apresentar um número enorme como medida confiável.

`errorVerified` é uma declaração do manifesto. O viewer **não mede** distância entre superfícies nem transforma uma estimativa em erro comprovado. Coarse exige `openingEquivalenceVerified` booleano explícito e aparece com aberturas não verificadas se ele for falso. Também exige `maxDiameterPixels` positivo; zero/ausente no finest significa ilimitado. Índices drawable são contíguos desde 0, erros não decrescem e limites coarse de diâmetro não crescem. Os seletores desta cena são manuais: a seleção automática com histerese pertence ao módulo de LOD do runtime, não a esta prévia.

`openings.pipelineId`, sua compatibilidade (`native-unchanged`, `external-code-required` ou `baked-reviewed`) e seu hash opcional identificam a dependência. A cena mostra esses campos, mas não executa recortes, interiores, velas ou geometria adicional do jogo. `runtimePath`/`runtimeSha256` opcionais permanecem metadados; este viewer não importa nem valida os bytes D3DMESH2. Revise esses componentes e a colisão nativa separadamente.

## Verificação isolada e capturas privadas

```powershell
& 'CAMINHO_DO_GODOT.exe' --headless --path editor/godot --script res://addons/asset_lod_review/lod_review_smoke.gd
```

O harness cria GLBs sintéticos incorporados em uma pasta temporária exclusiva e remove seus fixtures ao terminar. Verifica manifesto inválido, hash divergente, caminhos, papéis, referência geométrica, transformação uniforme, atributos e mapas importados, câmeras sincronizadas, erro/aberturas não verificados, notas pendentes e preservação dos hashes das fontes. Não abre uma partida nem modifica perfis.

Para capturas, use um Godot com renderização gráfica e uma pasta de saída privada existente:

```powershell
& 'CAMINHO_DO_GODOT.exe' --path editor/godot --script res://addons/asset_lod_review/lod_review_smoke.gd -- --render-only --manifest=CAMINHO_DO_MANIFESTO.json --output=PASTA_PRIVADA/comparacao.png
```

São sete vistas: quatro órbitas, um close oblíquo e dois closes frontais. `--left-source-index=N` e `--right-source-index=N` escolhem entradas do array `sources`. O primeiro PNG usa o nome indicado, e os demais recebem `-1` a `-6`. O arquivo `comparacao-evidence.json` conserva hashes de imagens, fontes e manifesto, câmera e escopo de cada vista; a aprovação continua pendente. O harness também reconfere os hashes dos GLBs e do manifesto depois de renderizar. Esses renders técnicos não medem FPS sustentado.

Em 8 de outubro de 2026, **84 verificações headless** passaram no Godot 4.7.2. Foram renderizadas cópias privadas de Adria com o master original sem UV, a versão texturizada da API e duas experiências de transferência de material, com e sem normal map. As evidências ficam em `diagnostics/asset-lod-20261008/godot-review/`, fora do repositório. As vistas próximas permitem observar o ganho de silhueta dos frascos e também regiões lisas/cinzas e atribuições de material que precisam de revisão. Algumas dessas regiões também estão no donor; não se deve atribuí-las automaticamente à transferência. Conservar seus mapas não equivale a refazer corretamente o bake de normais para outra superfície. Comparabilidade artística e aprovação dessas experiências continuam sem comprovação.

O manifesto conjunto privado `diagnostics/asset-lod-20261008/v2-package/adria-review.json` também passou no viewer, selecionando o master transferido no índice 1 e a referência drawable no índice 2. Esse fluxo final produziu sete PNGs `adria-v2-handoff*.png`, `adria-v2-handoff-evidence.json` e `adria-v2-handoff-render.log` na pasta privada de revisão, com **22 verificações e zero falhas** na Radeon RX 570. Original e experiência permaneceram inspect-only; os hashes finais dos três GLBs e do manifesto foram reconferidos. O [handoff v2](TOWN-ASSET-LOD-HANDOFF.md) liga essa prova à validação independente do pacote runtime.

## Limite desta entrega

Estão implementados o leitor de manifesto, a importação somente de leitura por hash, a comparação GLB sincronizada, o HUD, as notas locais e o harness. Não estão integrados a seleção do jogo, os buffers GPU, a medição de erro geométrico, a edição de UVs, a promoção de candidatos ou os recortes de código. A próxima revisão usa as evidências de cada objeto, começando pela prova privada de Adria, antes de declarar um LOD ou material apto à partida.
