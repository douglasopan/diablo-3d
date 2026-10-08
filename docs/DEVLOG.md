# Devlog do Diablo 3D

Site: **[Português](https://douglasopan.github.io/diablo-3d/)** · **[English](https://douglasopan.github.io/diablo-3d/en/)** · [RSS PT-BR](https://douglasopan.github.io/diablo-3d/rss.xml) · [RSS EN](https://douglasopan.github.io/diablo-3d/en/rss.xml)

O objetivo do projeto é reconstruir Diablo 1 inteiro em 3D, incluindo todos os níveis procedurais, personagens, monstros, objetos e efeitos. Tristram é a etapa atual de validação; os demais mapas continuam usando a renderização original. Consulte o [roadmap](ROADMAP.md) para distinguir o estado implementado dos próximos marcos.

Os registros são arquivos Markdown versionados em `docs/devlog/`. O gerador em `website/build.py` cria HTML por página, metadados, dados estruturados, sitemap e RSS. Uma publicação em `main` que altere posts ou arquivos do site dispara o workflow dedicado de GitHub Pages. O site funciona sem buscar o conteúdo no navegador. A tag padrão do Google Analytics 4 está no cabeçalho compartilhado de todas as páginas PT-BR/EN e começa a medir no carregamento. Configuração, limites e verificação estão no [README do site](../website/README.md).

A publicação é completa em português brasileiro e inglês: a versão PT-BR fica na raiz do projeto e a inglesa em `/en/`. O seletor PT/EN abre a página equivalente e conserva a seção de um artigo. A escolha fica salva no navegador quando o armazenamento local está disponível. Sem uma escolha salva, a página portuguesa direciona para inglês quando esse é o idioma principal do navegador. Links diretos de `/en/` funcionam de forma independente; os dois idiomas têm HTML pronto e continuam legíveis sem JavaScript.

## Música do site

A música de fundo do site usa a faixa fornecida pelo usuário, com controles simples e preferência local de pausa/volume. A origem, a preparação, o player e os limites de início automático e navegação estão em [Música do site](../website/MUSIC.md). Essa integração é independente do seletor de trilhas do jogo.

**Correção de créditos, 8 de outubro de 2026:** biblioteca PT/EN, player de fundo, cards musicais, metadados de página e JSON-LD passam a distinguir: **Composição original: Matt Uelmen, para Diablo (Blizzard Entertainment). Regravação/reinterpretação produzida por Douglas Pan com auxílio de IA.** A fonte é o manual oficial de Diablo, página impressa 78. O artigo `tristram-de-perto` também recebe o esclarecimento nos dois idiomas. Main Menu conserva o rótulo local; cinco arquivos, incluindo Tristram 3 como exportação alternativa, não equivalem a cinco composições. Os manifestos públicos recebem créditos editoriais adicionais, preservando hashes, tags e todos os bytes de áudio. A correção do site não comprova atualização do YouTube nem instalação do menu do jogo, que têm responsáveis próprios.

## Publicar um registro

Crie `docs/devlog/YYYY-MM-DD-slug.md`. Use UTF-8 e metadados YAML:

```yaml
---
title: "Um título concreto para a mudança"
date: 2026-10-07
description: "Uma descrição curta do problema, da mudança e de seus limites."
slug: um-titulo-concreto
image: /assets/captures/v4-town.webp
image_alt: "Vista contextual de Tristram na rodada v4 refinada."
category: Geometria
order: 9
status: draft
---
```

Após os metadados, escreva o corpo em Markdown, começando os subtítulos com `##`. Conte o problema, a mudança, a evidência e os limites. Inclua links para os commits que comprovam o estado descrito. Remova `status: draft` ou altere para `published` quando o artigo estiver pronto; rascunhos não entram no site, RSS ou sitemap.

Campos obrigatórios para artigos publicados: `title`, `date`, `description`, `slug`, `image`, `image_alt`, `category`. `order` ordena registros no mesmo dia; `updated` pode indicar uma revisão editorial real. As categorias são `Protótipo`, `Geometria`, `Personagens`, `Comunidade`, `Ferramentas` e `Luz`. O slug deve ser único, com letras minúsculas, números e hífens. O build rejeita metadados incompletos, imagens ausentes e links inseguros.

Use a data real de publicação do registro. Uma retrospectiva deve informar qual etapa representa e distinguir datas de commits, timestamps locais de diagnóstico e data editorial. O RSS publica a data em `dc:date`, sem inventar um horário que não existe nos metadados.

## Tradução completa para inglês

Para cada registro publicado em `docs/devlog/`, crie a tradução em `website/content/en/devlog/` com **o mesmo nome de arquivo**. Traduza `title`, `description`, `image_alt` e todo o corpo Markdown. Preserve `slug`, `date`, `updated` quando presente, `image`, `category`, `order` e `status`, incluindo o estado dos campos opcionais. As categorias continuam com as chaves canônicas em português; o publicador traduz seus rótulos na interface.

Conserve os níveis, a quantidade e a ordem dos subtítulos. O publicador associa os IDs dos títulos em inglês às âncoras originais, mantendo os links de seção ao trocar de idioma. Os links internos do site recebem a rota do idioma atual. Links de evidência no GitHub permanecem associados aos mesmos commits e documentos, mesmo quando o documento de origem está em português.

A tradução deve manter números, custos observados, imagens, referências, contexto histórico e limites de cada etapa. Não reduza um artigo a um resumo nem apresente trabalho futuro como implementado. O build exige uma tradução inglesa completa para cada registro publicado e rejeita divergências nos metadados canônicos ou na quantidade dos títulos. As páginas, os metadados e o RSS são gerados para ambos os idiomas.

## Imagens e galeria

Copie somente capturas contextuais revisadas para `website/public/assets/captures/`. Prefira WebP lossless na resolução original, com legenda e texto alternativo. Antes de publicar, confira visualmente dados pessoais, telas de contas e chaves.

Cadastre a imagem em `website/evidence.json` para incluí-la na galeria. Registre `file`, `title`, `alt`, `caption`, `category`, `stage`, `source`, `sha256` do original e `duplicate_sources`. O inventário público reúne apenas as selecionadas; o inventário local completo e mapeamentos privados permanecem fora do repositório.

Acrescente também a entrada correspondente em `EVIDENCE_EN`, dentro de `website/translations.py`, usando o mesmo caminho `file` como chave e traduzindo `title`, `alt` e `caption`. Esse overlay cobre todas as capturas publicadas sem alterar o arquivo, a origem, o hash original ou as referências de duplicação. Categoria e etapa recebem rótulos ingleses na publicação, preservando a proveniência da evidência.

Não publique MPQ, saves, CEL/CL2/MIN/TIL/SOL extraídos, texturas isoladas, modelos derivados dos arquivos do jogo nem credenciais. Referências de UI geradas por IA ficam em `website/design-references/`, com seus prompts; não entram no artefato publicado e não são evidência do jogo. A identidade visual usa o banner do projeto e D3D nos ícones.

## Identidade e animação

A logomarca transparente original fornecida pelo responsável pelo projeto contém 240 quadros PNG RGBA de 640×160, a 30 fps, em um ciclo de oito segundos. Sua versão WebP animada preserva o alpha, inclusive a transparência gradual das chamas e as faces escuras opacas das letras. O cabeçalho e o destaque inicial usam o WebP animado diretamente no HTML, com repetição automática inclusive sem JavaScript e sem botão de iniciar ou pausar. Por pedido explícito do usuário, a logomarca continua animada quando a preferência de redução de movimento está ativa; o parallax continua respeitando essa preferência. O rodapé usa o quadro estático. A origem da sequência está documentada em [ANIMATED-LOGO.md](ANIMATED-LOGO.md).

Os títulos usam a Cormorant Garamond já hospedada como `D3D Serif`, para uma leitura clara; o texto de leitura continua em Manrope. A Diablo WebFont fornecida pelo usuário é preservada como referência histórica, sem ser aplicada ou carregada nos títulos. As letras Mason já fazem parte do desenho original da logomarca, sem incorporar o arquivo de fonte Mason. As fontes Cormorant e Manrope existentes, seus registros de origem e os textos OFL permanecem em `website/public/assets/fonts/`.

## Verificação local

```powershell
python -m pip install -r website/requirements.txt
python website/build.py
python website/verify.py
python website/serve.py
```

Abra `http://127.0.0.1:4173/diablo-3d/` e `http://127.0.0.1:4173/diablo-3d/en/`. O servidor expõe somente `website/dist/`, não a raiz do repositório. Confira desktop, celular, navegação, busca, ampliação das capturas e repetição automática da logomarca no cabeçalho e no destaque inicial nos dois idiomas antes do commit. Confirme a animação com JavaScript desativado e com redução de movimento ativa, mantendo o parallax reduzido neste último caso. Teste também a troca PT/EN numa seção de artigo e a preferência de idioma do navegador. A saída `website/dist/` e as capturas locais de QA não são versionadas.

O workflow `.github/workflows/pages.yml` valida a publicação e envia **somente** `website/dist/` ao Pages. PRs executam build e verificação; o deploy ocorre em `main` ou por execução manual. A configuração do repositório deve usar **Settings → Pages → Source → GitHub Actions**.

## Controles de primeira pessoa — 8 de outubro de 2026

O registro `2026-10-08-controles-primeira-pessoa.md` e sua tradução completa apresentam a entrega instalada do commit `a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9`: mouse relativo para olhar, setas relativas à câmera usando as oito direções/cadência/colisão nativas, Esc para liberar e clique no mundo para retomar. A ordenação aguarda picking fresco após giro e não cria alvo de chão no céu. A validação passou 673 verificações puras, 1.652 produtivas e regressões 946 HUD/59.452 configurações; o diagnóstico substitui serviços físicos de captura e usa picking CPU, sem comprovar mouse real/Alt+Tab, aparência do retículo ou NPCs/itens/combate. Instalação às 17:30 de Brasília, 41 arquivos do perfil preservados, com alteração posterior somente no recibo esperado de runtime pelo iniciador. O site registra essa evidência; não instala ou testa o jogo novamente.

A comparação histórica de câmeras é reutilizada somente como contexto, sem nova imagem de gameplay ou retículo. Permanecem os 44 registros do inventário. Destaque do devlog, tecnologia, roadmap e fontes correntes acompanham a entrega; o relato humano de melhora no desempenho fica vinculado à GPU anterior, como avaliação subjetiva sem novos FPS. Modelos, materiais, iluminação, GPU, áudio e créditos musicais permanecem em suas frentes próprias.

**Atualização pequena no mesmo artigo:** o commit `4a276a10b090c6bb46f8db5c59c0383bff3bcc84` calibrou a altura ocular de primeira pessoa de 1,1 para 1,7 unidade, preservando o alvo da terceira em 1,1. Instalado às 18:18:44 de Brasília; 202 verificações de câmera, comparação nativa CPU de um ponto diante de Griswold/ferraria em 640×480 e 1.652 regressões de input passaram. Imagem indexada/paleta da terceira idênticas; 44 arquivos do perfil preservados. A seção PT/EN distingue a calibração inicial da avaliação artística pendente e da instalação anterior dos controles (41 arquivos). Destaque, tecnologia, roadmap e snapshot acompanham a revisão. Não há novo artigo, mídia ou evidência; capturas privadas e a imagem humana inacessível não foram publicadas. Nenhuma nova prova de mouse físico, GPU ou FPS. Fonte: [calibração ocular](TRISTRAM-HORIZON-CAMERAS.md#calibração-da-altura-ocular--8-de-outubro).

## Correção de zoom e recuperação da GPU — 8 de outubro de 2026

O novo registro `2026-10-08-gpu-zoom-recuperacao.md` e sua tradução integral documentam a correção instalada do commit `046a1850dd022a9a9b6a93d84cd381970bc82b89`. O stream temporário contava indevidamente desenhos residentes e rejeitava quadros ao atingir 1.048.576 triângulos. A separação mantém limites reais de memória/índices e o fallback de segurança; falhas de capacidade podem recuperar após mudança efetiva de carga e intervalo mínimo de um segundo. O diagnóstico Full HD/RX 570/FOV 80 passou oito poses, com até 1.185.392 triângulos e zero rasterização CPU na sequência, além de 627 sondas e 1.843 verificações sintéticas. O teste de limite real de pixels passou fallback integral e retorno automático após reduzir viewport, sem OFF/ON. Não comprova FPS sustentado ou 60 FPS; perda física do dispositivo não foi induzida. Preparação CPU/readback, orçamento DXGI e seleção explícita de adaptador continuam limitados ou pendentes.

O artigo reutiliza a comparação histórica de câmeras somente como contexto, explicitamente sem capturas novas de gameplay. Nenhuma imagem ou entrada do inventário foi adicionada ou alterada; permanecem os 44 registros. As métricas do post anterior são preservadas com ligação entre as duas entregas. Destaque, tecnologia, roadmap e fontes correntes acompanham a correção. Nenhum modelo, textura, perfil ou recibo privado entra na publicação.

## Suporte, créditos e geometria residente — 8 de outubro de 2026

O registro `2026-10-08-creditos-e-geometria-gpu.md` e sua tradução integral documentam a entrega instalada do commit `fc9642982da2b74b37af366f74c75e966aad4578`. O menu reúne suporte e créditos, mantém as telas herdadas e acrescenta autoria e direção de Douglas Pan, objetivo do jogo inteiro e links da comunidade. Passaram 1.193 verificações nativas, 26 capturas e 103 verificações Godot; gamepad físico e reprodução sonora não foram exercitados. Quatro capturas selecionadas entram como diagnósticos nativos offscreen, com pixels RGBA e resolução preservados em WebP lossless. Não são gameplay. A galeria passa a 44 registros, preservando os 40 anteriores.

Props e árvores em perspectiva agora reutilizam buffers indexados residentes na GPU, com cache geométrico independente de 256 MiB. Construções, cenário individual, atores e isométrica permanecem projetados. As medianas de quatro pares AB/BA em Full HD/RX 570 foram 229,183→77,821 ms em terceira pessoa e 214,149→68,647 ms em primeira: desenho isolado do mundo incluindo readback, excluindo simulação, interface e SDL. Não são FPS sustentado nem uma promessa de 60 FPS. A publicação identifica a classificação de uma borda em 960×540, a exclusão do caminho isométrico residente, 1.768 verificações sintéticas e os gargalos restantes. Início, destaque do devlog, tecnologia, roadmap, galeria e feeds acompanham a nova entrega; os registros anteriores preservam suas medições históricas. Nenhum renderer, perfil, modelo, cache ou dado original do jogo é modificado pela publicação.

## Registro GPU de 8 de outubro de 2026

O registro `2026-10-08-renderizacao-gpu-tristram.md` e sua tradução integral documentam o piloto Windows/Direct3D 11 do commit `c8329403e6b69ff3c95f97adeff38cb4a8c1e8a2`. As páginas atuais de tecnologia, início e roadmap acompanham essa revisão; os artigos de 07/10 preservam o estado histórico. A imagem de abertura reutilizada é explicitamente histórica, sem representar uma comparação CPU/GPU. Nenhuma nova captura local ou arquivo privado entra nesta publicação.

O resultado Full HD é o tempo de desenho do mundo, incluindo readback e excluindo interface/apresentação SDL: 154,00 ms CPU e 29,58 ms GPU, mediana de cinco chamadas aquecidas na Radeon RX 570 com Core i7-14700. Não converter essa medição em uma promessa de FPS. Permanecem explícitos o padrão público desligado, os controles separados de GPU e suavização, o retorno à CPU, a ponte síncrona e o mapa estático de sombras na CPU. Após os testes técnicos, o responsável experimentou a versão GPU na partida habitual e relatou “melhorou muito!!”. O mesmo artigo foi atualizado, com destaque no devlog e tradução integral, para registrar essa melhora percebida. O relato não acrescenta uma medição de FPS nem aceita integralmente os modelos 3D. Seguem o acompanhamento em uso prolongado, a medição sustentada e a revisão da cabana; menus HD e níveis procedurais em 3D permanecem pendentes.

## Registro do editor Godot de 8 de outubro de 2026

O registro `2026-10-08-editor-godot-arquitetura.md` e sua tradução integral documentam a entrega do commit `e17b77f0370df87732e227b7fa479d9b56c44280`: editor externo de arquitetura estática, cena efetivamente montada pelo jogo, salvar/reabrir, exportação explícita e aplicação validada pelo loader C++ em perfil de revisão. Tecnologia, roadmap, destaque do devlog, cartões e RSS acompanham essa entrega. O relato anterior de melhora percebida com a GPU permanece acessível no destaque e em seu artigo.

Os modos de snapshot e GLB fonte, a identidade por hashes e os testes publicados ficam descritos com seus limites. Chão e colisão são referências fixas; o formato inicial aceita materiais básicos e protege a cabana com luz e fogo. A ponte foi demonstrada com uma caixa sintética no vínculo do poço, sem representar uma nova arte aprovada. O artigo também registra a correção da pequena janela da porta, que atravessa a parede interna sem substituir o modelo nem alterar a colisão. A duração da regressão C++ é tempo de execução da suíte, sem constituir um benchmark por quadro.

A imagem reutilizada é explicitamente histórica, anterior ao editor e à correção da janela. Nenhum novo modelo, textura, captura privada ou dado original do jogo entra no site. O próximo passo continua sendo a revisão G1 dos objetos em 360° e a evolução do formato de interiores/luzes; a migração do jogo inteiro para Godot permanece uma avaliação futura. O jogo segue no DevilutionX e o objetivo continua sendo todos os níveis de Diablo 1.

## Rodada de câmeras, qualidade e desempenho — 8 de outubro de 2026

O registro `2026-10-08-tristram-de-perto.md` e sua tradução integral reúnem os grandes avanços das frentes: quatro câmeras, cache de iluminação, descarte espacial, perfil de primeira pessoa, protótipo de malhas persistentes, masters e LOD, revisão da Catedral, pesquisa de chão, menus, música, HUD e comunidade bilíngue. Os testes sustentam as histórias sem transformar o artigo em diário de comandos. O HUD novo ainda não está concluído; a composição funcional anterior é distinguida do acabamento visual reaberto pelo feedback do autor.

Três comparações contextuais novas foram revisadas visualmente e convertidas para WebP lossless: modos de câmera CPU/GPU e Adria antes/depois da transferência experimental. Elas entram na galeria com procedência, hashes, resolução e traduções completas; o inventário passa a 40 capturas. A prévia Godot não representa aparência na partida nem aprovação da recuperação de materiais. Capturas, títulos e legendas preservam defeitos e o caráter de candidato. As fontes públicas foram fixadas em `ae43f0470134af6bb470c68d20fa37647c8a0ec4`, revisão instalada pelo integrador em 8 de outubro. Cache, culling, K e menus são entregues; HUD novo, LOD, malhas persistentes e chão continuam em desenvolvimento. O estudo de chão gerou 30 imagens e passou 13 verificações fora do runtime, sem aplicar seus seis masters privados.

A rodada também inclui `/musica/` e sua versão em inglês: cinco MP3s personalizados gratuitos, player nativo único e downloads diretos. Todas as cópias e o fundo têm Douglas Pan nos créditos internos de artista, artista do álbum e autoria. Catálogo, tags, hashes, leitura completa e preservação dos pacotes de áudio foram verificados; os masters continuam intactos. O fluxo permanente está em `website/MUSIC.md`.

A inscrição voluntária em `/novidades/`, PT/EN, usa Google Forms com respostas privadas, email, idioma e consentimento obrigatório. Uma inscrição e um cancelamento de teste foram realmente persistidos. O cancelamento é manual antes de novos envios; nenhuma campanha foi enviada. A operação está em `website/NEWSLETTER.md`. Home, devlog, biblioteca e rodapé apresentam a inscrição sem exigir cadastro para acesso ou downloads.

## Fontes e limites do primeiro registro

A retrospectiva foi publicada em 07/10/2026 e cobre as etapas locais v1–v4, os cinco primeiros commits D3D até `f673fc709`, o interior e a limpeza do terreno em `63e5e749`, e as duas velas com janelas físicas em `cdeaaab0d`. O histórico anterior pertence ao DevilutionX upstream. O novo quarto existe no modelo Meshy opcional de revisão; o detalhe do piso não é legível pela janela pequena na câmera normal, exigindo os giros. A janela traseira dessa revisão é uma inferência artística aprovada, sem vista original dessa face. Os artigos anteriores preservam o estado e os limites da etapa que representam. Home usa o backend original; pixels iguais nessa rota não aprovam a geometria.

Os títulos, descrições, canonical, OpenGraph, JSON-LD, sitemap e HTML pronto ajudam a interpretação do conteúdo por buscadores e outros sistemas; não garantem indexação ou posição. O `robots.txt` também é gerado, mas em um projeto Pages fica sob `/diablo-3d/robots.txt`: crawlers procuram a política no domínio raiz, que este projeto não controla.
