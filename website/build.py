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
from soundtrack import load_soundtrack
from music_credits import SOURCE as MUSIC_CREDIT_SOURCE, album_metadata, credit_text, public_soundtrack
from hud_study import copy_study, gallery as hud_study_gallery, image_source as hud_study_image_source, load_study
from community import community_invite, YOUTUBE_URL
from videos import DEVLOG_SUBTITLES, DEVLOG_SUBTITLES_SHA256, all_videos_section, devlog_section, showcase_section, videos_section
from social import icon as social_icon, LICENSE as SOCIAL_ICON_LICENSE

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
PUBLIC = ROOT / 'public'
OUT = ROOT / 'dist'
BASE = os.environ.get('SITE_URL', 'https://douglasopan.github.io/diablo-3d').rstrip('/')
parsed_base = urlsplit(BASE)
if parsed_base.scheme not in ('http', 'https') or not parsed_base.netloc or parsed_base.query or parsed_base.fragment:
    raise ValueError('SITE_URL must be an absolute HTTP(S) URL without a query or fragment')
PREFIX = parsed_base.path.rstrip('/')
SITE_NAME = 'Diablo3D'
SITE_ALIASES = ['Diablo 3D', 'D3D']
GITHUB = 'https://github.com/douglasopan/diablo-3d'
SNAPSHOT = '7e4a6c3b3796a98f371b6a4817ad259c1bf91467'
ROADMAP_SNAPSHOT = 'a1ceed7e6adfdeb1c3e4b65e559ba50929198914'
DISCORD = 'https://discord.gg/4YxQ7s69S'
REDDIT = 'https://www.reddit.com/r/Diablo3D/'
PATREON = 'https://patreon.com/u107414'
PANVIA = 'https://panvia.online/jogos/thepan-art/diablo-3d/'
SOUNDTRACK_YOUTUBE_PLAYLIST = 'https://www.youtube.com/playlist?list=PLPiXmw2nj9BM'
SOUNDTRACK_YOUTUBE = {
    'menu-rock2': 'https://www.youtube.com/watch?v=KWlri7ryuR0',
    'menu-alternative': 'https://www.youtube.com/watch?v=YcaFpTO9C6Y',
    'town-rock': 'https://www.youtube.com/watch?v=t9dujaCADMg',
    'town-alternative': 'https://www.youtube.com/watch?v=m59pZHxSQ_w',
    'town-third': 'https://www.youtube.com/watch?v=TkMiffWTzPA',
}
NAV = [('Início', '/'), ('O jogo', '/projeto/'), ('Galeria', '/galeria/'), ('Diário', '/devlog/'), ('Música', '/musica/'), ('Apoiar', '/apoiar/')]
CATEGORIES = ('Protótipo', 'Geometria', 'Personagens', 'Comunidade', 'Ferramentas', 'Luz')
MONTHS = ('janeiro', 'fevereiro', 'março', 'abril', 'maio', 'junho', 'julho', 'agosto', 'setembro', 'outubro', 'novembro', 'dezembro')
EN_MONTHS = ('January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December')
LANG = 'pt-BR'


def localized_path(path, language=None):
    language = language or LANG
    routes = ('/download/', '/devlog/', '/musica/', '/novidades/', '/projeto/', '/tecnologia/', '/participar/', '/apoiar/', '/roadmap/', '/galeria/')
    route = urlsplit(path).path
    if language == 'en' and (route in ('/', '/rss.xml', '/404.html', '/evidence.json', '/soundtrack.json') or route.startswith(routes)):
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
    file = hud_study_image_source(path) or PUBLIC / path.lstrip('/')
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
        marker = '<div data-study-gallery="hud-r2"></div>'
        if marker in data['body']:
            if slug != 'interfaces-diablo-r2':
                raise ValueError('The R2 gallery belongs to its identified study article')
            data['body'] = data['body'].replace(marker, hud_study_gallery(language, url))
        showcase_marker = '<div data-model-showcase="public"></div>'
        if showcase_marker in data['body']:
            if slug != 'interfaces-diablo-r2':
                raise ValueError('The public model showcase belongs to its identified article')
            data['body'] = data['body'].replace(showcase_marker, showcase_section(language, url))
        devlog_marker = '<div data-devlog-video="final-20261009"></div>'
        if devlog_marker in data['body']:
            if slug != 'devlog-em-video':
                raise ValueError('The final devlog video belongs to its identified article')
            data['body'] = data['body'].replace(devlog_marker, devlog_section(language, url))
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
    return '<p class="art-credit">Ilustração de ambientação gerada com IA</p>'


def intro(eyebrow, title, description):
    art = '/assets/art/journal-atmosphere.webp' if eyebrow in ('DEVLOG', 'TECNOLOGIA') else '/assets/art/tristram-atmosphere.webp'
    return f'<header class="page-intro atmospheric bleed">{scene(art, True)}<div class="intro-copy"><p class="eyebrow">{eyebrow}</p><h1>{title}</h1><p class="lede">{description}</p></div>{art_credit()}</header>'


def filters(categories, search=False):
    search_html = '<div class="search-field"><label class="sr-only" for="search">Buscar registros do devlog</label><input id="search" type="search" placeholder="Buscar no diário…" autocomplete="off"></div>' if search else ''
    return '<div class="filter-bar" hidden data-enhancement><div class="filter-buttons" role="group" aria-label="Filtrar por tema">' + ''.join(f'<button type="button" data-filter="{esc(c)}" aria-pressed="{str(c == "Todos").lower()}">{esc(c)}</button>' for c in ['Todos', *categories]) + f'</div>{search_html}</div><p id="result-count" class="meta" aria-live="polite"></p>'


def no_results():
    return '<div id="no-results" class="panel empty-state" hidden><h2>Nenhum registro encontrado.</h2><p>Tente outro termo ou veja todas as etapas.</p><button id="reset-filters" class="button secondary" type="button">Limpar filtros</button></div>'


def devlog_highlight():
    return f'''
<section class="panel prose"><h2>Diablo 1 em primeira pessoa: nova prévia</h2><p>A R3 local de 12 de outubro reúne caixão animado, câmera, HUD e controles sobre a simulação nativa. Testes delimitados passaram; andares posteriores e gameplay completo continuam em revisão.</p><div class="actions">{button('Ler o novo devlog', '/devlog/primeira-pessoa-previa-nativa-catedral/', True)}{button('Download e testes na Panvia', PANVIA, True)}</div></section>'''


def lightbox():
    return '''<dialog id="image-dialog" aria-label="Visualizador de imagens" aria-describedby="dialog-caption"><div class="dialog-toolbar"><span id="dialog-counter"></span><button type="button" class="dialog-close" aria-label="Fechar imagem">Fechar ×</button></div><img id="dialog-image" alt=""><p id="dialog-caption"></p><div class="dialog-controls"><button type="button" id="dialog-prev" aria-label="Imagem anterior">← Anterior</button><button type="button" id="dialog-next" aria-label="Próxima imagem">Próxima →</button></div></dialog>'''


def background_music():
    return f'''<aside id="music-player" class="music-player" aria-label="Música de fundo"><div class="music-heading"><span>Música de fundo</span><span id="music-status" role="status">Toque para ouvir</span><button id="music-close" class="music-close" type="button" aria-label="Fechar música de fundo" title="Fechar música de fundo" hidden>×</button></div><audio id="background-music" controls loop preload="none" playsinline aria-label="Reproduzir música de fundo" data-icon-url="{url('/vendor/plyr/plyr.svg')}" src="{versioned_asset('/assets/audio/background-music.mp3')}">Seu navegador não oferece reprodução de áudio.</audio><a class="music-credit" href="{url('/musica/#music-credits')}" title="{esc(credit_text(LANG))}">Matt Uelmen · regravação Douglas Pan (IA)</a></aside><button id="music-open" class="music-open" type="button" aria-label="Abrir música de fundo" aria-controls="music-player" aria-expanded="false" title="Abrir música de fundo" hidden><span aria-hidden="true">♪</span></button>'''


def load_analytics():
    config = json.loads((ROOT / 'analytics.json').read_text(encoding='utf-8'))
    measurement_id = config.get('measurement_id')
    if not isinstance(measurement_id, str) or (measurement_id and not re.fullmatch(r'G-[A-Z0-9]{10}', measurement_id)):
        raise ValueError('analytics.json measurement_id must be empty or a GA4 G-XXXXXXXXXX ID')
    return {'measurement_id': measurement_id}


def analytics_tag():
    measurement_id = load_analytics()['measurement_id']
    if not measurement_id:
        return ''
    return f'''<!-- Google tag (gtag.js) -->
<script async src="https://www.googletagmanager.com/gtag/js?id={measurement_id}"></script>
<script>
  window.dataLayer = window.dataLayer || [];
  function gtag(){{dataLayer.push(arguments);}}
  gtag('js', new Date());

  gtag('config', '{measurement_id}');
</script>'''


def frame(title, description, path, content, image='/assets/banner.webp', article=None, noindex=False, music=None):
    title, description = translated(title), translated(description)
    document_title = f'{SITE_NAME} · {title}' if path == '/' else f'{title} · {SITE_NAME}'
    canonical = absolute(path)
    breadcrumb = [{'@type': 'ListItem', 'position': 1, 'name': translated('Início'), 'item': absolute('/')}]
    if path != '/':
        if article:
            breadcrumb.append({'@type': 'ListItem', 'position': 2, 'name': 'Devlog', 'item': absolute('/devlog/')})
        breadcrumb.append({'@type': 'ListItem', 'position': len(breadcrumb) + 1, 'name': title, 'item': canonical})
    structured = [
        {'@context': 'https://schema.org', '@type': 'WebSite', '@id': absolute('/#website'), 'url': absolute('/'), 'name': SITE_NAME, 'alternateName': SITE_ALIASES, 'inLanguage': LANG, 'description': translated('Diablo3D é um mod de Diablo 1 sobre DevilutionX com câmeras de primeira e terceira pessoa. Acompanhe Tristram, Catedral, modelos e versões na Panvia.')},
        {'@context': 'https://schema.org', '@type': 'BreadcrumbList', 'itemListElement': breadcrumb},
        {'@context': 'https://schema.org', '@type': 'BlogPosting' if article else 'WebPage', 'headline' if article else 'name': title, 'description': description, 'url': canonical, 'inLanguage': LANG, 'isPartOf': {'@id': absolute('/#website')}, **({'datePublished': article['date'], 'dateModified': article.get('updated', article['date']), 'image': absolute(image), 'author': {'@type': 'Organization', 'name': translated('Projeto D3D'), 'url': absolute('/projeto/')}, 'mainEntityOfPage': canonical} if article else {})}
    ]
    if music:
        structured.append(album_metadata(music, LANG, canonical, absolute))
    nav = ''.join(f'<a href="{url(p)}"{" aria-current=" + chr(34) + "page" + chr(34) if (p == path or (p == "/devlog/" and article)) else ""}>{name}</a>' for name, p in NAV)
    socials = [("discord", "Discord", DISCORD), ("reddit", "Reddit — comunidade Diablo 3D", REDDIT), ("github", "GitHub", GITHUB)]
    social_links = ''.join(f'<a class="social-link social-{name}" href="{href}" aria-label="{label}" title="{label}">{social_icon(name)}</a>' for name, label, href in socials)
    nav += f'<a class="desktop-discord" href="{DISCORD}">{social_icon("discord")}<span>Discord</span></a>'
    mobile_socials = f'<nav class="header-socials" aria-label="Comunidades do projeto">{social_links}</nav>'
    soundtrack_script = f'<script src="{versioned_asset("/soundtrack.js")}" defer></script>' if music else ''
    community_assets = f'<link rel="stylesheet" href="{versioned_asset("/community.css")}"><script src="{versioned_asset("/community.js")}" defer></script>'
    video_assets = f'<link rel="stylesheet" href="{versioned_asset("/youtube-videos.css")}"><script src="{versioned_asset("/youtube-videos.js")}" defer></script>' if path in ('/', '/musica/', '/galeria/', '/devlog/devlog-em-video/', '/devlog/interfaces-diablo-r2/') else ''
    article_meta = f'<meta property="article:published_time" content="{article["date"]}"><meta property="article:modified_time" content="{article.get("updated", article["date"])}">' if article else ''
    languages = '<nav class="language-switch" aria-label="Idioma">' + ''.join(f'<a href="{urlsplit(alternate_url(path, lang)).path}" hreflang="{lang}" lang="{lang}" data-language-link="{lang}" aria-label="{label}"' + (' aria-current="true"' if LANG == lang else '') + f'>{short}</a>' for lang, label, short in [('pt-BR', 'Ler em português', 'PT'), ('en', 'Read in English', 'EN')]) + '</nav>'
    alternates = ''.join(f'<link rel="alternate" hreflang="{lang}" href="{alternate_url(path, lang)}">' for lang in ('pt-BR', 'en', 'x-default'))
    document = f'''<!doctype html>
<html lang="{LANG}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><!-- {SOCIAL_ICON_LICENSE} -->{analytics_tag()}
<title>{esc(document_title)}</title><meta name="description" content="{esc(description)}"><meta name="robots" content="{'noindex, follow' if noindex else 'index, follow'}"><link rel="canonical" href="{canonical}">
<meta name="theme-color" content="#111211"><meta property="og:locale" content="{'en_US' if LANG == 'en' else 'pt_BR'}"><meta property="og:type" content="{'article' if article else 'website'}"><meta property="og:site_name" content="{SITE_NAME}"><meta property="og:title" content="{esc(document_title)}"><meta property="og:description" content="{esc(description)}"><meta property="og:url" content="{canonical}"><meta property="og:image" content="{absolute(image)}"><meta property="og:image:alt" content="{esc(article['image_alt'] if article else 'Banner oficial do projeto Diablo 3D')}">{article_meta}
<meta name="twitter:card" content="summary_large_image"><meta name="twitter:title" content="{esc(document_title)}"><meta name="twitter:description" content="{esc(description)}"><meta name="twitter:image" content="{absolute(image)}">
<link rel="icon" type="image/png" href="{url('/assets/d3d-icon.png')}"><link rel="apple-touch-icon" href="{url('/assets/d3d-touch.png')}"><link rel="alternate" type="application/rss+xml" title="Diablo 3D — Devlog" href="{url('/rss.xml')}"><link rel="stylesheet" href="{versioned_asset('/vendor/plyr/plyr.css')}"><link rel="stylesheet" href="{versioned_asset('/site.css')}">
{alternates}<script src="{versioned_asset('/language.js')}"></script><script type="application/ld+json">{json.dumps(structured, ensure_ascii=False).replace('<', chr(92) + 'u003c')}</script><script src="{versioned_asset('/site.js')}" defer></script><script src="{versioned_asset('/vendor/plyr/plyr.js')}" defer></script><script src="{versioned_asset('/music.js')}" defer></script>{soundtrack_script}{community_assets}{video_assets}</head>
<body><a class="skip-link" href="#main">Pular para o conteúdo</a><header class="site-header"><div class="wrap"><a class="brand" href="{url('/')}" aria-label="D3D — Início">{logo(True, True)}</a>{mobile_socials}<button class="menu-toggle" type="button" aria-controls="navigation" aria-expanded="false" hidden data-enhancement>Menu <span aria-hidden="true">☰</span></button><nav id="navigation" class="main-nav" aria-label="Navegação principal">{nav}</nav>{languages}</div></header>
<main id="main" class="wrap">{content}</main><footer class="site-footer"><div class="wrap"><div class="footer-top"><a class="brand" href="{url('/')}">{logo()}</a><nav aria-label="Links do projeto"><a href="{GITHUB}">GitHub ↗</a><a href="{DISCORD}">Discord ↗</a><a href="{YOUTUBE_URL}">YouTube ↗</a><a href="{PANVIA}">Panvia ↗</a><a href="{PATREON}">Patreon ↗</a><button type="button" data-community-open hidden data-enhancement aria-haspopup="dialog" aria-controls="community-invite">Comunidade</button><a href="{url('/musica/')}">Música ↗</a><a href="{url('/apoiar/')}">Apoiar ↗</a><a href="{url('/novidades/')}">Novidades ↗</a><a href="{url('/download/')}">Download · Windows</a><a href="{url('/rss.xml')}">RSS ↗</a><a href="{url('/tecnologia/')}">Tecnologia</a><a href="{url('/roadmap/')}">Roadmap</a><a href="{url('/participar/')}">Participar</a><a href="{url('/projeto/#ia')}">Uso de IA e créditos ↗</a><a href="{source('LICENSE.md', False)}">Licença ↗</a></nav></div><p>Projeto de fã independente, sem afiliação com a Blizzard Entertainment. Diablo e suas marcas pertencem aos respectivos titulares.</p><p>Código público sob <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>: distribuição gratuita e não comercial. Dados originais do jogo não são distribuídos.</p></div></footer>{'' if music else background_music()}{lightbox()}{community_invite(LANG)}</body></html>'''
    return localize_html(document)


def checkpoint_notice():
    return f'''
<section class="notice prose" id="estado"><h2>Prévia local: Diablo 1 em 3D</h2><p class="meta">12 de outubro de 2026 · R3 / 4fe0f467cd56</p><p>A prévia inclui caixão com tampa deslizante que permanece aberta, câmeras de primeira e terceira pessoa, logo animada e ajustes no HUD. Heróis e NPCs seguem em sprites nativos; o pack HD privado DamnationX não faz parte desta prévia.</p><p>231 arquivos conferidos e 111.564 verificações técnicas delimitadas passaram. Isso não comprova toda a campanha, sessão completa com entrada física ou FPS sustentado. O retorno do andar 2 ao 1 ainda tem um defeito registrado; os andares posteriores não estão qualificados.</p><p>Esta é uma entrega local de revisão, separada da instalação habitual b775. O navegador R12 também continua sendo um teste local. As versões disponíveis para download e testes ficam na Panvia.</p><div class="actions">{button('Ler o devlog desta prévia', '/devlog/primeira-pessoa-previa-nativa-catedral/', True)}{button('Download e testes na Panvia', PANVIA, True)}</div></section>'''


def download_page():
    release = json.loads((ROOT / 'releases.json').read_text(encoding='utf-8'))
    en = LANG == 'en'
    title = 'Download Diablo3D' if en else 'Baixe o Diablo3D'
    steps = ('Extract the entire ZIP into a new folder.','Inside the extracted folder, open Iniciar-Diablo3D.cmd to start the game.','Choose DIABDAT.MPQ from your original Diablo installation. This choice is remembered.') if en else ('Extraia todo o conteúdo do ZIP em uma nova pasta.','Dentro da pasta extraída, abra Iniciar-Diablo3D.cmd para iniciar o jogo.','Selecione DIABDAT.MPQ da sua instalação original de Diablo. A escolha fica salva.')
    description = 'Local6 Tristram 162% · Windows x64 · 209 MiB' if en else 'Local6 Tristram 162% · Windows x64 · 209 MiB'
    note = 'Original game data is required and is not included. No account is needed. Saves and settings stay in %LOCALAPPDATA%/Diablo3D-local6-grid162/profile; extract updates into a new folder.' if en else 'Os dados originais do jogo são necessários e não estão incluídos. Não precisa de conta. Saves e configurações ficam em %LOCALAPPDATA%/Diablo3D-local6-grid162/profile; extraia atualizações em uma nova pasta.'
    scope = 'Includes saved Godot edits to seven houses, horizontal native bridges, Warrior Light/Short Sword/Buckler and first/third person. World cells are another 35% larger than local5, reaching 1.62x the original scale. MPQ-only installation and all 19 scene files passed. CPU Warrior, grid/cameras, bridge coverage, classic tavern and well regression tests passed. New GPU/FPS/physical gameplay and relocated building collision remain unqualified; 18 Godot effects and Cathedral input validation remain pending.' if en else 'Inclui sete casas salvas no Godot, pontes nativas horizontais, Warrior Light/Short Sword/Buckler e primeira/terceira pessoa. Mundo mais 35% maior que na local5, totalizando 1,62x a escala original. Instalação MPQ-only e 19 arquivos passaram. Testes CPU de Warrior, escala/câmeras, cobertura das pontes, taverna clássica e poço passaram. Novos GPU/FPS/gameplay físico e colisão de edifícios movidos não qualificados; 18 efeitos Godot e validação de controles da Catedral pendentes.'
    credits = 'Based on DevilutionX, directed by Douglas Pan. Thanks to DamnationX for the pack studied as a reference; their HD pack is not included.' if en else 'Baseado no DevilutionX, com direção de Douglas Pan. Agradecemos ao DamnationX pelo pack estudado como referência; o pack HD deles não está incluído.'
    legacy = f'<header class="page-intro"><p class="eyebrow">LOCAL6 TRISTRAM 162%</p><h1>{title}</h1><p class="lede">{description}</p></header><section class="panel prose"><div class="actions">{button("Download ZIP · Windows x64", release["archive"]["url"])}{button("GitHub Release", release["releaseUrl"], True)}{button("Source code" if en else "Código correspondente", release["sourceArchive"]["url"], True)}</div><ol>' + ''.join(f'<li>{step}</li>' for step in steps) + f'</ol><p>{note}</p><p>SHA256: <code>{release["archive"]["sha256"]}</code></p></section><section class="panel prose"><h2>{"What is included" if en else "O que temos nesta versão"}</h2><p>{scope}</p><p>{credits}</p><p>{"Start this package manually using the included launcher." if en else "Este pacote inicia manualmente pelo atalho incluído."}</p>{button("Panvia", "https://panvia.online/jogos/thepan-art/diablo-3d/", True)}</section>'


    checkpoint = json.loads((ROOT / 'checkpoint-local3.json').read_text(encoding='utf-8'))
    heading = 'Local3: technical checkpoint' if en else 'Local3: checkpoint técnico'
    checkpoint_note = 'Exact executable and source preserved on October 14. This 4.1 MiB technical ZIP is not a complete installer: matching sealed private scene/Warrior runtimes and R17 assets are external requirements; DIABDAT.MPQ alone is insufficient. Performance remains in progress.' if en else 'Executável e fonte preservados em 14 de outubro. Este ZIP técnico de 4,1 MiB não é um instalador completo: exige os runtimes privados selados de cena/Warrior e assets R17 externos; DIABDAT.MPQ sozinho não basta. O desempenho continua em trabalho.'
    checkpoint_link = 'Download technical checkpoint' if en else 'Baixar checkpoint técnico'
    checkpoint_post = 'Read the local3 devlog' if en else 'Ler o devlog da local3'
    checkpoint_header = f'<section class="notice prose"><p class="eyebrow">2026.10.14 · LOCAL3</p><h2>{heading}</h2><p>{checkpoint_note}</p><div class="actions">{button(checkpoint_link, checkpoint["releaseUrl"])}{button(checkpoint_post, "/devlog/checkpoint-local3-warrior-hud-gpu/", True)}</div></section>'
    return legacy + checkpoint_header


def model_gallery():
    pieces = [
        ('warrior-local3-tp', 'O Warrior em 3D', 'O Guerreiro com espada e escudo, em uma prévia de Tristram.', 'Captura automatizada da montagem local3. Acabamento e desempenho em revisão.'),
        ('warrior-local3-equipment-fp', 'Espada e broquel em primeira pessoa', 'Os equipamentos vistos pelos olhos do Guerreiro, no mesmo teste.', 'Captura automatizada da montagem local3. Acabamento e desempenho em revisão.'),
        ('cabin-model', 'A cabana de Tristram', 'Telhados, portas e interiores vistos de novos ângulos.', 'Render de modelo Meshy em desenvolvimento.'),
        ('tavern-model', 'A taverna', 'Um lugar familiar, visto por outro lado.', 'Render de modelo Meshy em desenvolvimento.'),
        ('ogden-model', 'Ogden', 'Ogden e seu avental, em um estudo de modelagem.', 'Render de modelo Meshy em desenvolvimento.'),
        ('adria-model', 'Adria', 'Adria, em um estudo de modelagem.', 'Render de modelo Meshy em desenvolvimento.'),
        ('barrel', 'O barril', 'Pequenos objetos também fazem parte da memória de Diablo.', 'Render do modelo 3D fornecido por Douglas.'),
        ('stone-coffin-closed', 'O caixão de pedra', 'Corpo e tampa separados, para dar movimento à abertura.', 'Render do modelo 3D fornecido por Douglas.'),
    ]
    items = []
    for name, title, caption, origin in pieces:
        title, caption, origin = translated(title), translated(caption), translated(origin)
        path = '/assets/models/' + name + '.webp'
        details = ''
        if name == 'stone-coffin-closed':
            details = f'<details class="model-opening"><summary>Ver a tampa aberta</summary>{img("/assets/models/stone-coffin-open.webp", "Caixão de pedra com a tampa aberta — render do modelo 3D.")}</details>'
        items.append(f'<figure class="model-piece"><a class="model-visual" href="{url(path)}" data-lightbox data-caption="{esc(title + " — " + origin)}" aria-label="{esc(title + " — " + translated("Ampliar imagem"))}">{img(path, title + " — " + origin)}<span class="image-open" aria-hidden="true">Ampliar ↗</span></a><figcaption><h3>{esc(title)}</h3><p>{esc(caption)}</p><p class="meta">{esc(origin)}</p></figcaption>{details}</figure>')
    return '<div class="model-grid">' + ''.join(items) + '</div>'


def home(posts):
    return f'''<section class="hero hero-atmospheric atmospheric bleed">{scene(eager=True)}<div class="hero-copy"><p class="eyebrow">UM NOVO OLHAR PARA UM CLÁSSICO</p>{logo(True, True, 'project-banner')}<h1>Diablo 1 sob uma nova dimensão.</h1><p class="lede">Estamos trazendo Diablo 1 inteiro para o 3D. A ideia é voltar a Tristram, entrar nas masmorras e conhecer esse mundo de perto, em primeira ou terceira pessoa.</p><p class="hero-author meta">Projeto de fã de Douglas Pan, desenvolvido com apoio de IA sobre o DevilutionX.</p><div class="hero-invitations"><a class="button" href="{url('/download/')}" data-download-open aria-haspopup="dialog" aria-controls="download-dialog">Download</a><dialog id="download-dialog" aria-labelledby="download-heading"><button type="button" class="dialog-close" data-download-close aria-label="Fechar">×</button><h2 id="download-heading">Escolha como baixar</h2><div class="download-options"><a class="button" href="https://github.com/douglasopan/diablo-3d/releases">Baixar pelo GitHub Releases</a><a class="button" href="{PANVIA}">Baixar pelo Panvia Launcher <span>(mantém sua versão do jogo atualizada automaticamente)</span></a></div><p class="meta">Extraia todo o conteúdo do ZIP em uma nova pasta. Dentro da pasta extraída, abra Iniciar-Diablo3D.cmd para iniciar o jogo. Quando solicitado, selecione o DIABDAT.MPQ da sua instalação original de Diablo.</p></dialog><a class="button discord-invitation" href="{DISCORD}">{social_icon('discord')} Acompanhe via Discord</a></div><a class="hero-explore" href="#modelos">Veja o que estamos criando <span aria-hidden="true">↓</span></a></div>{art_credit()}</section>
<section class="game-story"><p class="eyebrow">O JOGO QUE VOCÊ CONHECE</p><h2>Você lembra o caminho. Agora pode olhar ao redor.</h2><p class="lede">As casas de Tristram, os corredores da Catedral e os pequenos detalhes estão ganhando volume. Queremos manter a atmosfera do original e abrir outra forma de explorar o mesmo Diablo.</p><p>O trabalho começou em Tristram e no primeiro andar da Catedral. O destino é o jogo inteiro: Catedral, Catacumbas, Cavernas e Inferno.</p>{button('Conheça a proposta', '/projeto/', True)}</section>
<section class="models-showcase" id="modelos" aria-labelledby="modelos-heading"><p class="eyebrow">DO MODELO AO MUNDO</p><h2 id="modelos-heading">Tristram está ganhando forma.</h2><p class="section-lede">Aqui estão algumas das peças que estamos preparando: casas, personagens e objetos que ajudam a dar vida a esse mundo. Cada modelo passa por ajustes antes de entrar no jogo.</p>{model_gallery()}<p class="model-note">Os renders de modelos mostram as peças fora do jogo. Casas e personagens são estudos em desenvolvimento; os vídeos mostram suas vistas em 360°. As capturas do Warrior e de seus equipamentos vêm da montagem local3 de testes. A R17 pública mantém heróis em sprites.</p>{button('Ver mais imagens e vídeos', '/galeria/', True)}</section>
{showcase_section(LANG, url)}
<section class="creator-story"><div><p class="eyebrow">POR TRÁS DO PROJETO</p><h2>Sou Douglas Pan. Quero ver esse mundo de perto.</h2></div><div><p>Estou tocando o Diablo3D: escolho os caminhos, acompanho os modelos, testo as versões e peço os ajustes. Quero compartilhar esse processo com quem também tem vontade de ver Diablo 1 em 3D.</p><p class="meta">Uso IA no código, nos textos, em estudos de arte e nas novas gravações musicais. A base DevilutionX, a arte e a música originais mantêm seus créditos.</p><div class="actions">{button('Veja os bastidores', '/devlog/', True)}{button('Uso de IA e créditos', '/projeto/#ia', True)}</div></div></section>
<section class="section-atmosphere atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap">{section_head('O projeto por dentro.', 'Novidades, imagens e decisões de quem está construindo o jogo.', '/devlog/')}<div class="card-grid">{''.join(card(p) for p in posts[:3])}</div></div>{art_credit()}</section>
<section class="contribute-banner chapter-band atmospheric bleed">{scene()}<div class="atmosphere-content wrap"><div><p class="eyebrow">VENHA ACOMPANHAR</p><h2>Vamos construir o próximo passo juntos.</h2><p>Entre no Discord para conversar, acompanhe as novidades e conte o que gostaria de ver. Se puder apoiar, dinheiro ou créditos em Meshy, outros geradores 3D, ChatGPT e Claude ajudam a manter o projeto andando.</p><p class="meta">O apoio é voluntário. Também precisamos de pessoas para criar, revisar e testar.</p></div><div class="actions">{button('Ajude o projeto a continuar', '/apoiar/')}{button('Entre no Discord', DISCORD, True)}</div></div>{art_credit()}</section>'''


def project():
    return intro('SOBRE O PROJETO', 'Diablo 1 inteiro, em 3D.', 'Diablo3D é um projeto de fã de Douglas Pan, baseado no DevilutionX, para recriar todo Diablo 1 em 3D. Conheça a proposta, acompanhe os modelos e participe do desenvolvimento.') + f'''
<section class="split" aria-label="Direção e proposta"><div class="panel prose"><h2>Douglas Pan dirige o projeto.</h2><p>Douglas define o objetivo, escolhe referências, pede correções e testa versões. Usa IA para escrever código e textos, gerar estudos visuais e produzir recursos. O trabalho novo do Diablo3D não é apresentado como feito manualmente.</p>{button('Como usamos IA', '/projeto/#ia', True)}</div><div class="panel prose"><h2>Diablo 1 em primeira e terceira pessoa</h2><p>Explorar Tristram e as masmorras por dentro, em primeira pessoa, ou acompanhar o herói em terceira pessoa. A simulação do DevilutionX mantém as regras do jogo; F4 permite comparar a cena com a visão original na mesma partida.</p>{button('Controles e limites do protótipo', '/tecnologia/#controles', True)}</div></section>
<section class="panel prose" id="ia"><p class="eyebrow">TRANSPARÊNCIA · USO DE IA</p><h2>IA no código, nos textos e nos recursos.</h2><h3>Código e conteúdo</h3><p>As mudanças de código e as ferramentas do projeto são desenvolvidas com Codex. Os textos e traduções deste site também são produzidos com IA. Douglas declarou o uso de Codex / Sol 6.1. A base DevilutionX e o trabalho de seus colaboradores mantêm seus próprios créditos.</p><h3>Arte e modelos</h3><p>IA gera conceitos, estudos de interface e as ilustrações de fundo deste site. O pipeline Meshy gerou modelos candidatos a partir de referências; eles precisam de correção e aprovação. Partes da cena também são reconstruídas por código a partir da arte original do jogo. Isso não torna essa arte original uma criação nossa.</p><p>Gerar ou importar não significa aceitar um modelo. Silhueta, escala, faces ocultas, portas, materiais e encaixe precisam ser comparados. Os imports Meshy anteriores ficam preservados para revisão; a prévia R3 usa sprites nativos para heróis e NPCs. Conceitos e turntables não são gameplay.</p><h3>Música e narração</h3><p>As novas gravações Rock do menu e de Tristram foram produzidas por Douglas com IA, como reinterpretações das composições de Matt Uelmen para Diablo. Não são composições originais de Douglas. O devlog em vídeo de 9 de outubro usa sua voz sintética autorizada, via ElevenLabs.</p><p>A música é opcional. No jogo, Esc → Configurações → Trilha sonora → Vanilla seleciona as gravações originais. No site, você pode pausar ou silenciar o player de fundo. As gravações originais não são oferecidas para download aqui.</p><div class="actions">{button('Ouvir e consultar os créditos', '/musica/#music-credits', True)}{button('Ver o pipeline de modelos', 'https://github.com/douglasopan/diablo-3d/blob/main/docs/MESHY-WORKFLOW.md', True)}</div><h3>Revisão e responsabilidade</h3><p>Douglas responde pela direção e pelas decisões de publicar. Testes de código, comparações de renderização e testes em jogo ajudam a encontrar defeitos, mas não provam revisão humana completa, fidelidade de toda a arte ou estabilidade. Cada registro identifica a versão, a origem das imagens e o que ainda falta testar.</p></section>
{checkpoint_notice()}
<section class="panel prose"><h2>A proposta e as escolhas</h2><p>O objetivo é todo Diablo 1 em 3D: Tristram, Catedral, Catacumbas, Cavernas e Inferno, com seu conteúdo e níveis procedurais. O primeiro piloto não conclui esse objetivo.</p><p>Primeira pessoa aproxima a exploração; terceira pessoa mantém o personagem visível. Teclas de movimento e uma interface legível ajudam a usar essas câmeras. F4 mantém a referência original acessível. Combate, colisão, mapas, itens e saves continuam sob a simulação nativa.</p><div class="actions">{button('Ver controles atuais e pedidos', '/tecnologia/#controles', True)}{button('Ver o roadmap completo', '/roadmap/', True)}</div></section><figure class="wide-capture">{img("/assets/captures/v4-town.webp", "Vista contextual da cidade de Tristram na etapa v4")}<figcaption>Captura histórica do protótipo v4, de outubro de 2026. Não identifica a build b775 nem comprova o estado atual dos modelos.</figcaption></figure><section class="panel prose"><h2>Como ajudar</h2><p>Precisamos de pessoas para revisar código, corrigir a arte e testar câmeras, combate, saves e transições. Arte autorada por pessoas e contribuições com IA são bem-vindas, com origem e permissões documentadas. Informe a versão, os passos para reproduzir e envie uma captura com contexto.</p><div class="actions">{button('Como contribuir', '/participar/', True)}{button('Entrar no Discord', 'https://discord.gg/4YxQ7s69S', True)}</div></section><aside class="notice prose"><h2>Créditos e direitos</h2><p>Projeto independente de fã, sem afiliação com a Blizzard. Diablo, a arte e as composições originais pertencem aos respectivos titulares. A base é DevilutionX, com seus avisos e créditos preservados. Usar IA não cria permissão para redistribuir recursos proprietários.</p><p>O código herda a Sustainable Use License: distribuição gratuita e não comercial. Não é uma licença MIT/GPL ou aprovada pela OSI. Dados originais do jogo são necessários e não são distribuídos pelo projeto.</p>{button('Ler a licença', 'https://github.com/douglasopan/diablo-3d/blob/main/LICENSE.md', True)}</aside>'''


def technology():
    return intro('TECNOLOGIA', 'Diablo 1 em primeira e terceira pessoa.', 'Câmeras First Person e Third Person no Diablo3D: movimento, controles e interface sobre a simulação do DevilutionX. Veja a prévia local, o que foi testado e como acompanhar o download na Panvia.') + f'''
{checkpoint_notice()}
<section class="panel prose"><h2>Como funciona a primeira pessoa</h2><p>A câmera aproxima a exploração de Tristram e da Catedral. A terceira pessoa acompanha o herói; F4 conserva a comparação com a visão original na mesma partida. O mapa, seed, combate, colisão, itens e saves continuam sob a simulação nativa.</p><p>C++/SDL e GPU Direct3D 11 no Windows desenham a nova apresentação. Codex participa do desenvolvimento do código. Godot é um editor externo e Meshy gerou candidatos de modelos, sem aprovação automática.</p>{button('Origem, IA e créditos', '/projeto/#ia', True)}</section><section class="panel prose" id="controles"><h2>Controles da prévia R3</h2><p>Estes são os atalhos do perfil de revisão 4fe0f467cd56, não uma promessa de padrão da futura build pública. Eles podem ser remapeados. Testes técnicos e serviços de foco simulados não substituem uma sessão completa com teclado e mouse físicos.</p><div class="table-scroll"><table><thead><tr><th scope="col">Comando</th><th scope="col">Ação</th></tr></thead><tbody><tr><th scope="row"><kbd>WASD / setas</kbd></th><td>Movimento relativo à câmera.</td></tr><tr><th scope="row"><kbd>Mouse / K</kbd></th><td>Olhar e alternar primeira / terceira pessoa; Home restaura terceira pessoa.</td></tr><tr><th scope="row"><kbd>I</kbd></th><td>Inventário; posição e ampliação disponíveis na prévia.</td></tr><tr><th scope="row"><kbd>Tab</kbd></th><td>Mapa.</td></tr><tr><th scope="row"><kbd>Q</kbd></th><td>Missões.</td></tr><tr><th scope="row"><kbd>B</kbd></th><td>Livro de magias.</td></tr><tr><th scope="row"><kbd>E</kbd></th><td>Interação na masmorra: coleta próxima comprovada; portas e gatilhos ainda exigem revisão.</td></tr><tr><th scope="row"><kbd>Botão esquerdo</kbd></th><td>Ataque parado; segurar repete na cadência nativa e soltar interrompe.</td></tr><tr><th scope="row"><kbd>Botão direito</kbd></th><td>Magia selecionada, com repetição e consumo nativos.</td></tr><tr><th scope="row"><kbd>F4 / Esc</kbd></th><td>Alternar apresentação nativa / 3D e abrir o menu.</td></tr></tbody></table></div><details><summary>E o esquema M/Q/J pedido?</summary><p>O esquema alternativo de M para mapa, Q para magias, J para missões e Tab para inventário continua um pedido separado. O perfil desta prévia usa Tab para mapa, Q para missões e B para livro de magias.</p></details></section><section class="panel prose"><h2>Diablo3D e o mod Diablo First Person</h2><p>Diablo3D é o projeto de Douglas Pan. O mod Diablo First Person de plopsDOTio é outro projeto independente. Seu método de materiais de piso e parede foi estudado e adaptado de forma delimitada, preservando nosso renderer, câmera, movimento e UI. Os créditos e avisos herdados continuam aplicáveis.</p>{button('Créditos e método da adaptação', 'https://github.com/douglasopan/diablo-3d/blob/main/docs/DUNGEON-REFERENCE-ADAPTATION.md', True)}</section><section class="panel prose"><h2>O que ainda falta</h2><p>Revisão visual do conjunto, portas e gatilhos, sessões prolongadas, FPS sustentado e correção do retorno entre andares. Arte própria continua em produção. O estudo de gerador de masmorras não é um recurso já entregue.</p><div class="actions">{button('Novidades da Catedral e HUD', '/devlog/primeira-pessoa-previa-nativa-catedral/', True)}{button('Ver o roadmap completo', '/roadmap/', True)}</div></section>'''


def participate():
    steps = [('Escolha um objeto', 'Consulte o catálogo e identifique uma família reutilizável de casas, árvores, pedras, adereços ou personagens.'), ('Reserve o ID', 'Abra uma issue para coordenar o trabalho. Aguarde a confirmação do mantenedor antes de modelar; abrir a issue ainda não confirma a reserva.'), ('Modele e compare', 'Mantenha a composição na câmera original. Complete as faces e confira rotações, escala e contato com o chão.'), ('Envie para revisão', 'Inclua capturas, origem dos recursos, licença e arquivos permitidos. Um candidato passa por revisão antes de ser aceito.')]
    return intro('PARTICIPAR', 'Construa Diablo 3D<br>com a gente.', 'O objetivo reúne todos os níveis de Diablo 1. As contribuições atuais ajudam a revisar Tristram e o piloto técnico do primeiro andar normal da Catedral: arte, código, testes e documentação.') + f'''
<div class="actions">{button('Entrar no Discord', DISCORD)}{button('Ver o catálogo', source('docs/ASSET-CATALOG.md', False), True)}</div>
<section>{section_head('Uma contribuição por vez.')}<ol class="steps">{''.join(f'<li class="step"><span class="step-number">{i+1:02d}</span><h3>{t}</h3><p>{d}</p></li>' for i,(t,d) in enumerate(steps))}</ol></section>
<section class="split"><div class="panel prose"><h2>Arte e modelagem</h2><p>Comece por uma família de objetos completos, não por um fragmento de tile. Compare silhueta, escala, portas, janelas, orientação e encaixe no terreno.</p><p>O catálogo registra 12 objetos arquitetônicos completos em nove famílias, 93 árvores em seis famílias e 501 grupos de pedras em seis padrões na auditoria v4. Essas contagens descrevem a reconstrução atual; não são modelos de colaboradores já aprovados.</p><p>O registro publicado em 07/10 ainda tem zero modelos manualmente autorados aceitos.</p></div><div class="panel prose"><h2>Código, testes e documentação</h2><p>Ajude na renderização, importação, comparação e revisão de guias. Um relato útil inclui versão, enquadramento, reprodução do problema e captura contextual.</p><p>Há espaço para testar seleção, oclusão, caminhada e integração do renderer sem mudar a lógica da partida.</p>{button('Abrir uma reserva de recurso', GITHUB + '/issues/new?template=3d_asset.yml', True)}</div></section>
<details class="panel"><summary>Checklist para enviar um modelo</summary><div class="prose"><ul><li>Referência e procedência documentadas.</li><li>Mesma composição, escala e silhueta na câmera original.</li><li>Objeto completo, com faces e contato com o chão revisados.</li><li>Capturas em ±5° e 0°, 90°, 180°, 270°.</li><li>Sem arquivos proprietários extraídos, saves ou credenciais.</li></ul><p>Não envie MPQ, CEL/CL2/MIN/TIL/SOL extraídos, texturas isoladas do jogo ou modelos derivados desses arquivos para o site.</p></div></details>
<section class="contribute-banner chapter-band atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap"><div><p class="eyebrow">OUTRA FORMA DE PARTICIPAR</p><h2>Ajude a sustentar o desenvolvimento.</h2><p>Apoio financeiro e ajuda com Meshy, outros geradores 3D, ChatGPT e Claude contribuem para as revisões e a continuidade do projeto.</p></div>{button('Ver formas de apoio', '/apoiar/')}</div>{art_credit()}</section>
<aside class="notice"><h2>Antes de contribuir</h2><p>Leia o <a href="{source('docs/CONTRIBUTING.md', False)}">guia de contribuição</a> e a <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>. A distribuição deve ser gratuita e não comercial. Arte independente precisa de origem e permissões compatíveis.</p></aside>'''


def roadmap():
    milestones = [
        ('EM DESENVOLVIMENTO', 'Tristram', 'Tristram e a prévia local R3 usam sprites nativos para heróis e NPCs. Os imports Meshy antigos continuam preservados para revisão. Arte, reconstrução, câmera e desempenho ainda precisam de trabalho.'),
        ('EM DESENVOLVIMENTO', 'Primeiro andar normal da Catedral', 'A prévia local R3 inclui materiais nativos, escadas, câmera e caixão 3D no primeiro andar normal. Os gates específicos passaram; o teste amplo ainda encontra defeito no retorno 2→1. Arte, portas, gameplay físico e andares posteriores não estão concluídos.'),
        ('PRIORIDADE · JOGO COMPLETO', 'Concluir e estabilizar todo Diablo 1', 'Completar todos os níveis da Catedral, Catacumbas, Cavernas e Inferno, incluindo personagens, inimigos, objetos, animações, efeitos e conteúdo do jogo. Cada ambiente precisa de implementação, comparação com o original e testes. Tristram é o primeiro estágio, não o limite do projeto.'),
        ('FUTURO · APÓS O JOGO COMPLETO', 'Persistência e renovação do mundo', 'Depois de concluir e estabilizar Diablo 1, validar mundo persistente, respawn de loot e renovação de objetos, contêineres e estoques de NPCs. Saves, reinício, reconexão, quests e prevenção de duplicação precisam de testes. Os intervalos serão configuráveis: 30 minutos é apenas um exemplo a definir e testar.'),
        ('FUTURO · DEPENDE DA PERSISTÊNCIA', 'Pesquisar multiplayer para 50–100 jogadores', 'A meta de 50–100 jogadores depende da persistência, de arquitetura de rede e de validação de autoridade, sincronização e capacidade. É pesquisa futura, sem prazo ou capacidade disponível. O build atual é offline.'),
        ('FUTURO · FASE MULTIPLAYER', 'Identidade e convivência', 'Nomes e níveis dos jogadores, chat em balões e amigos pertencem à futura fase multiplayer. Voz por proximidade continua como pesquisa, com decisões próprias de moderação, privacidade e custos. Esses recursos ainda não estão implementados.'),
        ('FUTURO · DEPOIS DO MULTIPLAYER', 'Expansão da superfície e novos níveis', 'Planejar novas regiões exploráveis de superfície e novos níveis depois da fase multiplayer. Progressão, geração, saves e compatibilidade exigem uma proposta e um piloto próprios. Horizonte decorativo e fog já estão integrados como visuais provisórios em Tristram; dia/noite continua como trabalho futuro. Nenhum desses recursos amplia o terreno jogável.'),
    ]
    features = [
        ('PRÉVIA LOCAL · REVISÃO PENDENTE', 'HUD compacto e painéis', 'A prévia local já tem painéis, mapa e inventário arrastáveis, ampliação do inventário e botão de nível corrigido. O sistema completo de tamanho e sobreposição de todas as janelas, e a revisão física prolongada, continuam pendentes.'),
        ('PRÉVIA LOCAL · INTERAÇÕES DELIMITADAS', 'Interagir antes de atacar', 'E e o ataque parado estão integrados na prévia local; a coleta próxima passou no gate específico. Portas, baús e gatilhos precisam de revisão própria. Alcance, linha de visão e cadência continuam nativos; isso não qualifica toda interação.'),
        ('CANDIDATO PRIVADO · ASSETS PENDENTES', 'Herói, mãos e equipamento em primeira pessoa', 'O Guerreiro 2K é candidato privado com 52 ossos, mãos e dedos, sem arma, escudo ou clipes, sem aceite final ou instalação. O inventário e a derivação local das mãos estão em preparação; sincronizar arma e equipamento ao combate depende de assets aprovados e de uma arma 3D ainda ausente. O rig anterior de Ogden está preservado como histórico; b775 usa seu sprite nativo.'),
        ('FUTURO · REQUISITO DEFINIDO', 'Indicadores de jogadores e monstros', 'Jogadores terão nome, nível e HP acima da cabeça, sem etiqueta do próprio jogador em primeira pessoa. Para monstros, estudar somente a barra nativa ao mirar; indicadores permanentes em todos os monstros não fazem parte do pedido. Implementação e validação ainda são necessárias.'),
        ('FUTURO · PILOTO DELIMITADO', 'Bússola e pontos descobertos', 'A bússola no topo deve seguir a orientação da câmera e mostrar somente pontos já descobertos. O piloto começa pelas escadas, preservando seed e andar. Cadastro 3D e objetivos ficam para uma etapa posterior.'),
        ('PILOTO ISOLADO · COMPATIBILIDADE PENDENTE', 'Rotação de itens no inventário em 90°', 'Contrato e piloto isolado devem trocar largura e altura do item, preservando atributos e permitindo rollback. Antes de aplicar, validar saves, rede, stash, trocas, lojas e equipamento. Girar a imagem sozinho não resolve o encaixe nem a compatibilidade.'),
    ]
    return intro('ROADMAP', 'Diablo inteiro.<br>Um marco de cada vez.', 'Todos os níveis fazem parte do objetivo. Tristram e o primeiro andar da Catedral estão em desenvolvimento. A ordem continua: jogo completo em 3D, persistência e respawn, pesquisa de multiplayer para 50–100 jogadores e, depois, expansão. Este roadmap não fixa prazos.') + f'''
{checkpoint_notice()}

<aside class="notice"><p class="eyebrow">REFERÊNCIA LOCAL · INSTALAÇÃO DE 09 OUTUBRO 2026</p><p><kbd>F4</kbd> alterna nativo e 3D · <kbd>K</kbd> alterna terceira e primeira pessoa · <kbd>Home</kbd> restaura terceira pessoa · build offline.</p><p>Este registro conserva a instalação local de 9 de outubro. As correções e os gates seguintes continuam com o integrador; sua conclusão e a versão pública não são inferidas deste registro.</p><p><a href="{url('/devlog/cameras-continuidade-catedral/')}">registro da instalação e seus limites</a> · <a href="{GITHUB}/commit/{SNAPSHOT}">commit 7e4a6c3b</a></p></aside>
<section class="timeline">{''.join(f'<article class="milestone panel"><span class="tag">{badge}</span><h2>{title}</h2><p>{body}</p></article>' for badge,title,body in milestones)}</section>
<section aria-labelledby="candidatos"><h2 id="candidatos">Candidatos e requisitos em preparação</h2><p>Os estados abaixo distinguem código privado, pilotos isolados e recursos futuros. Nenhum deles representa uma nova versão pública pronta para instalar.</p><div class="card-grid">{''.join(f'<article class="panel prose"><span class="tag">{badge}</span><h3>{title}</h3><p>{body}</p></article>' for badge,title,body in features)}</div></section>
<section class="panel prose"><h2>Editor de mapas: decisão condicionada à viabilidade</h2><p>Os testes standalone, da guarda CPU e headless limitado passaram, mas ainda falta validação ponta a ponta com os 13 modelos. Aplicação ao jogo, dados canônicos, máscaras visuais, colisão, seleção e captura compatível continuam pendentes. Editar e aplicar casas pela entrada habitual ainda precisa dessa prova.</p><p>A menor adaptação é simplificar o Godot e os gizmos existentes. Um aplicativo dedicado ou uma planta 2D exigiriam nova implementação. A escolha depende de viabilidade e decisão; não ganha prioridade automática sobre concluir o jogo, nem tem prazo ou requisito de RAM prometido.</p></section>
<section id="distribuicao" class="panel prose"><p class="eyebrow">VERSÕES NA PANVIA</p><h2>Distribuição pública e instalação</h2><p>As versões para download e testes ficam na Panvia. Confira ali a versão, os requisitos e as instruções de instalação. Dados originais do jogo continuam fora da distribuição.</p><div class="actions">{button('Download e testes na Panvia', PANVIA)}</div></section>
<section class="panel prose"><h2>O que precisa melhorar agora</h2><p>Silhuetas e faces ocultas da arquitetura, materiais, personagens e oclusão. Geração automática auxilia o processo, mas cada objeto precisa de revisão humana e comparação real.</p><p>As comparações históricas pela rota nativa conservam a versão em que foram feitas. Validar a geometria exige revisar a malha ativa na mesma câmera.</p><p>A expansão deste projeto para Diablo 2 foi cancelada e fica fora deste roadmap. A prioridade é Diablo 1 completo, seguida de multiplayer e, depois, expansão.</p><a href="{GITHUB}/blob/{ROADMAP_SNAPSHOT}/docs/ROADMAP.md">Consultar o roadmap versionado no GitHub ↗</a></section>'''


def gallery(evidence):
    items = []
    for entry in evidence:
        items.append(f'''<figure class="gallery-item" data-filter-item data-category="{esc(entry['category'])}" data-search="{esc(entry['title'] + ' ' + entry['caption'])}"><a href="{url(entry['file'])}" data-lightbox data-caption="{esc(entry['title'] + ' — ' + entry['caption'])}">{img(entry['file'], entry['alt'])}<span class="image-open" aria-hidden="true">Ampliar ↗</span></a><figcaption><span class="tag">{esc(entry['stage'])}</span><h2>{esc(entry['title'])}</h2><p>{esc(entry['caption'])}</p></figcaption></figure>''')
    return intro('GALERIA', 'Do primeiro protótipo<br>ao jogo completo.', 'O destino é todo Diablo 1 em 3D. Esta galeria reúne os vídeos públicos do projeto e as capturas técnicas de Tristram e da Catedral. Cada registro conserva seu contexto: gameplay gravado, estudos de personagens, trilhas ou testes offscreen. As capturas técnicas não representam sessões de gameplay ou aprovação de arte final.') + '<p class="notice">As etapas v1 a v4 são registros locais retrospectivos publicados aqui em 07/10/2026. Novos níveis serão documentados quando implementados. As ilustrações de ambientação e os conceitos de interface ficam separados das capturas e comparações técnicas abaixo, identificadas por legenda e procedência.</p>' + all_videos_section(LANG, url) + filters(sorted(set(e['category'] for e in evidence))) + f'<section class="gallery-grid">{"".join(items)}</section>' + no_results() + f'<p class="meta">{len(evidence)} {translated('capturas selecionadas.')} <a href="{url("/evidence.json")}">Inventário público e proveniência</a>.</p>'


def support():
    return intro('APOIAR O DESENVOLVIMENTO', 'Ajude a construir<br>Diablo inteiro em 3D.', 'Modelos, revisões e ferramentas exigem investimento. Seu apoio ajuda a manter o desenvolvimento, avançar até a conclusão e explorar novas possibilidades. Tristram segue em revisão e o piloto técnico de nove regiões do primeiro andar normal da Catedral ganhou materiais nativos; formas, arte e gameplay continuam pendentes. O objetivo inclui todos os níveis.') + f'''
<div class="actions">{button('Conversar no Discord', DISCORD)}{button('Apoiar no Patreon', PATREON, True)}{button('Ver formas de apoio', '#formas-de-apoio', True)}</div>
<section id="formas-de-apoio">{section_head('Escolha como apoiar.', 'O contato direto é o caminho preferido para combinar a contribuição.')}<div class="support-grid">
<article class="support-card panel"><p class="eyebrow">APOIO FINANCEIRO · CONTATO DIRETO</p><h2>Vamos combinar<br>a melhor forma.</h2><p>Para apoiar em dinheiro, fale com <strong class="contact-handle">douglasopan</strong> no Discord. Podemos combinar uma forma adequada ao seu país e ao valor da contribuição.</p><div class="actions">{button('Entrar no Discord oficial', DISCORD)}</div><p class="meta">No servidor, procure por <strong>douglasopan</strong>.</p><div class="payment-identity"><span>Destinatário informado para PayPal</span><strong>douglas8pan@gmail.com</strong></div><div class="actions"><button type="button" class="button secondary" data-copy="douglas8pan@gmail.com" hidden data-enhancement>Copiar e-mail PayPal</button>{button('Abrir PayPal', 'https://www.paypal.com/', True)}</div><p id="copy-status" class="meta" aria-live="polite"></p><p>Se preferir usar o PayPal diretamente, copie o e-mail acima e confira o destinatário na plataforma antes de confirmar o envio.</p><p class="meta">Para apoio dentro do Brasil, também podemos combinar o envio pelo Asaas ou outra forma disponível por contato. Um link de pagamento será compartilhado quando definido.</p><p>Se preferir apoiar pela plataforma, visite a página de Douglas Pan no Patreon.</p>{button('Apoiar no Patreon', PATREON, True)}</article>
<article class="support-card panel"><p class="eyebrow">CRÉDITOS E FERRAMENTAS</p><h2>Ajude a transformar<br>ideias em recursos.</h2><p>Aceitamos ajuda com créditos e uso de ferramentas que apoiam a criação de modelos, o código e a documentação.</p><div class="credit-list"><span>Meshy</span><span>Outros geradores 3D</span><span>ChatGPT</span><span>Claude</span></div><p>Converse antes para combinar uma forma compatível com a plataforma: créditos, uma assinatura oferecida ou ajuda na geração de um recurso. A disponibilidade varia entre os serviços.</p>{button('Combinar apoio em créditos', DISCORD, True)}<p class="meta">Não é necessário compartilhar senha nem chave de API.</p></article>
</div><p class="notice">O apoio é voluntário. O contato direto permite combinar a forma de envio; PayPal, Asaas e outros meios podem cobrar tarifas de processamento ou conversão conforme o método e o país. Consulte as condições do serviço antes de confirmar. <a href="https://www.paypal.com/br/business/paypal-business-fees">Tarifas PayPal ↗</a> · <a href="https://www.asaas.com/link-pagamento">Links de pagamento Asaas ↗</a></p></section>
<section class="section-atmosphere atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp')}<div class="atmosphere-content wrap">{section_head('O que o apoio torna possível.')}<div class="support-uses"><article class="panel"><p class="eyebrow">01 · CRIAÇÃO</p><h3>Modelos e revisões</h3><p>Gerar candidatos, completar faces, ajustar materiais e comparar cada recurso com o original.</p></article><article class="panel"><p class="eyebrow">02 · FERRAMENTAS</p><h3>Desenvolvimento</h3><p>Custear ferramentas de geração, assistência ao código, documentação e testes.</p></article><article class="panel"><p class="eyebrow">03 · CONTINUIDADE</p><h3>Conclusão e expansão</h3><p>Dar continuidade ao trabalho da cidade aos níveis do jogo e avaliar possibilidades futuras.</p></article></div></div>{art_credit()}</section>
<section class="support-faq">{section_head('Apoio com clareza.')}<details class="panel"><summary>O apoio é voluntário?</summary><div class="prose"><p>Sim. Ele ajuda a sustentar o desenvolvimento e não compra acesso exclusivo ao jogo. O código permanece público sob a <a href="{source('LICENSE.md', False)}">Sustainable Use License</a>, com distribuição gratuita e não comercial.</p></div></details><details class="panel"><summary>Onde acompanho o progresso?</summary><div class="prose"><p>As mudanças publicadas, capturas e verificações aparecem no <a href="{url('/devlog/')}">devlog</a>. O <a href="{url('/roadmap/')}">roadmap</a> mostra o estado atual e os marcos planejados. Não há data prometida de conclusão.</p></div></details><details class="panel"><summary>Posso ajudar sem dinheiro ou créditos?</summary><div class="prose"><p>Sim. Modelagem, programação, capturas de teste e documentação também fazem o projeto avançar. Veja <a href="{url('/participar/')}">como participar</a>.</p></div></details></section>'''


def soundtrack_page(music):
    first = music['tracks'][0]
    credit = esc(credit_text(LANG))
    contexts = {
        'menu-rock2': 'Menu principal · versão Rock2',
        'menu-alternative': 'Menu principal · versão anterior',
        'town-rock': 'Tristram · opção 1',
        'town-alternative': 'Tristram · opção 2',
        'town-third': 'Tristram · opção 3',
    }
    tracks = []
    for number, track in enumerate(music['tracks'], 1):
        asset = versioned_asset(track['public_path'])
        title = esc(track['title'] + (' — Alternate Export' if track['id'] == 'town-third' else ''))
        youtube_url = SOUNDTRACK_YOUTUBE.get(track['id'])
        youtube_link = button('Ouvir no YouTube', youtube_url, True).replace('<a ', f'<a data-soundtrack-youtube="{esc(track["id"])}" ', 1) if youtube_url else ''
        size = f"{track['size_bytes'] / 1_000_000:.1f} MB"
        tracks.append(f'''<li class="soundtrack-track panel"><span class="track-number" aria-hidden="true">{number:02}</span><div class="track-copy"><p class="eyebrow">{esc(contexts.get(track['id'], track['environment']))}</p><h2>{title}</h2><p class="track-credit">{credit}</p><p class="meta">{esc(track['duration_label'])} <span aria-hidden="true">·</span> MP3 · {size}</p></div><div class="actions"><button class="button secondary" type="button" data-soundtrack-src="{esc(asset)}" data-soundtrack-title="{title}" aria-label="{esc(translated('Tocar') + ' ' + track['title'])}" aria-pressed="false" hidden data-enhancement><span data-soundtrack-label>Tocar</span></button><a class="button" href="{esc(asset)}" download="Douglas-Pan--{esc(track['id'])}.mp3" aria-label="{esc(translated('Baixar MP3') + ' · ' + track['title'])}">Baixar MP3 <span aria-hidden="true">↓</span></a>{youtube_link}<noscript><a href="{esc(asset)}">Ouvir MP3 ↗</a></noscript></div></li>''')
    return intro('REGRAVAÇÕES · DIABLO 3D', 'A música de Diablo.<br>Em novas interpretações.', 'Ouça e baixe gratuitamente regravações e reinterpretações das músicas originais de Diablo, produzidas por Douglas Pan com auxílio de IA. A biblioteca atual reúne versões do menu principal e de Tristram.') + f'''
<section class="notice prose" id="music-credits"><h2>Créditos musicais</h2><p>{credit}</p><p><a href="{MUSIC_CREDIT_SOURCE}">Créditos oficiais de Diablo — manual, página 78 ↗</a></p></section>
<section class="soundtrack-player panel" aria-label="Player da trilha"><p class="eyebrow">ESCUTE A TRILHA</p><h2 id="soundtrack-title">{esc(first['title'])}</h2><p>{credit}</p><audio id="soundtrack-player" controls preload="none" src="{esc(versioned_asset(first['public_path']))}" aria-label="{esc(translated('Ouvir') + ' ' + first['title'])}">Seu navegador pode baixar as faixas pelos links abaixo.</audio><p id="soundtrack-status" class="meta" role="status" aria-live="polite">Escolha uma faixa para ouvir.</p><p>{button('Ouvir a playlist no YouTube', SOUNDTRACK_YOUTUBE_PLAYLIST, True)}</p></section>
<ol class="soundtrack-list" aria-label="Faixas para ouvir e baixar">{''.join(tracks)}</ol>
{videos_section(LANG, url)}
<aside class="notice prose"><h2>Versões do projeto</h2><p>{esc(translated('Esta biblioteca reúne {count} arquivos personalizados.').format(count=len(music['tracks'])))}</p><p>Tristram 3 aparenta ser outra exportação da primeira opção; a lista reúne versões disponíveis, sem contar cada arquivo como uma composição distinta.</p><p>Main Menu é o rótulo usado no projeto; o título oficial da composição correspondente ainda não foi confirmado.</p><p>Douglas Pan também consta nas tags internas dos MP3s como crédito da produção destas versões. A composição original é de Matt Uelmen. Esta biblioteca oferece regravações/reinterpretações; as gravações originais do jogo não são distribuídas aqui.</p><p>Não é necessário cadastrar e-mail para ouvir ou baixar.</p></aside>
<section class="panel prose"><h2>Acompanhe os próximos capítulos.</h2><p>O objetivo é reconstruir todo Diablo 1 em 3D. Novas faixas e avanços aparecem no devlog.</p><div class="actions">{button('Acompanhar o devlog', '/devlog/')}{button('Apoiar o projeto', '/apoiar/', True)}{button('Apoiar no Patreon', PATREON, True)}</div></section>'''


def load_newsletter():
    config = json.loads((ROOT / 'newsletter.json').read_text(encoding='utf-8'))
    if set(config) != {'signup_url', 'cancel_url'}:
        raise ValueError('Newsletter config must contain only public signup and cancellation URLs')
    for value in config.values():
        if not isinstance(value, str) or not re.fullmatch(r'https://docs\.google\.com/forms/d/e/[A-Za-z0-9_-]+/viewform', value):
            raise ValueError('Newsletter URLs must be public Google Forms respondent links')
    if config['signup_url'] == config['cancel_url']:
        raise ValueError('Newsletter signup and cancellation forms must be distinct')
    return config


def newsletter_banner():
    return f'''<section class="newsletter-banner panel"><div><p class="eyebrow">NOVIDADES POR E-MAIL</p><h2>Acompanhe a próxima etapa.</h2><p>Deixe seu e-mail se quiser receber notícias do Diablo 3D. A inscrição é voluntária e pode ser cancelada.</p></div>{button('Quero receber novidades', '/novidades/')}</section>'''


def newsletter_page(config):
    return intro('ACOMPANHE O PROJETO', 'O próximo capítulo.<br>No seu e-mail.', 'Inscreva-se voluntariamente para receber novidades do desenvolvimento do Diablo 3D, em português ou inglês.') + f'''
<section class="panel prose newsletter-details"><h2>Receba novidades do Diablo 3D</h2><p>O formulário pede seu e-mail, o idioma de preferência e seu consentimento para receber notícias do projeto. As respostas ficam privadas com o responsável pelo Diablo 3D.</p><p>Seu contato será usado para novidades do projeto. Não é necessário se inscrever para ler o site, ouvir músicas ou baixar os arquivos gratuitos.</p><div class="actions">{button('Deixar meu e-mail', config['signup_url'])}</div><p class="meta">O formulário abre no Google Forms. A confirmação aparece após o envio concluído.</p></section>
<section class="panel prose newsletter-details"><h2>Quer cancelar?</h2><p>Use o formulário de cancelamento com o mesmo e-mail da inscrição. O pedido é recebido de forma privada e processado manualmente antes de qualquer novo envio.</p><div class="actions">{button('Pedir cancelamento', config['cancel_url'], True)}</div><p>Para dúvidas sobre sua inscrição, fale com douglasopan no Discord oficial.</p>{button('Conversar no Discord', DISCORD, True)}</section>'''


def write(path, text):
    target = OUT / localized_path(path).lstrip('/')
    if path.endswith('/'):
        target /= 'index.html'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding='utf-8', newline='\n')


def build_locale(posts, evidence, soundtrack, newsletter):
    pages = [
        ('/download/', 'Download Diablo3D · local3 FULL', 'Complete local3 preview package for Windows x64.', download_page()),
        ('/', 'Diablo 1 em 3D · Primeira e terceira pessoa', 'Diablo3D recria todo Diablo 1 em 3D, com primeira e terceira pessoa. Conheça os modelos, acompanhe o diário e baixe a versão disponível na Panvia.', home(posts)),
        ('/devlog/', 'Devlog Diablo3D · Diablo 1 em primeira pessoa', 'Acompanhe o desenvolvimento do jogo completo em Diablo 3D. Registros de Tristram, piloto da Catedral e materiais nativos, com capturas, medições do renderer e limites.', intro('DEVLOG', 'Diário de construção.', 'Do primeiro protótipo ao objetivo de reconstruir todo Diablo 1 em 3D. O que mudou, como foi testado e o que ainda precisa de revisão, com capturas, comparações técnicas e fontes versionadas.') + devlog_highlight() + '<p class="notice">Os registros de 07/10/2026 formam a retrospectiva inicial. v1–v4 representam etapas locais anteriores; novos registros acompanham as entregas seguintes.</p>' + filters(CATEGORIES, True) + '<h2 class="sr-only">Registros publicados</h2><section class="card-grid devlog-feed">' + ''.join(card(p) for p in posts) + '</section>' + no_results()),
        ('/musica/', 'Música de Diablo · Regravações', 'Composição original de Matt Uelmen para Diablo (Blizzard Entertainment). Ouça as regravações/reinterpretações produzidas por Douglas Pan com auxílio de IA.', soundtrack_page(soundtrack)),
        ('/novidades/', 'Receba novidades do Diablo 3D', 'Inscreva-se voluntariamente para receber novidades do Diablo 3D em português ou inglês. Respostas privadas e cancelamento disponível.', newsletter_page(newsletter)),
        ('/projeto/', 'Sobre o projeto', 'Douglas Pan, direção do Diablo3D, uso de IA no código, arte, textos e música, créditos ao DevilutionX e limites do protótipo.', project()),
        ('/tecnologia/', 'Diablo 1 First Person · Câmeras e controles', 'Como funciona Diablo 1 em primeira pessoa no Diablo3D: câmeras, WASD, interação, HUD e limites da prévia R3. Download e testes na Panvia.', technology()),
        ('/participar/', 'Participe do Diablo 3D', 'Contribua com modelos, código, testes e documentação. Reserve um objeto, compare com o original e envie para revisão.', participate()),
        ('/apoiar/', 'Apoie o desenvolvimento', 'Ajude a desenvolver todo Diablo 1 em 3D. Apoio financeiro por contato direto e PayPal, ou ajuda com Meshy, outros geradores 3D, ChatGPT e Claude.', support()),
        ('/roadmap/', 'Roadmap do Diablo 3D', 'Todos os níveis de Diablo 1 em 3D: Tristram, Catedral, Catacumbas, Cavernas e Inferno. Etapa atual, próximos marcos e pesquisas futuras.', roadmap()),
        ('/galeria/', 'Galeria do desenvolvimento', 'Vídeos públicos e capturas do Diablo 3D: devlogs, showcases, trilhas e testes técnicos de Tristram e da Catedral, com contexto, créditos e limites.', gallery(evidence)),
    ]
    for path, title, description, content in pages:
        if path in ('/', '/devlog/', '/musica/'):
            content += newsletter_banner()
        write(path, frame(title, description, path, content, music=soundtrack if path == '/musica/' else None))
    for post in posts:
        content = f'''<header class="page-intro article-intro atmospheric bleed">{scene('/assets/art/journal-atmosphere.webp', True)}<div class="intro-copy"><p class="eyebrow">{esc(translated(post['category']))} · DEVLOG</p><h1>{esc(post['title'])}</h1><p class="lede">{esc(post['description'])}</p><div class="meta"><time datetime="{post['date']}">{date_label(post['date'])}</time><span>Projeto D3D</span><span>{post['minutes']} {translated('min de leitura')}</span></div></div>{art_credit()}</header><nav class="breadcrumb" aria-label="Você está aqui"><a href="{url('/devlog/')}">Devlog</a><span aria-hidden="true"> / </span><span>{esc(post['category'])}</span></nav><div class="article-layout"><article class="prose"><figure>{img(post['image'], post['image_alt'], True)}<figcaption>{esc(post['image_alt'])}</figcaption></figure>{post['body']}<aside class="notice"><p>Este registro é versionado no GitHub. <a href="{source(post['source_file'], False)}">Ver fonte e histórico ↗</a></p></aside></article><aside class="toc"><details open><summary>Neste registro</summary>{post['toc']}</details><div class="notice"><p>F4 alterna nativo e 3D. Uma comparação da geometria exige revisar a malha ativa.</p></div></aside></div><section>{section_head('Continue acompanhando.')}<div class="card-grid">{''.join(card(p) for p in [p for p in posts if p is not post][:3])}</div></section>'''
        write(post['path'], frame(post['title'], post['description'], post['path'], content, post['image'], post))
    write('/404.html', frame('Página não encontrada', 'Encontre o devlog, as capturas e as informações do projeto Diablo 3D.', '/404.html', intro('404', 'Esse caminho ainda<br>não foi construído.', 'Você pode continuar pelo diário do projeto ou voltar ao início.') + button('Voltar ao início', '/'), noindex=True))
    atom = 'http://www.w3.org/2005/Atom'
    ET.register_namespace('atom', atom)
    rss = ET.Element('rss', version='2.0')
    channel = ET.SubElement(rss, 'channel')
    for name, value in [('title', 'Diablo 3D — Diário de construção'), ('link', absolute('/devlog/')), ('description', 'Etapas reais de um projeto para reconstruir todo Diablo 1 em 3D. Tristram em revisão e piloto técnico de nove regiões com materiais nativos no primeiro andar normal da Catedral.'), ('language', LANG)]:
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
    write('/soundtrack.json', json.dumps(public_soundtrack(soundtrack, LANG), ensure_ascii=False, indent=2) + '\n')
    return [localized_path(p[0]) for p in pages] + [localized_path(p['path']) for p in posts]


def build():
    global LANG
    LANG = 'pt-BR'
    load_analytics()  # Invalid tracking configuration must fail before replacing the artifact.
    soundtrack = load_soundtrack()  # Validate actual final MP3 tags and hashes before touching output.
    newsletter = load_newsletter()
    load_study()  # Validate the selected document media before replacing output.
    posts = load_posts()
    raw_evidence = json.loads((ROOT / 'evidence.json').read_text(encoding='utf-8'))
    evidence = raw_evidence['entries'] if isinstance(raw_evidence, dict) else raw_evidence
    for entry in evidence:
        image_info(entry['file'])
        if not all(entry.get(k) for k in ('title', 'alt', 'caption', 'category', 'stage', 'source', 'sha256')):
            raise ValueError('Incomplete evidence metadata')
    allowed = {'.css', '.js', '.png', '.webp', '.svg', '.woff', '.woff2'}
    public_extras = {PUBLIC / soundtrack['background']['public_path'].lstrip('/'), PUBLIC / 'vendor/plyr/LICENSE.txt'}
    public_extras.update(PUBLIC / track['public_path'].lstrip('/') for track in soundtrack['tracks'])
    subtitles = PUBLIC / DEVLOG_SUBTITLES.lstrip('/')
    if hashlib.sha256(subtitles.read_bytes()).hexdigest() != DEVLOG_SUBTITLES_SHA256:
        raise ValueError('The final English subtitle file differs from the reviewed publication')
    public_extras.add(subtitles)
    for path in PUBLIC.rglob('*'):
        if path.is_symlink() or (path.is_file() and path.suffix.lower() not in allowed and path not in public_extras and not (path.parent == PUBLIC / 'assets/fonts' and path.suffix == '.txt')):
            raise ValueError(f'Unexpected public file: {path.relative_to(PUBLIC)}')
    # Output deletion is deliberately constrained to this generator's own directory.
    if OUT.resolve().parent != ROOT.resolve() or OUT.name != 'dist':
        raise ValueError('Output path escaped website directory')
    if OUT.exists():
        shutil.rmtree(OUT)
    shutil.copytree(PUBLIC, OUT)
    copy_study(OUT)
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
        paths.extend(build_locale(posts, localized_evidence, soundtrack, newsletter))
    LANG = 'pt-BR'
    write('/releases.json', (ROOT / 'releases.json').read_text(encoding='utf-8'))
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
