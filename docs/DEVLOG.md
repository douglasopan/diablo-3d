# Devlog do Diablo 3D

Site: **[Português](https://douglasopan.github.io/diablo-3d/)** · **[English](https://douglasopan.github.io/diablo-3d/en/)** · [RSS PT-BR](https://douglasopan.github.io/diablo-3d/rss.xml) · [RSS EN](https://douglasopan.github.io/diablo-3d/en/rss.xml)

O objetivo do projeto é reconstruir Diablo 1 inteiro em 3D, incluindo todos os níveis procedurais, personagens, monstros, objetos e efeitos. Tristram é a etapa atual de validação; os demais mapas continuam usando a renderização original. Consulte o [roadmap](ROADMAP.md) para distinguir o estado implementado dos próximos marcos.

Os registros são arquivos Markdown versionados em `docs/devlog/`. O gerador em `website/build.py` cria HTML por página, metadados, dados estruturados, sitemap e RSS. Uma publicação em `main` que altere posts ou arquivos do site dispara o workflow dedicado de GitHub Pages. O site funciona sem buscar o conteúdo no navegador e não usa analytics de terceiros.

A publicação é completa em português brasileiro e inglês: a versão PT-BR fica na raiz do projeto e a inglesa em `/en/`. O seletor PT/EN abre a página equivalente e conserva a seção de um artigo. A escolha fica salva no navegador quando o armazenamento local está disponível. Sem uma escolha salva, a página portuguesa direciona para inglês quando esse é o idioma principal do navegador. Links diretos de `/en/` funcionam de forma independente; os dois idiomas têm HTML pronto e continuam legíveis sem JavaScript.

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

## Registro GPU de 8 de outubro de 2026

O registro `2026-10-08-renderizacao-gpu-tristram.md` e sua tradução integral documentam o piloto Windows/Direct3D 11 do commit `c8329403e6b69ff3c95f97adeff38cb4a8c1e8a2`. As páginas atuais de tecnologia, início e roadmap acompanham essa revisão; os artigos de 07/10 preservam o estado histórico. A imagem de abertura reutilizada é explicitamente histórica, sem representar uma comparação CPU/GPU. Nenhuma nova captura local ou arquivo privado entra nesta publicação.

O resultado Full HD é o tempo de desenho do mundo, incluindo readback e excluindo interface/apresentação SDL: 154,00 ms CPU e 29,58 ms GPU, mediana de cinco chamadas aquecidas na Radeon RX 570 com Core i7-14700. Não converter essa medição em uma promessa de FPS. Permanecem explícitos o padrão público desligado, os controles separados de GPU e suavização, o retorno à CPU, a ponte síncrona e o mapa estático de sombras na CPU. Após os testes técnicos, o responsável experimentou a versão GPU na partida habitual e relatou “melhorou muito!!”. O mesmo artigo foi atualizado, com destaque no devlog e tradução integral, para registrar essa melhora percebida. O relato não acrescenta uma medição de FPS nem aceita integralmente os modelos 3D. Seguem o acompanhamento em uso prolongado, a medição sustentada e a revisão da cabana; menus HD e níveis procedurais em 3D permanecem pendentes.

## Registro do editor Godot de 8 de outubro de 2026

O registro `2026-10-08-editor-godot-arquitetura.md` e sua tradução integral documentam a entrega do commit `e17b77f0370df87732e227b7fa479d9b56c44280`: editor externo de arquitetura estática, cena efetivamente montada pelo jogo, salvar/reabrir, exportação explícita e aplicação validada pelo loader C++ em perfil de revisão. Tecnologia, roadmap, destaque do devlog, cartões e RSS acompanham essa entrega. O relato anterior de melhora percebida com a GPU permanece acessível no destaque e em seu artigo.

Os modos de snapshot e GLB fonte, a identidade por hashes e os testes publicados ficam descritos com seus limites. Chão e colisão são referências fixas; o formato inicial aceita materiais básicos e protege a cabana com luz e fogo. A ponte foi demonstrada com uma caixa sintética no vínculo do poço, sem representar uma nova arte aprovada. O artigo também registra a correção da pequena janela da porta, que atravessa a parede interna sem substituir o modelo nem alterar a colisão. A duração da regressão C++ é tempo de execução da suíte, sem constituir um benchmark por quadro.

A imagem reutilizada é explicitamente histórica, anterior ao editor e à correção da janela. Nenhum novo modelo, textura, captura privada ou dado original do jogo entra no site. O próximo passo continua sendo a revisão G1 dos objetos em 360° e a evolução do formato de interiores/luzes; a migração do jogo inteiro para Godot permanece uma avaliação futura. O jogo segue no DevilutionX e o objetivo continua sendo todos os níveis de Diablo 1.

## Fontes e limites do primeiro registro

A retrospectiva foi publicada em 07/10/2026 e cobre as etapas locais v1–v4, os cinco primeiros commits D3D até `f673fc709`, o interior e a limpeza do terreno em `63e5e749`, e as duas velas com janelas físicas em `cdeaaab0d`. O histórico anterior pertence ao DevilutionX upstream. O novo quarto existe no modelo Meshy opcional de revisão; o detalhe do piso não é legível pela janela pequena na câmera normal, exigindo os giros. A janela traseira dessa revisão é uma inferência artística aprovada, sem vista original dessa face. Os artigos anteriores preservam o estado e os limites da etapa que representam. Home usa o backend original; pixels iguais nessa rota não aprovam a geometria.

Os títulos, descrições, canonical, OpenGraph, JSON-LD, sitemap e HTML pronto ajudam a interpretação do conteúdo por buscadores e outros sistemas; não garantem indexação ou posição. O `robots.txt` também é gerado, mas em um projeto Pages fica sob `/diablo-3d/robots.txt`: crawlers procuram a política no domínio raiz, que este projeto não controla.
