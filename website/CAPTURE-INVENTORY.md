# Inventário de capturas do devlog

Auditoria local realizada em 07/10/2026, acrescida das cinco comparações contextuais finais de `63e5e749d73c213aecf4b77b400aa04acc5a0598` e das quatro comparações de fogo e janelas de `cdeaaab0d208bf0da4239b98c287619508e308a5`. Em 08/10 foram selecionadas mais três comparações: quatro câmeras CPU/GPU e duas revisões de Adria no Godot. As contagens do inventário local completo continuam descrevendo a auditoria inicial; estas extensões não são uma nova varredura de todos os diagnósticos posteriores.

| Item | Quantidade |
| --- | ---: |
| Imagens locais de diagnóstico inventariadas | 12,830 |
| Capturas de clipboard inventariadas | 84 |
| Imagens no inventário local completo | 12,914 |
| Hashes SHA-256 distintos do conjunto completo | 1,620 |
| Cópias exatas redundantes no conjunto completo | 11,294 |
| Imagens classificadas como fontes, texturas ou material isolado e excluídas | 8,641 |
| Candidatas contextuais de diagnóstico antes da seleção visual | 3,961 |
| Hashes distintos dessas candidatas contextuais | 1,093 |
| Imagens selecionadas e visualmente revisadas para o site | 48 |
| Prints do autor incluídos nessa seleção | 4 |

O inventário completo, as folhas de contato e o mapeamento privado das capturas do autor ficam somente na área local de auditoria, fora do repositório. Este documento não expõe nomes de arquivos de outros projetos, dados de conta, conversas privadas ou caminhos pessoais.

## Critérios de publicação

- Somente screenshots contextuais do jogo e comparações de cenas de desenvolvimento foram selecionados. As comparações de Adria mostram a ferramenta de autoria e suas limitações, sem representar gameplay ou aceitação artística.
- Fontes extraídas, sprites isolados, atlas, texturas, arquivos de albedo, ensaios sintéticos e imagens isoladas de candidatos de modelo foram excluídos. MPQ, saves e modelos não fazem parte destes assets.
- Os demais prints de clipboard tratam de outros projetos ou de telas privadas e não são publicados.
- Todas as imagens selecionadas foram conferidas visualmente; não há dados pessoais, credenciais ou chaves visíveis nelas.
- O WebP é lossless, mantém a resolução original e foi decodificado para verificar igualdade dos pixels. A conversão não amplia, retoca nem gera capturas por IA. O recorte de auditoria da lâmpada já recebido inclui ampliação 6× nearest, identificada no próprio quadro; seu tamanho e seus pixels foram preservados.
- As referências de UI geradas por IA pertencem à etapa de design do site e não são evidência do jogo.
- A igualdade de imagens na rota de comparação nativa valida a seleção do backend original; não prova que a malha 3D é perfeita. As legendas distinguem referência original e geometria forçada.
- As primeiras 28 imagens permanecem como retrospectiva das etapas anteriores. Cinco comparações finais registram a versão publicada do interior com luz amarela real, piso físico de madeira e revisão do terreno externo. O detalhe das tábuas internas não fica legível pela janela pequena no enquadramento externo; o recorte de terreno mostra grama e sombra, com o interior fora do quadro.

O arquivo `evidence.json` registra, para cada imagem pública, título, texto alternativo, legenda, etapa, categoria, fonte contextual, hash SHA-256 da imagem original e fontes de cópias exatas encontradas. Os prints do autor usam uma origem pública genérica; seu mapeamento exato permanece local.

As três capturas de 08/10 preservam a resolução e os pixels originais após conversão lossless. A comparação de câmeras antecede as novas otimizações e não mede fluidez. As duas comparações de Adria distinguem master sem material, resultado reduzido e transferência experimental, ainda sem aprovação ou instalação. Nenhum GLB, textura isolada, perfil, caminho pessoal ou credencial acompanha as imagens.

## Seleção pública

| Asset | Etapa | Categoria |
| --- | --- | --- |
| `tavern-cleanup-before.webp` | 09/10 · antes · CPU offscreen · feb9ca77e | Geometria |
| `tavern-cleanup-after.webp` | 09/10 · depois · CPU offscreen · feb9ca77e | Geometria |
| `project-credits-main-pt.webp` | 08/10 · UI nativa offscreen · fc9642982 | Ferramentas |
| `project-credits-submenu-pt.webp` | 08/10 · UI nativa offscreen · fc9642982 | Ferramentas |
| `project-credits-author-pt.webp` | 08/10 · UI nativa offscreen · fc9642982 | Ferramentas |
| `project-credits-author-en.webp` | 08/10 · UI nativa offscreen · fc9642982 | Ferramentas |
| `tristram-camera-modes.webp` | 08/10 · comparação técnica de câmeras | Protótipo |
| `adria-master-textured-comparison.webp` | 08/10 · revisão de autoria | Geometria |
| `adria-material-transfer-review.webp` | 08/10 · transferência experimental não instalada | Geometria |
| `v1-center.webp` | V1 | Protótipo |
| `v1-rotated.webp` | V1 | Protótipo |
| `v2-center.webp` | V2 | Geometria |
| `v2-low.webp` | V2 | Geometria |
| `v3-forced.webp` | V3 | Ferramentas |
| `v3-low.webp` | V3 · malha forçada | Geometria |
| `v4-town.webp` | V4 · refinamento local posterior | Geometria |
| `v4-rotated.webp` | V4 · refinamento local posterior | Geometria |
| `v4-actors.webp` | V4 · refinamento local posterior | Personagens |
| `meshy-review.webp` | V4 · iluminação intermediária do modelo importado | Luz |
| `lighting-comparison.webp` | V4 · luz externa publicada | Luz |
| `lighting-orbits.webp` | V4 · luz externa publicada | Luz |
| `lighting-angles.webp` | V4 · luz externa publicada | Luz |
| `author-cabin-error.webp` | Revisão enviada pelo autor | Protótipo |
| `author-cabin-prototype.webp` | Revisão enviada pelo autor | Protótipo |
| `author-cabin-target.webp` | Jogo original · referência enviada pelo autor | Ferramentas |
| `v1-cathedral.webp` | V1 | Protótipo |
| `v2-cathedral.webp` | V2 | Geometria |
| `v4-smithy-comparison.webp` | V4 · auditoria de arquitetura | Ferramentas |
| `v4-adria-comparison.webp` | V4 · auditoria de arquitetura | Ferramentas |
| `v4-farnham-comparison.webp` | V4 · auditoria de arquitetura | Ferramentas |
| `v4-crypt.webp` | V4 · refinamento local posterior | Geometria |
| `v4-well-orbits.webp` | V4 · refinamento local posterior · poço | Ferramentas |
| `v4-tree-orbits.webp` | V4 · refinamento local posterior · vegetação | Geometria |
| `v4-player-orbits.webp` | V4 · refinamento local posterior · atores | Personagens |
| `v4-cabin-west-orbits.webp` | V4 · refinamento local posterior · cabana oeste | Geometria |
| `v4-cabin-mask-comparison.webp` | V4 · revisão de máscaras | Geometria |
| `author-menu-startup.webp` | Inicialização enviada pelo autor | Ferramentas |
| `cabin-interior-final.webp` | V4 · interior publicado | Luz |
| `cabin-floor-final.webp` | V4 · interior publicado | Luz |
| `cabin-lamp-final.webp` | V4 · interior publicado | Luz |
| `cabin-orbits-final.webp` | V4 · interior publicado | Luz |
| `cabin-angles-final.webp` | V4 · interior publicado | Luz |
| `cabin-fire-tone.webp` | V4 · fogo e duas janelas publicados | Luz |
| `cabin-rear-fire.webp` | V4 · fogo e duas janelas publicados | Luz |
| `cabin-fire-orbits.webp` | V4 · fogo e duas janelas publicados | Luz |
| `cabin-fire-angles.webp` | V4 · fogo e duas janelas publicados | Luz |

## Taverna — 9 de outubro

Duas prévias sanitizadas do replay próprio do integrador foram autorizadas individualmente e conferidas visualmente. Mostram antes/depois da mesma câmera, em órbita zero, com a correção do [commit feb9ca77e](https://github.com/douglasopan/diablo-3d/commit/feb9ca77e73e66c2cc209af51ca9bb9b3504e6c1). São renders CPU offscreen, não capturas da janela do jogo, gameplay, testes de GPU ou aprovação integral da arte. Os dois PNGs foram convertidos para WebP lossless, em 960×720, com igualdade integral dos pixels RGBA, sem corte, retoque, ampliação ou nova geração. Hashes originais e publicados, variantes e classificação ficam em `evidence.json`, com legendas completas em português e inglês. Nenhum perfil, modelo, extração ou recibo privado acompanha as imagens. Os 44 registros anteriores são preservados e a seleção passa a 46; as contagens da auditoria local inicial não representam uma nova varredura.

## Suporte e créditos — 8 de outubro

Quatro imagens da entrega [fc9642982](https://github.com/douglasopan/diablo-3d/commit/fc9642982da2b74b37af366f74c75e966aad4578) mostram o menu principal e o submenu em português e os créditos de Douglas Pan em português e inglês. São renders da interface nativa produzidos por diagnóstico offscreen, sem iniciar uma partida; não são gameplay nem screenshots da janela do jogo. A revisão visual confirmou ausência de dados privados e preservou o enquadramento inteiro. A conversão WebP lossless conserva 1920×1080 e igualdade integral dos pixels RGBA, sem recorte, redimensionamento ou retoque. Os 40 registros anteriores permanecem; a seleção passa a 44. Recibos privados e caminhos locais não entram no site. Os testes e limites estão no [guia do editor dessa revisão](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/GODOT-EDITOR.md).

## Comparações da versão publicada

Correção de procedência em 08/10/2026: `cabin-interior-final.webp` e `cabin-angles-final.webp` são pranchas históricas de renders técnicos offscreen, com recortes e composição, associadas ao release `63e5e749`. As fontes PNG registradas em `evidence.json` e os WebPs têm pixels RGBA idênticos. O pipeline usa turntables e `qa_cabin_lighting.py`; essas imagens não são screenshots da janela do jogo, não provam o estado atual da partida e não representam aprovação integral do modelo. A correção altera a classificação e as legendas PT/EN, preservando arquivos, hashes, dimensões e vínculo histórico.

As cinco comparações finais pertencem ao [commit `63e5e749`](https://github.com/douglasopan/diablo-3d/commit/63e5e749d73c213aecf4b77b400aa04acc5a0598). Foram revisadas individualmente e mantidas em WebP lossless na resolução recebida.

- `cabin-interior-final.webp`: referência original, malha antes e cabana com janela amarela depois.
- `cabin-floor-final.webp`: terreno externo, removendo a sombra antiga pintada; o interior está fora do recorte.
- `cabin-lamp-final.webp`: mesmo quadro e recorte com a lâmpada desligada/ligada, em ampliação de auditoria 6× nearest já presente na origem.
- `cabin-orbits-final.webp`: antes/depois em −5°, 0° e +5°.
- `cabin-angles-final.webp`: antes/depois em 0°, 90°, 180° e 270°.

A janela confirma visualmente a luz interna; o detalhe do piso de madeira não é legível nos pequenos pixels da janela. As comparações de giro permitem revisar a integração volumétrica com a cena. Não são publicados albedo isolado, atlas ou relatórios brutos com caminhos locais.

## Fogo e duas janelas publicados

Quatro comparações reais do [commit `cdeaaab0d`](https://github.com/douglasopan/diablo-3d/commit/cdeaaab0d208bf0da4239b98c287619508e308a5) ampliam a seleção para 37 imagens distintas. Todas foram inspecionadas visualmente e verificadas após a conversão lossless.

- `cabin-fire-tone.webp`: referência nativa, interior anterior e tom de fogo das velas 3D na janela frontal.
- `cabin-rear-fire.webp`: fogo desligado/ligado na abertura traseira da mesma vista de 180°.
- `cabin-fire-orbits.webp`: antes/depois em −5°, 0° e +5°.
- `cabin-fire-angles.webp`: frente e traseira nas vistas de 0°, 90°, 180° e 270°.

A abertura traseira é uma inferência artística aprovada, pois não há referência original disponível dessa face. Duas aberturas dão para o mesmo cômodo com piso físico de madeira e duas velas físicas; a pequena janela do enquadramento externo não permite conferir o detalhe das tábuas, da cera ou dos pavios. Os recortes de auditoria já recebidos preservam suas ampliações nearest e cores originais. Não se publicam relatórios brutos, albedo isolado, atlas ou modelos.

## Piloto técnico da Catedral — 9 de outubro de 2026

Duas capturas do gate RX 570 R4 associado ao commit `c5e8cb7fdcf79ceb6ed368b77bfa2ae55446a91e` foram revisadas e adicionadas: piloto GPU após recuperação e referência nativa de fallback. São PNGs originais byte-exatos, sem conversão, recorte ou retoque, ambos em 640×480. Não constituem gameplay ou comparação artística na mesma câmera; o cenário está parado e parcialmente inicializado, anterior à instalação. Sete saídas formam três grupos por hash; somente duas fontes distintas foram selecionadas. As 46 entradas anteriores permanecem intactas, totalizando **48 capturas**. Procedência e limites em [DEVLOG-MEDIA.md](DEVLOG-MEDIA.md#cathedral-technical-pilot--9-october-2026).
