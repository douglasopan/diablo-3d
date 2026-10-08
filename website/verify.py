"""Validate the actual publication artifact before upload, without network access."""
from __future__ import annotations

import json
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

import build
from soundtrack import load_soundtrack, safe_file, sha256, validate_mp3


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.tags = []
        self.ids = set()
        self.h1 = 0
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        self.tags.append((tag, attrs))
        if tag == 'h1':
            self.h1 += 1
        if attrs.get('id'):
            assert attrs['id'] not in self.ids, f'Duplicate ID: {attrs["id"]}'
            self.ids.add(attrs['id'])


def verify():
    root = build.OUT
    files = list(root.rglob('*'))
    assert root.is_dir(), 'Build the website before verifying'
    documents = {p: Document(p.read_text(encoding='utf-8')) for p in files if p.suffix == '.html'}
    assert len(documents) >= 9, 'Expected the complete website and article pages'
    analytics = build.load_analytics()
    for path, document in documents.items():
        assert document.h1 == 1, f'{path}: exactly one h1 required'
        language = 'en' if path.relative_to(root).parts[0] == 'en' else 'pt-BR'
        assert any(t == 'html' and a.get('lang') == language for t, a in document.tags), f'{path}: incorrect language'
        assert any(t == 'meta' and a.get('name') == 'description' and a.get('content') for t, a in document.tags)
        assert any(t == 'link' and a.get('rel') == 'canonical' and a.get('href', '').startswith(build.BASE) for t, a in document.tags)
        assert any(t == 'meta' and a.get('property') == 'og:image' for t, a in document.tags)
        alternates = {a.get('hreflang'): a.get('href') for t, a in document.tags if t == 'link' and a.get('rel') == 'alternate' and a.get('hreflang')}
        assert set(alternates) == {'pt-BR', 'en', 'x-default'}, f'{path}: language alternates missing'
        logical = '/' + path.relative_to(root).as_posix()
        if language == 'en':
            logical = logical[3:]
        if logical.endswith('/index.html'):
            logical = logical[:-10]
        for locale in ('pt-BR', 'en', 'x-default'):
            assert alternates[locale] == build.alternate_url(logical, locale), f'{path}: incorrect alternate page'
        raw = path.read_text(encoding='utf-8')
        tracking_scripts = [a for t, a in document.tags if t == 'script' and a.get('src', '').startswith('https://www.googletagmanager.com/gtag/js')]
        if analytics['measurement_id']:
            assert len(tracking_scripts) == 1, f'{path}: one Google tag required'
            assert tracking_scripts[0]['src'] == 'https://www.googletagmanager.com/gtag/js?id=' + analytics['measurement_id'], f'{path}: incorrect measurement ID'
            assert 'async' in tracking_scripts[0], f'{path}: Google tag must be async'
            assert raw.count("gtag('config', '" + analytics['measurement_id'] + "');") == 1, f'{path}: one standard Google config required'
        else:
            assert not tracking_scripts, f'{path}: disabled tracking must omit Google tag'
        import re
        for data in re.findall(r'<script type="application/ld\+json">(.*?)</script>', raw, re.S):
            json.loads(data)
        for tag, attrs in document.tags:
            if tag == 'img':
                assert 'alt' in attrs, f'{path}: image missing alt'
                # The lightbox image receives its source/dimensions at interaction time.
                if attrs.get('id') != 'dialog-image':
                    assert int(attrs.get('width', 0)) > 0 and int(attrs.get('height', 0)) > 0
            for name in ('href', 'src'):
                if not attrs.get(name):
                    continue
                parsed = urlsplit(attrs[name])
                if parsed.scheme or parsed.netloc:
                    assert parsed.scheme in ('https', 'http'), f'{path}: unsafe URL'
                    continue
                if not parsed.path:
                    target = path
                else:
                    assert parsed.path.startswith(build.PREFIX + '/'), f'{path}: unprefixed project URL {attrs[name]}'
                    relative = unquote(parsed.path[len(build.PREFIX):]).lstrip('/')
                    target = root / relative
                    if parsed.path.endswith('/'):
                        target /= 'index.html'
                assert target.is_file(), f'{path}: broken local link {attrs[name]}'
                if parsed.fragment:
                    assert target in documents and unquote(parsed.fragment) in documents[target].ids, f'{path}: broken fragment {attrs[name]}'
        assert not any(t == 'script' and a.get('src', '').startswith('http') and a not in tracking_scripts for t, a in document.tags), 'Only the configured Google tag may be external'
    sitemap = ET.parse(root / 'sitemap.xml')
    locations = [element.text for element in sitemap.getroot().iter() if element.tag.endswith('loc')]
    assert len(locations) == len(documents) - 2, 'Sitemap must include both languages and omit both 404 pages'
    assert len(locations) == len(set(locations)), 'Duplicate sitemap URLs'
    for location in locations:
        assert location.startswith(build.BASE + '/')
    posts = build.load_posts()
    for language in ('pt-BR', 'en'):
        rss = ET.parse(root / ('en/rss.xml' if language == 'en' else 'rss.xml'))
        assert rss.findtext('./channel/language') == language
        assert len(rss.findall('.//item')) == len(posts), 'RSS must include every published post in each language'
        assert all(item.findtext('link').startswith(build.alternate_url('/devlog/', language)) for item in rss.findall('.//item'))
    assert (root / 'robots.txt').read_text().endswith(build.absolute('/sitemap.xml') + '\n')
    assert not any(p.name in ('PROMPTS.md', 'requirements.txt', 'build.py') or 'design-references' in p.parts for p in files), 'Build-only materials leaked to artifact'
    assert not any(p.suffix.lower() in ('.mpq', '.sv', '.cel', '.cl2', '.min', '.til', '.sol', '.obj', '.glb', '.gltf') for p in files), 'Proprietary/derived game assets must never be uploaded'
    soundtrack = load_soundtrack()
    for language in ('', 'en/'):
        assert json.loads((root / (language + 'soundtrack.json')).read_text(encoding='utf-8')) == soundtrack, 'Published soundtrack manifest differs from validated source'
        library = documents[root / (language + 'musica/index.html')]
        assert sum(t == 'audio' for t, a in library.tags) == 1, 'Library must have one player'
        assert any(t == 'audio' and a.get('id') == 'soundtrack-player' and 'autoplay' not in a for t, a in library.tags)
        downloads = [a for t, a in library.tags if t == 'a' and 'download' in a]
        assert len(downloads) == len(soundtrack['tracks']), 'Every custom track needs a free direct download'
    expected_audio = {track['public_path'] for track in soundtrack['tracks']} | {soundtrack['background']['public_path']}
    assert {'/' + p.relative_to(root).as_posix() for p in files if p.is_file() and p.suffix.lower() == '.mp3'} == expected_audio, 'Unexpected MP3 in publication artifact'
    for track in soundtrack['tracks']:
        output = safe_file(root, track['public_path'].lstrip('/'))
        assert sha256(output) == track['sha256'], 'Published soundtrack bytes changed'
        validate_mp3(output, track['tags'])
    assert sha256(root / soundtrack['background']['public_path'].lstrip('/')) == soundtrack['background']['sha256'], 'Published background alias differs'
    evidence = json.loads((root / 'evidence.json').read_text(encoding='utf-8'))
    entries = evidence['entries'] if isinstance(evidence, dict) else evidence
    assert len({e['sha256'] for e in entries}) == len(entries), 'Selected images must not be exact duplicates'
    english_evidence = json.loads((root / 'en/evidence.json').read_text(encoding='utf-8'))
    english_entries = english_evidence['entries'] if isinstance(english_evidence, dict) else english_evidence
    assert [(e['file'], e['sha256']) for e in entries] == [(e['file'], e['sha256']) for e in english_entries], 'Translated gallery must preserve the evidence'
    for original, english in zip(entries, english_entries):
        assert original['title'] != english['title'] and original['caption'] != english['caption'], 'English gallery must be translated'
    print(f'Artifact verified: {len(documents)} HTML files; all local URLs/fragments, metadata, sitemap, RSS and {len(entries)} unique captures valid.')


if __name__ == '__main__':
    verify()
