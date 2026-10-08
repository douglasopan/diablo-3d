"""D3D's small static publisher. Only website/public is copied to the artifact."""
from __future__ import annotations

import datetime as dt
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
from html.parser import HTMLParser
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

import markdown
from PIL import Image
import yaml

from translations import EVIDENCE_EN, translate_text

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
PUBLIC = ROOT / 'public'
OUT = ROOT / 'dist'
BASE = os.environ.get('SITE_URL', 'https://douglasopan.github.io/diablo-3d').rstrip('/')
parsed_base = urlsplit(BASE)
if parsed_base.scheme not in ('http', 'https') or not parsed_base.netloc or parsed_base.query or parsed_base.fragment:
    raise ValueError('SITE_URL must be an absolute HTTP(S) URL without a query or fragment')
PREFIX = parsed_base.path.rstrip('/')
GITHUB = 'https://github.com/douglasopan/diablo-3d'
SNAPSHOT = 'cdeaaab0d208bf0da4239b98c287619508e308a5'
DISCORD = 'https://discord.gg/4YxQ7s69S'
NAV = [('Início', '/'), ('Devlog', '/devlog/'), ('Projeto', '/projeto/'), ('Tecnologia', '/tecnologia/'), ('Participar', '/participar/'), ('Apoiar', '/apoiar/'), ('Roadmap', '/roadmap/'), ('Galeria', '/galeria/')]
CATEGORIES = ('Protótipo', 'Geometria', 'Personagens', 'Comunidade', 'Ferramentas', 'Luz')
MONTHS = ('janeiro', 'fevereiro', 'março', 'abril', 'maio', 'junho', 'julho', 'agosto', 'setembro', 'outubro', 'novembro', 'dezembro')
EN_MONTHS = ('January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December')
LANG = 'pt-BR'


def localized_path(path, language=None):
    language = language or LANG
    routes = ('/devlog/', '/projeto/', '/tecnologia/', '/participar/', '/apoiar/', '/roadmap/', '/galeria/')
    route = urlsplit(path).path
    if language == 'en' and (route in ('/', '/rss.xml', '/404.html', '/evidence.json') or route.startswith(routes)):
        return '/en' + path
    return path


def alternate_url(path, language):
    return BASE + localized_path(path, language)


def translated(value):
    return translate_text(str(value)) if LANG == 'en' else str(value)


class LocalizedHTML(HTMLParser):
    """Translate authored text/labels without rewriting code, URLs or markup."""
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.verbatim = []

    def handle_decl(self, declaration):
        self.parts.append('<!' + declaration + '>')

    def handle_starttag(self, tag, attrs):
        localizable = {'alt', 'aria-label', 'placeholder', 'title'}
        attrs = dict(attrs)
        if tag == 'meta' and (attrs.get('name') == 'description' or attrs.get('property', '').startswith('og:') or attrs.get('name', '').startswith('twitter:')):
            localizable.add('content')
        for key in localizable & attrs.keys():
            attrs[key] = translated(attrs[key] or '')
        self.parts.append('<' + tag + ''.join(f' {key}' if value is None else f' {key}="{esc(value)}"' for key, value in attrs.items()) + '>')
        if tag in ('script', 'style', 'code', 'pre'):
            self.verbatim.append(tag)

    def handle_endtag(self, tag):
        self.parts.append(f'</{tag}>')
        if self.verbatim and self.verbatim[-1] == tag:
            self.verbatim.pop()

    def handle_data(self, data):
        if self.verbatim and self.verbatim[-1] in ('script', 'style'):
            self.parts.append(data)
        else:
            self.parts.append(esc(data if self.verbatim else translated(data)))

    def handle_comment(self, comment):
        self.parts.append('<!--' + comment + '-->')


def localize_html(text):
    if LANG != 'en':
        return text
    parser = LocalizedHTML()
    parser.feed(text)
    return ''.join(parser.parts)


def esc(value):
    return html.escape(str(value), quote=True)


def url(path):
    return PREFIX + localized_path(path)


def versioned_asset(path):
    digest = hashlib.sha256((PUBLIC / path.lstrip('/')).read_bytes()).hexdigest()[:12]
    return url(path) + '?v=' + digest


def absolute(path):
    return BASE + localized_path(path)


def source(path, pinned=True):
    return f'{GITHUB}/blob/{SNAPSHOT if pinned else "main"}/{path}'


def date_label(date):
    day = dt.date.fromisoformat(str(date))
    if LANG == 'en':
        return f'{EN_MONTHS[day.month - 1]} {day.day}, {day.year}'
    return f'{day.day:02d} de {MONTHS[day.month - 1]} de {day.year}'


def image_info(path):
    if not path.startswith('/assets/') or '..' in Path(path).parts:
        raise ValueError(f'Image must be in public /assets/: {path}')
    file = PUBLIC / path.lstrip('/')
    if not file.is_file():
        raise ValueError(f'Missing public image: {path}')
    with Image.open(file) as im:
        return im.size


def img(path, alt, eager=False, cls=''):
    width, height = image_info(path)
    return f'<img src="{esc(url(path))}" alt="{esc(alt)}" width="{width}" height="{height}" loading="{"eager" if eager else "lazy"}" decoding="async"{(" class=" + chr(34) + esc(cls) + chr(34)) if cls else ""}>'


def button(label, href, secondary=False):
    target = url(href) if href.startswith('/') else href
    return f'<a class="button{" secondary" if secondary else ""}" href="{esc(target)}">{esc(label)} <span aria-hidden="true">↗</span></a>'


def logo(eager=False, animated=False, cls='brand-logo'):
    asset = 'd3d-logo-animated.webp' if animated else 'd3d-logo-static.webp'
    return img('/assets/branding/' + asset, 'Diablo 3D', eager, cls)


class ContentLinks(HTMLParser):
    """Validate rendered Markdown links and add dimensions to local images."""
    def __init__(self, language=None):
        super().__init__(convert_charrefs=False)
        self.parts = []
        self.language = language or LANG

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag in ('script', 'iframe', 'style', 'object', 'embed', 'form') or any(k.startswith('on') for k in attrs):
            raise ValueError('Active HTML is not allowed in devlog content')
        for key in ('href', 'src'):
            if key not in attrs:
                continue
            value = attrs[key] or ''
            if value.startswith('//') or urlsplit(value).scheme not in ('', 'http', 'https'):
                raise ValueError(f'Unsupported content URL: {value}')
            if key == 'src':
                width, height = image_info(value)
                if not attrs.get('alt'):
                    raise ValueError('Devlog images require meaningful alt text')
                attrs.update(width=str(width), height=str(height), loading='lazy', decoding='async')
            if value.startswith('/'):
                attrs[key] = PREFIX + localized_path(value, self.language)
        self.parts.append('<' + tag + ''.join(f' {k}="{esc(v or "")}"' for k, v in attrs.items()) + '>')

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        self.parts.append(f'</{tag}>')

    def handle_data(self, data):
        self.parts.append(data)

    def handle_entityref(self, name):
        self.parts.append(f'&{name};')

    def handle_charref(self, name):
        self.parts.append(f'&#{name};')


def render_markdown(text, language=None):
    parser = markdown.Markdown(extensions=['tables', 'fenced_code', 'sane_lists', 'toc'], output_format='html')
    rendered = parser.convert(text)
    validator = ContentLinks(language)
    validator.feed(rendered)
    return ''.join(validator.parts), parser.toc


def load_posts(language=None):
    language = language or LANG
    posts = []
    slugs = set()
    for path in sorted((REPO / 'docs/devlog').glob('*.md')):
        raw = path.read_text(encoding='utf-8')
        match = re.match(r'\A---\s*\n(.*?)\n---\s*\n(.*)\Z', raw, re.S)
        if not match:
            raise ValueError(f'{path.name}: YAML frontmatter required')
        data = yaml.safe_load(match[1])
        if not isinstance(data, dict):
            raise ValueError(f'{path.name}: frontmatter must be an object')
        if data.get('status', 'published') not in ('draft', 'published'):
            raise ValueError(f'{path.name}: status must be draft or published')
        if data.get('status') == 'draft':
            continue
        for key in ('title', 'date', 'description', 'slug', 'image', 'image_alt', 'category'):
            if not data.get(key):
                raise ValueError(f'{path.name}: missing {key}')
        slug = str(data['slug'])
        if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', slug) or slug in slugs:
            raise ValueError(f'{path.name}: invalid or duplicate slug')
        if data['category'] not in CATEGORIES:
            raise ValueError(f'{path.name}: unknown category')
        slugs.add(slug)
        data['date'] = dt.date.fromisoformat(str(data['date'])).isoformat()
        if data.get('updated'):
            data['updated'] = dt.date.fromisoformat(str(data['updated'])).isoformat()
        if not isinstance(data.get('order', 0), int):
            raise ValueError(f'{path.name}: order must be an integer')
        image_info(data['image'])
        data['body'], data['toc'] = render_markdown(match[2])
        if language == 'en':
            translated_file = ROOT / 'content/en/devlog' / path.name
            if not translated_file.is_file():
                raise ValueError(f'{path.name}: complete English translation required')
            english = re.match(r'\A---\s*\n(.*?)\n---\s*\n(.*)\Z', translated_file.read_text(encoding='utf-8'), re.S)
            if not english:
                raise ValueError(f'{path.name}: English frontmatter required')
            english_data = yaml.safe_load(english[1])
            for key in ('slug', 'date', 'updated', 'image', 'category', 'order', 'status'):
                if str(english_data.get(key, 'published' if key == 'status' else None)) != str(data.get(key, 'published' if key == 'status' else None)):
                    raise ValueError(f'{path.name}: English {key} must match original metadata')
            for key in ('title', 'description', 'image_alt'):
                if not english_data.get(key):
                    raise ValueError(f'{path.name}: English {key} required')
                data[key] = english_data[key]
            body, toc = render_markdown(english[2], language)
            headings = r'(<h[1-6]\b[^>]*\bid=")([^"]+)(")'
            original_ids = [heading[1] for heading in re.findall(headings, data['body'])]
            english_ids = [heading[1] for heading in re.findall(headings, body)]
            if len(original_ids) != len(english_ids):
                raise ValueError(f'{path.name}: English heading structure must match original')
            anchors = dict(zip(english_ids, original_ids))
            body = re.sub(headings, lambda match: match[1] + anchors[match[2]] + match[3], body)
            links = r'href="#([^"]+)"'
            data['body'] = re.sub(links, lambda match: 'href="#' + anchors.get(match[1], match[1]) + '"', body)
            data['toc'] = re.sub(links, lambda match: 'href="#' + anchors.get(match[1], match[1]) + '"', toc)
            match_body = english[2]
        else:
            match_body = match[2]
        data['minutes'] = max(1, round(len(re.findall(r'\w+', match_body)) / 200))
        data['path'] = f'/devlog/{slug}/'
        data['file'] = path.name
        data['source_file'] = ('website/content/en/devlog/' if language == 'en' else 'docs/devlog/') + path.name
        posts.append(data)
    if not posts:
        raise ValueError('At least one published devlog article is required')
    return sorted(posts, key=lambda p: (p['date'], p.get('order', 0), p['slug']), reverse=True)


def card(post):
    return f'''<article class="card" data-filter-item data-category="{esc(post['category'])}" data-search="{esc(post['title'] + ' ' + post['description'] + ' ' + translated(post['category']))}">
      <a class="card-media" href="{url(post['path'])}" tabindex="-1" aria-hidden="true">{img(post['image'], '')}</a>
      <div class="card-body"><div class="meta"><span class="tag">{esc(post['category'])}</span><time datetime="{post['date']}">{date_label(post['date'])}</time></div>
      <h3><a href="{url(post['path'])}">{esc(post['title'])}</a></h3><p>{esc(post['description'])}</p><span class="read-more">{post['minutes']} {translated('min de leitura')} <span aria-hidden="true">↗</span></span></div></article>'''


def section_head(label, subtitle='', href=None):
    return f'<div class="section-heading"><div><h2>{label}</h2>{"<p>" + subtitle + "</p>" if subtitle else ""}</div>{button("Ver todos", href, True) if href else ""}</div>'


def scene(path='/assets/art/tristram-atmosphere.webp', eager=False):
    picture = img(path, '', eager).replace(' decoding="async"', ' decoding="async" data-parallax role="presentation"')
    return f'<div class="parallax-scene" aria-hidden="true">{picture}</div><div class="scene-shade"></div>'


def art_credit():
    return '<p class="art-credit">Ilustração de ambientação</p>'


def intro(eyebrow, title, description):
    art = '/assets/art/journal-atmosphere.webp' if eyebrow in ('DEVLOG', 'TECNOLOGIA') else '/assets/art/tristram-atmosphere.webp'
    return f'<header class="page-intro atmospheric bleed">{scene(art, True)}<div class="intro-copy"><p class="eyebrow">{eyebrow}</p><h1>{title}</h1><p class="lede">{description}</p></div>{art_credit()}</header>'


def filters(categories, search=False):
    search_html = '<div class="search-field"><label class="sr-only" for="search">Buscar registros do devlog</label><input id="search" type="search" placeholder="Buscar no diário…" autocomplete="off"></div>' if search else ''
    return '<div class="filter-bar" hidden data-enhancement><div class="filter-buttons" role="group" aria-label="Filtrar por tema">' + ''.join(f'<button type="button" data-filter="{esc(c)}" aria-pressed="{str(c == "Todos").lower()}">{esc(c)}</button>' for c in ['Todos', *categories]) + f'</div>{search_html}</div><p id="result-count" class="meta" aria-live="polite"></p>'


def no_results():
    return '<div id="no-results" class="panel empty-state" hidden><h2>Nenhum registro encontrado.</h2><p>Tente outro termo ou veja todas as etapas.</p><button id="reset-filters" class="button secondary" type="button">Limpar filtros</button></div>'


def lightbox():
    return '''<dialog id="image-dialog" aria-label="Visualizador de captura" aria-describedby="dialog-caption"><div class="dialog-toolbar"><span id="dialog-counter"></span><button type="button" class="dialog-close" aria-label="Fechar captura">Fechar ×</button></div><img id="dialog-image" alt=""><p id="dialog-caption"></p><div class="dialog-controls"><button type="button" id="dialog-prev" aria-label="Captura anterior">← Anterior</button><button type="button" id="dialog-next" aria-label="Próxima captura">Próxima →</button></div></dialog>'''


def frame(title, description, path, content, image='/assets/banner.webp', article=None, noindex=False):
    title, description = translated(title), translated(description)
    canonical = absolute(path)
    breadcrumb = [{'@type': 'ListItem', 'position': 1, 'name': translated('Início'), 'item': absolute('/')}]
    if path != '/':
        if article:
            breadcrumb.append({'@type': 'ListItem', 'position': 2, 'name': 'Devlog', 'item': absolute('/devlog/')})
        breadcrumb.append({'@type': 'ListItem', 'position': len(breadcrumb) + 1, 'name': title, 'item': canonical})
    structured = [
        {'@context': 'https://schema.org', '@type': 'WebSite', '@id': absolute('/#website'), 'url': absolute('/'), 'name': 'Diablo 3D · D3D', 'inLanguage': LANG, 'description': translated('Projeto independente para reconstruir todo Diablo 1 em 3D, com todos os níveis. O protótipo atual está em Tristram, sobre DevilutionX.')},
        {'@context': 'https://schema.org', '@type': 'BreadcrumbList', 'itemListElement': breadcrumb},
        {'@context': 'https://schema.org', '@type': 'BlogPosting' if article else 'WebPage', 'headline' if article else 'name': title, 'description': description, 'url': canonical, 'inLanguage': LANG, 'isPartOf': {'@id': absolute('/#website')}, **({'datePublished': article['date'], 'dateModified': article.get('updated', article['date']), 'image': absolute(image), 'author': {'@type': 'Organization', 'name': translated('Projeto D3D'), 'url': absolute('/projeto/')}, 'mainEntityOfPage': canonical} if article else {})}
    ]
    nav = ''.join(f'<a href="{url(p)}"{" aria-current=" + chr(34) + "page" + chr(34) if (p == path or (p == "/devlog/" and article)) else ""}>{name}</a>' for name, p in NAV)
    article_meta = f'<meta property="article:published_time" content="{article["date"]}"><meta property="article:modified_time" content="{article.get("updated", article["date"])}">' if article else ''
    languages = '<nav class="language-switch" aria-label="Idioma">' + ''.join(f'<a href="{urlsplit(alternate_url(path, lang)).path}" hreflang="{lang}" lang="{lang}" data-language-link="{lang}" aria-label="{label}"' + (' aria-current="true"' if LANG == lang else '') + f'>{short}</a>' for lang, label, short in [('pt-BR', 'Ler em português', 'PT'), ('en', 'Read in English', 'EN')]) + '</nav>'
    alternates = ''.join(f'<link rel="alternate" hreflang="{lang}" href="{alternate_url(path, lang)}">' for lang in ('pt-BR', 'en', 'x-default'))
    document = f'''<!doctype html>
<html lang="{LANG}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)} · Diablo 3D</title><meta name="description" content="{esc(description)}"><meta name="robots" content="{'noindex, follow' if noindex else 'index, follow'}"><link rel="canonical" href="{canonical}">
<meta name="theme-color" content="#111211"><meta property="og:locale" content="{'en_US' if LANG == 'en' else 'pt_BR'}"><meta property="og:type" content="{'article' if article else 'website'}"><meta property="og:site_name" content="Diablo 3D · D3D"><meta property="og:title" content="{esc(title)}"><meta property="og:description" content="{esc(description)}"><meta property="og:url" content="{canonical}"><meta property="og:image" content="{absolute(image)}"><meta property="og:image:alt" content="{esc(article['image_alt'] if article else 'Banner oficial do projeto Diablo 3D')}">{article_meta}
<meta name="twitter:card" content="summary_large_image"><meta name="twitter:title" content="{esc(title)}"><meta name="twitter:description" content="{esc(description)}"><meta name="twitter:image" content="{absolute(image)}">
<link rel="icon" type="image/png" href="{url('/assets/d3d-icon.png')}"><link rel="apple-touch-icon" href="{url('/assets/d3d-touch.png')}"><link rel="alternate" type="application/rss+xml" title="Diablo 3D — Devlog" href="{url('/rss.xml')}"><link rel="stylesheet" href="{versioned_asset('/site.css')}">
{alternates}<script src="{versioned_asset('/language.js')}"></script><script type="application/ld+json">{json.dumps(structured, ensure_ascii=False).replace('<', chr(92) + 'u003c')}</script><script src="{versioned_asset('/site.js')}" defer></script></head>
<body><a class="skip-link" href="#main">Pular para o conteúdo</a><header class="site-header"><div class="wrap"><a class="brand" href="{url('/')}" aria-label="D3D — Início">{logo(True, True)}</a><button class="menu-toggle" type="button" aria-controls="navigation" aria-expanded="false" hidden data-enhancement>Menu <span aria-hidden="true">☰</span></button><nav id="navigation" class="main-nav" aria-label="Navegação principal">{nav}</nav>{languages}</div></header>
<main id="main" class="wrap">{content}</main><footer class="site-footer"><div class="wrap"><div class="footer-top"><a class="brand" href="{url('/')}">{logo()}</a><nav aria-label="Links do projeto"><a href="{GITHUB}">GitHub ↗</a><a href="{DISCORD}">Discord ↗</a><a href="{url('/apoiar/')}">Apoiar ↗</a><a href="{url('/rss.xml')}">RSS ↗</a><a href="{source('LICENSE.md', False)}">Licença ↗</a></nav></div><p>Projeto de fã independente, sem afiliação com a Blizzard Entertainment. Diablo e suas marcas pertencem aos respectivos titulares.</p><p>Código público sob <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>: distribuição gratuita e não comercial. Dados originais do jogo não são distribuídos.</p></div></footer>{lightbox()}</body></html>'''
    return localize_html(document)


def home(posts):
    return f'''<section class="hero hero-atmospheric atmospheric bleed">{scene(eager=True)}<div class="hero-copy"><p class="eyebrow">DIABLO 3D · DEVLOG ABERTO</p>{logo(True, True, 'project-banner')}<h1>Diablo 1 sob uma nova dimensão.</h1><p class="lede">Reconstruir todo Diablo 1 em 3D, com todos os níveis, preservando a partida e a atmosfera do original. Tristram é o primeiro marco de um trabalho que avança com testes e colaboração.</p><div class="actions">{button('Acompanhar o devlog', '/devlog/')}{button('Apoiar o projeto', '/apoiar/', True)}</div><div class="status-strip"><span><i aria-hidden="true"></i> Objetivo: jogo completo</span><span>Etapa atual: Tristram</span><span>Protótipo offline · CPU</span></div></div>{art_credit()}</section><figure class="wide-capture evidence-capture"><a href="{url('/galeria/')}">{img('/assets/captures/cabin-interior-final.webp', 'Cabana leste: jogo original, protótipo antes e protótipo depois do interior iluminado', True)}</a><figcaption><span class="tag">CAPTURA REAL · 63e5e749</span> Original, antes e depois: o interior da cabana de revisão em Tristram. Forma e materiais continuam em revisão.</figcaption></figure>
<section class="section-atmosphere atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap">{section_head('Cada mudança deixa um registro.', 'Capturas, decisões e limites do desenvolvimento.', '/devlog/')}<div class="card-grid">{''.join(card(p) for p in posts[:3])}</div></div>{art_credit()}</section>
<section class="split"><div class="panel"><p class="eyebrow">UMA PARTIDA, DUAS VISÕES</p><h2>O jogo continua.<br>A câmera muda.</h2><p><kbd>F4</kbd> alterna entre o original e o protótipo na mesma partida. Movimento, colisões, inventário e interação continuam usando a simulação do DevilutionX.</p><p class="notice"><kbd>Home</kbd> retorna ao backend original. Pixels iguais nessa rota comprovam esse retorno; a fidelidade da geometria precisa de comparações com a malha ativa.</p>{button('Entender a tecnologia', '/tecnologia/', True)}</div><div class="panel"><p class="eyebrow">DO PRIMEIRO MARCO AO JOGO COMPLETO</p><h2>A jornada começa<br>em Tristram.</h2><p>A cidade é a etapa atual de calibração. Depois vem o primeiro nível procedural da Catedral, seguido dos demais níveis da Catedral, Catacumbas, Cavernas e Inferno, com o conteúdo do jogo.</p><p class="meta">As masmorras ainda usam o renderer original. Os marcos futuros serão registrados com evidências à medida que forem implementados.</p>{button('Ver o roadmap completo', '/roadmap/', True)}</div></section>
<section class="contribute-banner chapter-band atmospheric bleed">{scene()}<div class="atmosphere-content wrap"><div><p class="eyebrow">UM PROJETO EM COMUNIDADE</p><h2>Ajude a construir o próximo capítulo.</h2><p>Modelagem, código, testes e documentação fazem o projeto avançar. Apoio financeiro e ajuda com ferramentas de geração sustentam as revisões, a continuidade e a expansão para todos os níveis.</p></div><div class="actions">{button('Apoiar o desenvolvimento', '/apoiar/')}{button('Como contribuir', '/participar/', True)}</div></div>{art_credit()}</section>'''


def project():
    return intro('SOBRE O PROJETO', 'Todo Diablo 1.<br>Reconstruído em 3D.', 'Diablo 3D, ou D3D, é um projeto de fã independente sobre DevilutionX. O objetivo é reconstruir o jogo inteiro, com todos os níveis e seu conteúdo. Tristram é a etapa atual de desenvolvimento.') + f'''
<figure class="wide-capture">{img('/assets/captures/v4-town.webp', 'Vista contextual da cidade de Tristram na etapa v4')}<figcaption>Registro real do protótipo v4. A cidade ainda exige revisão visual e de materiais.</figcaption></figure>
<section>{section_head('Três compromissos.')}<div class="tech-grid"><div class="tech-card"><span class="eyebrow">01 · FIDELIDADE</span><h3>Começar pelo original</h3><p>Composição, escala, portas, janelas, silhuetas e posição. A vista original é uma referência; as outras faces também precisam fazer sentido.</p></div><div class="tech-card"><span class="eyebrow">02 · CONTINUIDADE</span><h3>Preservar a partida</h3><p>F4 troca a visão na mesma simulação. Movimento, inventário e NPCs seguem o engine existente. Masmorras ainda usam a renderização original.</p></div><div class="tech-card"><span class="eyebrow">03 · COLABORAÇÃO</span><h3>Revisar em público</h3><p>Código, guias, catálogo e registros ficam no GitHub. Colaboradores podem reservar famílias de modelos, documentar a origem e submeter comparações.</p></div></div></section>
<section class="split"><div class="panel prose"><h2>De onde partimos</h2><p>O protótipo começou a partir do commit <a href="https://github.com/diasurgical/devilutionX/commit/dac104babfb6187415432f428ac2516747ffc154">dac104bab do DevilutionX</a>. O histórico anterior pertence ao engine upstream. A primeira publicação consolidada de D3D ocorreu em 07 de outubro de 2026 e já reunia as etapas locais v1 a v4.</p><p>Este devlog distingue experimentos locais, commits publicados e planos. Capturas artificiais usadas para projetar a interface não são apresentadas como imagens do jogo.</p></div><div class="panel prose"><h2>O que existe hoje</h2><p>Uma visão girável da cidade, geometria agrupada, personagens com profundidade e ferramentas de auditoria. A reconstrução ainda tem diferenças de forma, textura, oclusão e iluminação.</p><p>A cabana leste do modelo Meshy opcional de revisão possui um quarto físico, tábuas de madeira, duas janelas recortadas e duas velas com oscilação sutil. A janela traseira é uma inferência de projeto aprovada, sem referência original de sua face posterior. O detalhe do piso exige inspeção no giro.</p><p>A limpeza de sombras pintadas cobre oito peças auditadas das cabanas; outros objetos ainda precisam de revisão. O build é offline, com <code>NONET=ON</code>. Mais jogadores e voz por proximidade são pesquisa futura.</p></div></section>
<section>{section_head('Uma visão completa. Etapas concretas.')}<ol class="scope-path"><li><span class="tag">AGORA</span><h3>Tristram</h3><p>Calibrar objetos, personagens, luz, câmera e comparação com o original.</p></li><li><span class="tag">PRÓXIMO MARCO</span><h3>Primeiro nível da Catedral</h3><p>Levar a reconstrução 3D ao mapa procedural da mesma partida.</p></li><li><span class="tag">OBJETIVO COMPLETO</span><h3>Todos os níveis</h3><p>Demais níveis da Catedral, Catacumbas, Cavernas e Inferno, com inimigos, objetos e conteúdo do jogo.</p></li></ol><div class="actions">{button('Participar', '/participar/')}{button('Apoiar o projeto', '/apoiar/', True)}</div></section>
<aside class="notice"><h2>Colaboração gratuita e não comercial</h2><p>A licença herdada é a <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>. Não é MIT, GPL ou licença aprovada pela OSI. O acesso ao código não concede direitos sobre os recursos proprietários do jogo.</p></aside>'''


def technology():
    cards = [('DevilutionX / C++', 'Simulação, mapas, controles e interação continuam no engine existente.', 'docs/BUILDING-D3D.md'), ('Renderer de software', 'Rasterização na CPU e conversão para a paleta do jogo. O protótipo não é uma implementação de PBR completo.', 'docs/TRISTRAM-STATUS.pt-BR.md'), ('Auditoria headless', 'Capturas reproduzíveis, seleção, contato com o chão, projeção e comparações com malha forçada.', 'docs/TRISTRAM-STATUS.pt-BR.md'), ('Python', 'Ferramentas locais de referência, conversão e organização. Arquivos extraídos do jogo permanecem fora do site.', 'docs/MESHY-WORKFLOW.md'), ('Meshy · opcional', 'Geração multi-view e importação de candidatos para revisão. Gerar um modelo não comprova sua fidelidade.', 'docs/MESHY-WORKFLOW.md'), ('GitHub e Discord', 'Histórico de código, reservas de objetos, revisão por evidência e coordenação de colaboradores.', 'docs/CONTRIBUTING.md')]
    return intro('TECNOLOGIA', 'Uma nova visão.<br>O mesmo jogo.', 'Uma base para reconstruir todo Diablo 1 em 3D. Hoje, a geometria de Tristram trabalha sobre o mapa da mesma partida; o renderer de software produz a imagem na CPU, com a paleta do jogo.') + f'''
<section>{section_head('Do mapa à imagem.')}<ol class="pipeline"><li><span>01</span><h3>Mapa e simulação</h3><p>Estado da mesma partida</p></li><li><span>02</span><h3>Geometria agrupada</h3><p>Objetos inteiros e volumes</p></li><li><span>03</span><h3>Luz e paleta</h3><p>RGB linear nos materiais importados</p></li><li><span>04</span><h3>Imagem na CPU</h3><p>Rasterização de software</p></li></ol></section>
<section>{section_head('Ferramentas e decisões.')}<div class="tech-grid">{''.join(f'<div class="tech-card"><span class="eyebrow">0{i+1}</span><h3>{t}</h3><p>{d}</p><a href="{source(s)}">Ler a documentação ↗</a></div>' for i,(t,d,s) in enumerate(cards))}</div></section>
<section class="split"><div class="panel prose"><h2>Comparar a rota correta</h2><p><kbd>F4</kbd> alterna original e 3D. <kbd>Home</kbd> restaura o enquadramento nativo e usa o backend original. Esse retorno pode produzir pixels idênticos sem provar uma malha fiel.</p><p>Para revisar um objeto, use malha forçada na perspectiva original, giros próximos de ±5° e os quatro ângulos principais. A oclusão e as faces não vistas também contam.</p></div><div class="panel prose"><h2>Luz externa e interior</h2><p>A calibração publicada ilumina a cor-base dos materiais importados em RGB linear antes de convertê-la para a paleta. A arte nativa ainda conserva iluminação pintada e tratamento de compatibilidade. Sombras geométricas atuais cobrem arquitetura estática.</p><p>Árvores, pedras e personagens ainda não participam desse mesmo mapa de sombras. A cabana de revisão possui duas fontes pontuais de velas, com oscilação discreta e paredes que bloqueiam seu vazamento. Travessões e adereços ainda não projetam sombras pontuais individuais. Materiais PBR do modelo importado não estão integralmente aplicados.</p></div></section>
<details class="panel"><summary>O que vem depois e o que está em pesquisa</summary><div class="prose"><p>Depois de Tristram: primeiro nível procedural da Catedral, seguido de todos os demais níveis e conteúdos do jogo. As masmorras ainda usam o renderer original.</p><p>Dia/noite, horizonte e fog são planos posteriores. Expansão lateral de mapas continua aberta. Rede e voz por proximidade precisam de investigação e validação próprias.</p><p>O build atual é offline (<code>NONET=ON</code>). Alterar um limite de jogadores não cria, por si só, uma rede escalável.</p><a href="{source('docs/NETWORKING-RESEARCH.md')}">Pesquisa de rede ↗</a></div></details>'''


def participate():
    steps = [('Escolha um objeto', 'Consulte o catálogo e identifique uma família reutilizável de casas, árvores, pedras, adereços ou personagens.'), ('Reserve o ID', 'Abra uma issue para coordenar o trabalho. Aguarde a confirmação do mantenedor antes de modelar; abrir a issue ainda não confirma a reserva.'), ('Modele e compare', 'Mantenha a composição na câmera original. Complete as faces e confira rotações, escala e contato com o chão.'), ('Envie para revisão', 'Inclua capturas, origem dos recursos, licença e arquivos permitidos. Um candidato passa por revisão antes de ser aceito.')]
    return intro('PARTICIPAR', 'Construa Diablo 3D<br>com a gente.', 'O objetivo reúne todos os níveis de Diablo 1. As contribuições atuais começam pelos objetos de Tristram e pela base técnica: arte, código, testes e documentação.') + f'''
<div class="actions">{button('Entrar no Discord', DISCORD)}{button('Ver o catálogo', source('docs/ASSET-CATALOG.md', False), True)}</div>
<section>{section_head('Uma contribuição por vez.')}<ol class="steps">{''.join(f'<li class="step"><span class="step-number">{i+1:02d}</span><h3>{t}</h3><p>{d}</p></li>' for i,(t,d) in enumerate(steps))}</ol></section>
<section class="split"><div class="panel prose"><h2>Arte e modelagem</h2><p>Comece por uma família de objetos completos, não por um fragmento de tile. Compare silhueta, escala, portas, janelas, orientação e encaixe no terreno.</p><p>O catálogo registra 12 objetos arquitetônicos completos em nove famílias, 93 árvores em seis famílias e 501 grupos de pedras em seis padrões na auditoria v4. Essas contagens descrevem a reconstrução atual; não são modelos de colaboradores já aprovados.</p><p>O registro publicado em 07/10 ainda tem zero modelos manualmente autorados aceitos.</p></div><div class="panel prose"><h2>Código, testes e documentação</h2><p>Ajude na renderização, importação, comparação e revisão de guias. Um relato útil inclui versão, enquadramento, reprodução do problema e captura contextual.</p><p>Há espaço para testar seleção, oclusão, caminhada e integração do renderer sem mudar a lógica da partida.</p>{button('Abrir uma reserva de recurso', GITHUB + '/issues/new?template=3d_asset.yml', True)}</div></section>
<details class="panel"><summary>Checklist para enviar um modelo</summary><div class="prose"><ul><li>Referência e procedência documentadas.</li><li>Mesma composição, escala e silhueta na câmera original.</li><li>Objeto completo, com faces e contato com o chão revisados.</li><li>Capturas em ±5° e 0°, 90°, 180°, 270°.</li><li>Sem arquivos proprietários extraídos, saves ou credenciais.</li></ul><p>Não envie MPQ, CEL/CL2/MIN/TIL/SOL extraídos, texturas isoladas do jogo ou modelos derivados desses arquivos para o site.</p></div></details>
<section class="contribute-banner chapter-band atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap"><div><p class="eyebrow">OUTRA FORMA DE PARTICIPAR</p><h2>Ajude a sustentar o desenvolvimento.</h2><p>Apoio financeiro e ajuda com Meshy, outros geradores 3D, ChatGPT e Claude contribuem para as revisões e a continuidade do projeto.</p></div>{button('Ver formas de apoio', '/apoiar/')}</div>{art_credit()}</section>
<aside class="notice"><h2>Antes de contribuir</h2><p>Leia o <a href="{source('docs/CONTRIBUTING.md', False)}">guia de contribuição</a> e a <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>. A distribuição deve ser gratuita e não comercial. Arte independente precisa de origem e permissões compatíveis.</p></aside>'''


def roadmap():
    milestones = [('EM DESENVOLVIMENTO', 'Tristram', 'Calibrar objetos completos e coerentes em 360°, personagens, materiais, contato com o terreno e comparação com a perspectiva original. A cabana leste opcional de revisão já possui duas janelas abertas, piso físico e velas com oscilação sutil. A cidade e os demais objetos continuam em revisão.'), ('PRÓXIMO MARCO', 'Primeiro nível procedural da Catedral', 'Construir geometria 3D a partir do mapa vivo e da semente da partida. Esse trabalho sucede a calibração da cidade; hoje as masmorras usam o renderer original.'), ('OBJETIVO COMPLETO · PLANEJADO', 'Todos os níveis de Diablo 1', 'Reconstruir os demais níveis da Catedral, as Catacumbas, as Cavernas e o Inferno, com os inimigos, objetos e conteúdo do jogo. Cada ambiente precisará de implementação e comparação próprias; nenhum desses níveis é apresentado como concluído.'), ('PLANEJAMENTO', 'Ambiente e alcance do mapa', 'Ciclo dia/noite, horizonte e fog vêm depois de Tristram. A expansão lateral dos mapas permanece uma questão aberta.'), ('PESQUISA', 'Rede e voz por proximidade', 'Restaurar e validar a rede existente em um experimento separado, antes de investigar mais jogadores e voz. O build atual permanece offline.')]
    return intro('ROADMAP', 'Diablo inteiro.<br>Um marco de cada vez.', 'Todos os níveis do jogo fazem parte do objetivo. Tristram é a etapa atual; o roadmap organiza os próximos marcos sem fixar prazos de conclusão.') + f'''
<aside class="notice"><p class="eyebrow">ESTADO DOCUMENTADO · 07 OUTUBRO 2026</p><p><kbd>F4</kbd> alterna a visão · <kbd>Home</kbd> usa o backend original · build offline.</p><p>A última etapa desta retrospectiva é a <a href="{GITHUB}/commit/{SNAPSHOT}">revisão das velas e das aberturas da cabana</a>. As capturas e limites ficam registrados no <a href="{url('/devlog/')}">devlog</a>.</p></aside>
<section class="timeline">{''.join(f'<article class="milestone panel"><span class="tag">{badge}</span><h2>{title}</h2><p>{body}</p></article>' for badge,title,body in milestones)}</section>
<section class="panel prose"><h2>O que precisa melhorar agora</h2><p>Silhuetas e faces ocultas da arquitetura, materiais, personagens e oclusão. Geração automática auxilia o processo, mas cada objeto precisa de revisão humana e comparação real.</p><p>As diferenças observadas ficam registradas junto das capturas. A igualdade de pixels na rota Home não substitui a revisão da malha.</p><a href="{source('docs/ROADMAP.md', False)}">Consultar o roadmap versionado no GitHub ↗</a></section>'''


def gallery(evidence):
    items = []
    for entry in evidence:
        items.append(f'''<figure class="gallery-item" data-filter-item data-category="{esc(entry['category'])}" data-search="{esc(entry['title'] + ' ' + entry['caption'])}"><a href="{url(entry['file'])}" data-lightbox data-caption="{esc(entry['title'] + ' — ' + entry['caption'])}">{img(entry['file'], entry['alt'])}<span class="image-open" aria-hidden="true">Ampliar ↗</span></a><figcaption><span class="tag">{esc(entry['stage'])}</span><h2>{esc(entry['title'])}</h2><p>{esc(entry['caption'])}</p></figcaption></figure>''')
    return intro('GALERIA', 'Do primeiro protótipo<br>ao jogo completo.', 'O destino é todo Diablo 1 em 3D. As evidências atuais mostram Tristram: referências do original, comparações e giros de revisão, com as diferenças preservadas.') + '<p class="notice">As etapas v1 a v4 são registros locais retrospectivos publicados aqui em 07/10/2026. Novos níveis serão documentados quando implementados. As ilustrações de ambientação e os conceitos de interface ficam separados das capturas reais abaixo.</p>' + filters(sorted(set(e['category'] for e in evidence))) + f'<section class="gallery-grid">{"".join(items)}</section>' + no_results() + f'<p class="meta">{len(evidence)} {translated('capturas selecionadas.')} <a href="{url("/evidence.json")}">Inventário público e proveniência</a>.</p>'


def support():
    return intro('APOIAR O DESENVOLVIMENTO', 'Ajude a construir<br>Diablo inteiro em 3D.', 'Modelos, revisões e ferramentas exigem investimento. Seu apoio ajuda a manter o desenvolvimento, avançar até a conclusão e explorar novas possibilidades. A etapa atual é Tristram; o objetivo inclui todos os níveis.') + f'''
<div class="actions">{button('Conversar no Discord', DISCORD)}{button('Ver formas de apoio', '#formas-de-apoio', True)}</div>
<section id="formas-de-apoio">{section_head('Escolha como apoiar.', 'O contato direto é o caminho preferido para combinar a contribuição.')}<div class="support-grid">
<article class="support-card panel"><p class="eyebrow">APOIO FINANCEIRO · CONTATO DIRETO</p><h2>Vamos combinar<br>a melhor forma.</h2><p>Para apoiar em dinheiro, fale com <strong class="contact-handle">douglasopan</strong> no Discord. Podemos combinar uma forma adequada ao seu país e ao valor da contribuição.</p><div class="actions">{button('Entrar no Discord oficial', DISCORD)}</div><p class="meta">No servidor, procure por <strong>douglasopan</strong>.</p><div class="payment-identity"><span>Destinatário informado para PayPal</span><strong>douglas8pan@gmail.com</strong></div><div class="actions"><button type="button" class="button secondary" data-copy="douglas8pan@gmail.com" hidden data-enhancement>Copiar e-mail PayPal</button>{button('Abrir PayPal', 'https://www.paypal.com/', True)}</div><p id="copy-status" class="meta" aria-live="polite"></p><p>Se preferir usar o PayPal diretamente, copie o e-mail acima e confira o destinatário na plataforma antes de confirmar o envio.</p><p class="meta">Para apoio dentro do Brasil, também podemos combinar o envio pelo Asaas ou outra forma disponível por contato. Um link de pagamento será compartilhado quando definido.</p></article>
<article class="support-card panel"><p class="eyebrow">CRÉDITOS E FERRAMENTAS</p><h2>Ajude a transformar<br>ideias em recursos.</h2><p>Aceitamos ajuda com créditos e uso de ferramentas que apoiam a criação de modelos, o código e a documentação.</p><div class="credit-list"><span>Meshy</span><span>Outros geradores 3D</span><span>ChatGPT</span><span>Claude</span></div><p>Converse antes para combinar uma forma compatível com a plataforma: créditos, uma assinatura oferecida ou ajuda na geração de um recurso. A disponibilidade varia entre os serviços.</p>{button('Combinar apoio em créditos', DISCORD, True)}<p class="meta">Não é necessário compartilhar senha nem chave de API.</p></article>
</div><p class="notice">O apoio é voluntário. O contato direto permite combinar a forma de envio; PayPal, Asaas e outros meios podem cobrar tarifas de processamento ou conversão conforme o método e o país. Consulte as condições do serviço antes de confirmar. <a href="https://www.paypal.com/br/business/paypal-business-fees">Tarifas PayPal ↗</a> · <a href="https://www.asaas.com/link-pagamento">Links de pagamento Asaas ↗</a></p></section>
<section class="section-atmosphere atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap">{section_head('O que o apoio torna possível.')}<div class="support-uses"><article class="panel"><p class="eyebrow">01 · CRIAÇÃO</p><h3>Modelos e revisões</h3><p>Gerar candidatos, completar faces, ajustar materiais e comparar cada recurso com o original.</p></article><article class="panel"><p class="eyebrow">02 · FERRAMENTAS</p><h3>Desenvolvimento</h3><p>Custear ferramentas de geração, assistência ao código, documentação e testes.</p></article><article class="panel"><p class="eyebrow">03 · CONTINUIDADE</p><h3>Conclusão e expansão</h3><p>Dar continuidade ao trabalho da cidade aos níveis do jogo e avaliar possibilidades futuras.</p></article></div></div>{art_credit()}</section>
<section class="support-faq">{section_head('Apoio com clareza.')}<details class="panel"><summary>O apoio é voluntário?</summary><div class="prose"><p>Sim. Ele ajuda a sustentar o desenvolvimento e não compra acesso exclusivo ao jogo. O código permanece público sob a <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>, com distribuição gratuita e não comercial.</p></div></details><details class="panel"><summary>Onde acompanho o progresso?</summary><div class="prose"><p>As mudanças publicadas, capturas e verificações aparecem no <a href="{url('/devlog/')}">devlog</a>. O <a href="{url('/roadmap/')}">roadmap</a> mostra o estado atual e os marcos planejados. Não há data prometida de conclusão.</p></div></details><details class="panel"><summary>Posso ajudar sem dinheiro ou créditos?</summary><div class="prose"><p>Sim. Modelagem, programação, capturas de teste e documentação também fazem o projeto avançar. Veja <a href="{url('/participar/')}">como participar</a>.</p></div></details></section>'''


def write(path, text):
    target = OUT / localized_path(path).lstrip('/')
    if path.endswith('/'):
        target /= 'index.html'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding='utf-8', newline='\n')


def build_locale(posts, evidence):
    pages = [
        ('/', 'Diablo 1 sob uma nova dimensão.', 'Diablo 3D: um projeto para reconstruir todo Diablo 1 em 3D, com todos os níveis. Etapa atual: Tristram. Devlog, capturas reais e formas de apoiar.', home(posts)),
        ('/devlog/', 'Diário de construção', 'Acompanhe o desenvolvimento do jogo completo em Diablo 3D. Registros atuais de Tristram, capturas reais, decisões e verificações.', intro('DEVLOG', 'Diário de construção.', 'Do primeiro protótipo ao objetivo de reconstruir todo Diablo 1 em 3D. O que mudou, como foi testado e o que ainda precisa de revisão, com capturas reais e fontes versionadas.') + '<p class="notice">Retrospectiva publicada em 07/10/2026. A data de cada post é a publicação deste registro; v1–v4 representam etapas locais anteriores à primeira publicação consolidada.</p>' + filters(CATEGORIES, True) + '<h2 class="sr-only">Registros publicados</h2><section class="card-grid devlog-feed">' + ''.join(card(p) for p in posts) + '</section>' + no_results()),
        ('/projeto/', 'Sobre o projeto', 'Conheça o D3D: projeto independente para reconstruir todo Diablo 1 em 3D, com todos os níveis, preservando a partida. Tristram é a etapa atual.', project()),
        ('/tecnologia/', 'Tecnologia do protótipo', 'DevilutionX e C++, rasterização na CPU, paleta, geometria agrupada, Python, auditoria headless e revisão opcional com Meshy.', technology()),
        ('/participar/', 'Participe do Diablo 3D', 'Contribua com modelos, código, testes e documentação. Reserve um objeto, compare com o original e envie para revisão.', participate()),
        ('/apoiar/', 'Apoie o desenvolvimento', 'Ajude a desenvolver todo Diablo 1 em 3D. Apoio financeiro por contato direto e PayPal, ou ajuda com Meshy, outros geradores 3D, ChatGPT e Claude.', support()),
        ('/roadmap/', 'Roadmap do Diablo 3D', 'Todos os níveis de Diablo 1 em 3D: Tristram, Catedral, Catacumbas, Cavernas e Inferno. Etapa atual, próximos marcos e pesquisas futuras.', roadmap()),
        ('/galeria/', 'Galeria do desenvolvimento', 'Capturas reais de cada etapa do protótipo Diablo 3D, incluindo comparações da cabana e ângulos de revisão.', gallery(evidence)),
    ]
    for path, title, description, content in pages:
        write(path, frame(title, description, path, content))
    for post in posts:
        content = f'''<header class="page-intro article-intro atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp', True)}<div class="intro-copy"><p class="eyebrow">{esc(translated(post['category']))} · DEVLOG</p><h1>{esc(post['title'])}</h1><p class="lede">{esc(post['description'])}</p><div class="meta"><time datetime="{post['date']}">{date_label(post['date'])}</time><span>Projeto D3D</span><span>{post['minutes']} {translated('min de leitura')}</span></div></div>{art_credit()}</header><nav class="breadcrumb" aria-label="Você está aqui"><a href="{url('/devlog/')}">Devlog</a><span aria-hidden="true"> / </span><span>{esc(post['category'])}</span></nav><div class="article-layout"><article class="prose"><figure>{img(post['image'], post['image_alt'], True)}<figcaption>{esc(post['image_alt'])}</figcaption></figure>{post['body']}<aside class="notice"><p>Este registro é versionado no GitHub. <a href="{source(post['source_file'], False)}">Ver fonte e histórico ↗</a></p></aside></article><aside class="toc"><details open><summary>Neste registro</summary>{post['toc']}</details><div class="notice"><p>Home usa o backend original. Essa rota não valida a malha.</p></div></aside></div><section>{section_head('Continue acompanhando.')}<div class="card-grid">{''.join(card(p) for p in [p for p in posts if p is not post][:3])}</div></section>'''
        write(post['path'], frame(post['title'], post['description'], post['path'], content, post['image'], post))
    write('/404.html', frame('Página não encontrada', 'Encontre o devlog, as capturas e as informações do projeto Diablo 3D.', '/404.html', intro('404', 'Esse caminho ainda<br>não foi construído.', 'Você pode continuar pelo diário do projeto ou voltar ao início.') + button('Voltar ao início', '/'), noindex=True))
    atom = 'http://www.w3.org/2005/Atom'
    ET.register_namespace('atom', atom)
    rss = ET.Element('rss', version='2.0')
    channel = ET.SubElement(rss, 'channel')
    for name, value in [('title', 'Diablo 3D — Diário de construção'), ('link', absolute('/devlog/')), ('description', 'Etapas reais de um projeto para reconstruir todo Diablo 1 em 3D. Tristram é o marco atual.'), ('language', LANG)]:
        ET.SubElement(channel, name).text = translated(value)
    ET.SubElement(channel, f'{{{atom}}}link', href=absolute('/rss.xml'), rel='self', type='application/rss+xml')
    dc = 'http://purl.org/dc/elements/1.1/'
    ET.register_namespace('dc', dc)
    for post in posts:
        item = ET.SubElement(channel, 'item')
        for name, value in [('title', post['title']), ('link', absolute(post['path'])), ('description', post['description']), ('category', translated(post['category']))]:
            ET.SubElement(item, name).text = value
        ET.SubElement(item, f'{{{dc}}}date').text = post['date']
        ET.SubElement(item, 'guid', isPermaLink='true').text = absolute(post['path'])
    write('/rss.xml', ET.tostring(rss, encoding='unicode', xml_declaration=True))
    return [localized_path(p[0]) for p in pages] + [localized_path(p['path']) for p in posts]


def build():
    global LANG
    LANG = 'pt-BR'
    posts = load_posts()
    raw_evidence = json.loads((ROOT / 'evidence.json').read_text(encoding='utf-8'))
    evidence = raw_evidence['entries'] if isinstance(raw_evidence, dict) else raw_evidence
    for entry in evidence:
        image_info(entry['file'])
        if not all(entry.get(k) for k in ('title', 'alt', 'caption', 'category', 'stage', 'source', 'sha256')):
            raise ValueError('Incomplete evidence metadata')
    allowed = {'.css', '.js', '.png', '.webp', '.svg', '.woff', '.woff2'}
    for path in PUBLIC.rglob('*'):
        if path.is_symlink() or (path.is_file() and path.suffix.lower() not in allowed and not (path.parent == PUBLIC / 'assets/fonts' and path.suffix == '.txt')):
            raise ValueError(f'Unexpected public file: {path.relative_to(PUBLIC)}')
    # Output deletion is deliberately constrained to this generator's own directory.
    if OUT.resolve().parent != ROOT.resolve() or OUT.name != 'dist':
        raise ValueError('Output path escaped website directory')
    if OUT.exists():
        shutil.rmtree(OUT)
    shutil.copytree(PUBLIC, OUT)
    write('/.nojekyll', '')
    write('/evidence.json', json.dumps(raw_evidence, ensure_ascii=False, indent=2) + '\n')
    paths = []
    for language in ('pt-BR', 'en'):
        LANG = language
        posts = load_posts()
        localized_evidence = evidence
        if language == 'en':
            if set(EVIDENCE_EN) != {entry['file'] for entry in evidence}:
                raise ValueError('English gallery translations must cover every capture')
            localized_evidence = [dict(entry, **{key: EVIDENCE_EN[entry['file']][key] for key in ('title', 'alt', 'caption')}) for entry in evidence]
            public_entries = [dict(entry, category=translated(entry['category']), stage=translated(entry['stage'])) for entry in localized_evidence]
            public_inventory = dict(raw_evidence, entries=public_entries) if isinstance(raw_evidence, dict) else public_entries
            write('/evidence.json', json.dumps(public_inventory, ensure_ascii=False, indent=2) + '\n')
        paths.extend(build_locale(posts, localized_evidence))
    LANG = 'pt-BR'
    namespace = 'http://www.sitemaps.org/schemas/sitemap/0.9'
    xhtml = 'http://www.w3.org/1999/xhtml'
    ET.register_namespace('', namespace)
    ET.register_namespace('xhtml', xhtml)
    sitemap = ET.Element(f'{{{namespace}}}urlset')
    for path in paths:
        entry = ET.SubElement(sitemap, f'{{{namespace}}}url')
        ET.SubElement(entry, f'{{{namespace}}}loc').text = BASE + path
        original_path = path[3:] if path.startswith('/en/') else path
        for language in ('pt-BR', 'en', 'x-default'):
            ET.SubElement(entry, f'{{{xhtml}}}link', rel='alternate', hreflang=language, href=alternate_url(original_path, language))
    write('/sitemap.xml', ET.tostring(sitemap, encoding='unicode', xml_declaration=True))
    write('/robots.txt', f'User-agent: *\nAllow: /\nSitemap: {absolute("/sitemap.xml")}\n')
    print(f'Built {len(paths)} bilingual pages, {len(posts)} articles per language and {len(evidence)} curated captures -> website/dist')


if __name__ == '__main__':
    build()
