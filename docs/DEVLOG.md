# Devlog do Diablo 3D

Site: **[douglasopan.github.io/diablo-3d](https://douglasopan.github.io/diablo-3d/)** · [RSS](https://douglasopan.github.io/diablo-3d/rss.xml)

O objetivo do projeto é reconstruir Diablo 1 inteiro em 3D, incluindo todos os níveis procedurais, personagens, monstros, objetos e efeitos. Tristram é a etapa atual de validação; os demais mapas continuam usando a renderização original. Consulte o [roadmap](ROADMAP.md) para distinguir o estado implementado dos próximos marcos.

Os registros são arquivos Markdown versionados em `docs/devlog/`. O gerador em `website/build.py` cria HTML por página, metadados, dados estruturados, sitemap e RSS. Uma publicação em `main` que altere posts ou arquivos do site dispara o workflow dedicado de GitHub Pages. O site funciona sem buscar o conteúdo no navegador e não usa analytics de terceiros.

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

## Imagens e galeria

Copie somente capturas contextuais revisadas para `website/public/assets/captures/`. Prefira WebP lossless na resolução original, com legenda e texto alternativo. Antes de publicar, confira visualmente dados pessoais, telas de contas e chaves.

Cadastre a imagem em `website/evidence.json` para incluí-la na galeria. Registre `file`, `title`, `alt`, `caption`, `category`, `stage`, `source`, `sha256` do original e `duplicate_sources`. O inventário público reúne apenas as selecionadas; o inventário local completo e mapeamentos privados permanecem fora do repositório.

Não publique MPQ, saves, CEL/CL2/MIN/TIL/SOL extraídos, texturas isoladas, modelos derivados dos arquivos do jogo nem credenciais. Referências de UI geradas por IA ficam em `website/design-references/`, com seus prompts; não entram no artefato publicado e não são evidência do jogo. A identidade visual usa o banner do projeto e D3D nos ícones.

## Verificação local

```powershell
python -m pip install -r website/requirements.txt
python website/build.py
python website/verify.py
python website/serve.py
```

Abra `http://127.0.0.1:4173/diablo-3d/`. O servidor expõe somente `website/dist/`, não a raiz do repositório. Confira desktop, celular, navegação, busca e ampliação das capturas antes do commit. A saída `website/dist/` e as capturas locais de QA não são versionadas.

O workflow `.github/workflows/pages.yml` valida a publicação e envia **somente** `website/dist/` ao Pages. PRs executam build e verificação; o deploy ocorre em `main` ou por execução manual. A configuração do repositório deve usar **Settings → Pages → Source → GitHub Actions**.

## Fontes e limites do primeiro registro

A retrospectiva foi publicada em 07/10/2026 e cobre as etapas locais v1–v4, os cinco primeiros commits D3D até `f673fc709`, o interior e a limpeza do terreno em `63e5e749`, e as duas velas com janelas físicas em `cdeaaab0d`. O histórico anterior pertence ao DevilutionX upstream. O novo quarto existe no modelo Meshy opcional de revisão; o detalhe do piso não é legível pela janela pequena na câmera normal, exigindo os giros. A janela traseira dessa revisão é uma inferência artística aprovada, sem vista original dessa face. Os artigos anteriores preservam o estado e os limites da etapa que representam. Home usa o backend original; pixels iguais nessa rota não aprovam a geometria.

Os títulos, descrições, canonical, OpenGraph, JSON-LD, sitemap e HTML pronto ajudam a interpretação do conteúdo por buscadores e outros sistemas; não garantem indexação ou posição. O `robots.txt` também é gerado, mas em um projeto Pages fica sob `/diablo-3d/robots.txt`: crawlers procuram a política no domínio raiz, que este projeto não controla.
