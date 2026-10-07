"""Validate the actual publication artifact before upload, without network access."""
from __future__ import annotations

import json
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

import build


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
    for path, document in documents.items():
        assert document.h1 == 1, f'{path}: exactly one h1 required'
        assert any(t == 'html' and a.get('lang') == 'pt-BR' for t, a in document.tags)
        assert any(t == 'meta' and a.get('name') == 'description' and a.get('content') for t, a in document.tags)
        assert any(t == 'link' and a.get('rel') == 'canonical' and a.get('href', '').startswith(build.BASE) for t, a in document.tags)
        assert any(t == 'meta' and a.get('property') == 'og:image' for t, a in document.tags)
        raw = path.read_text(encoding='utf-8')
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
        assert not any(t == 'script' and a.get('src', '').startswith('http') for t, a in document.tags), 'No third-party scripts'
    sitemap = ET.parse(root / 'sitemap.xml')
    locations = [element.text for element in sitemap.getroot().iter() if element.tag.endswith('loc')]
    assert len(locations) == len(documents) - 1, 'Sitemap must include every indexable page and omit 404'
    assert len(locations) == len(set(locations)), 'Duplicate sitemap URLs'
    for location in locations:
        assert location.startswith(build.BASE + '/')
    rss = ET.parse(root / 'rss.xml')
    posts = build.load_posts()
    assert len(rss.findall('.//item')) == len(posts), 'RSS must include every published post'
    assert (root / 'robots.txt').read_text().endswith(build.absolute('/sitemap.xml') + '\n')
    assert not any(p.name in ('PROMPTS.md', 'requirements.txt', 'build.py') or 'design-references' in p.parts for p in files), 'Build-only materials leaked to artifact'
    assert not any(p.suffix.lower() in ('.mpq', '.sv', '.cel', '.cl2', '.min', '.til', '.sol', '.obj', '.glb', '.gltf') for p in files), 'Proprietary/derived game assets must never be uploaded'
    evidence = json.loads((root / 'evidence.json').read_text(encoding='utf-8'))
    entries = evidence['entries'] if isinstance(evidence, dict) else evidence
    assert len({e['sha256'] for e in entries}) == len(entries), 'Selected images must not be exact duplicates'
    print(f'Artifact verified: {len(documents)} HTML files; all local URLs/fragments, metadata, sitemap, RSS and {len(entries)} unique captures valid.')


if __name__ == '__main__':
    verify()
