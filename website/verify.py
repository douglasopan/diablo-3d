"""Validate the actual publication artifact before upload, without network access."""
from __future__ import annotations

import json
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

import build
from community import DISCORD_URL, YOUTUBE_URL
from soundtrack import load_soundtrack, safe_file, sha256, validate_mp3
from music_credits import COMPOSER, PRODUCER, SOURCE, credit_text, public_soundtrack
from videos import SHOWCASE_VIDEO_IDS, VIDEO_IDS


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
        if logical != '/musica/':
            assert any(t == 'a' and a.get('class') == 'music-credit' and a.get('href') == build.PREFIX + ('/en' if language == 'en' else '') + '/musica/#music-credits' and a.get('title') == credit_text(language) for t, a in document.tags), f'{path}: background player needs localized music credits'
        invitations = [a for t, a in document.tags if t == 'dialog' and a.get('id') == 'community-invite']
        assert len(invitations) == 1 and 'open' not in invitations[0], f'{path}: one initially closed community invitation required'
        assert invitations[0].get('aria-labelledby') == 'community-invite-title' and invitations[0].get('aria-describedby') == 'community-invite-text'
        launchers = [a for t, a in document.tags if t == 'button' and 'data-community-open' in a]
        assert len(launchers) == 1 and launchers[0].get('aria-controls') == 'community-invite'
        for destination in (DISCORD_URL, YOUTUBE_URL):
            assert any(t == 'a' and a.get('href') == destination and 'data-community-visit' in a for t, a in document.tags), f'{path}: missing community destination'
        assert sum(t == 'script' and urlsplit(a.get('src', '')).path == build.PREFIX + '/community.js' and 'defer' in a for t, a in document.tags) == 1
        assert not any(t == 'iframe' for t, a in document.tags), f'{path}: embedded videos must wait for a visitor click'
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
    study = build.load_study()
    study_root = root / 'assets/studies/hud-r2'
    expected_study_files = {root / public.lstrip('/') for public in study['files']} | {study_root / 'catalog.json'}
    assert {p for p in study_root.rglob('*') if p.is_file()} == expected_study_files, 'Only selected R2 media and the public catalog may be copied'
    for public, entry in study['files'].items():
        assert sha256(root / public.lstrip('/')) == entry['sha256'], 'Published R2 media bytes changed'
    assert json.loads((study_root / 'catalog.json').read_text(encoding='utf-8')) == study['catalog']
    for language in ('', 'en/'):
        article = documents[root / (language + 'devlog/interfaces-diablo-r2/index.html')]
        media = [a for t, a in article.tags if t == 'a' and 'data-lightbox' in a]
        assert len(media) == 78 and len({a['href'] for a in media}) == 78, 'The R2 gallery must include all selected art and native references once'
        assert {a['href'] for a in media} == {build.PREFIX + public for public in study['files']}
        study_images = [a for t, a in article.tags if t == 'img' and a.get('src', '').startswith(build.PREFIX + '/assets/studies/hud-r2/')]
        assert len(study_images) == 79 and sum(a.get('loading') == 'lazy' for a in study_images) == 78 and sum(a.get('loading') == 'eager' for a in study_images) == 1, 'All gallery images must load lazily; only the article hero is eager'
    for language in ('', 'en/'):
        for route, expected_ids in (('index.html', [*SHOWCASE_VIDEO_IDS.values(), VIDEO_IDS['menu-rock2']]), ('musica/index.html', list(VIDEO_IDS.values())), ('devlog/interfaces-diablo-r2/index.html', list(SHOWCASE_VIDEO_IDS.values()))):
            video_page = documents[root / (language + route)]
            buttons = [a for t, a in video_page.tags if t == 'button' and 'data-youtube-id' in a]
            assert [a['data-youtube-id'] for a in buttons] == expected_ids, 'Only the confirmed public videos may be offered'
            assert all('hidden' in a and a.get('aria-controls') in video_page.ids for a in buttons), 'On-demand buttons require a real local target and progressive enhancement'
            assert sum(t == 'script' and urlsplit(a.get('src', '')).path == build.PREFIX + '/youtube-videos.js' and 'defer' in a for t, a in video_page.tags) == 1
        locale = 'en' if language else 'pt-BR'
        published_manifest = json.loads((root / (language + 'soundtrack.json')).read_text(encoding='utf-8'))
        assert published_manifest == public_soundtrack(soundtrack, locale), 'Published soundtrack credits or validated audio manifest differ'
        library = documents[root / (language + 'musica/index.html')]
        raw_library = (root / (language + 'musica/index.html')).read_text(encoding='utf-8')
        assert 'music-credits' in library.ids and SOURCE in raw_library
        import re
        visible_library = re.sub(r'<script\b[^>]*>.*?</script>', '', raw_library, flags=re.S)
        assert visible_library.count(credit_text(locale)) == 12, 'Player, five track rows and five video cards need complete visible music credits'
        structured = json.loads(re.search(r'<script type="application/ld\+json">(.*?)</script>', raw_library, re.S).group(1))
        album = next(entry for entry in structured if entry.get('@type') == 'MusicAlbum')
        assert album['numTracks'] == len(album['track']) == len(soundtrack['tracks']) and album['creditText'] == credit_text(locale)
        for recording, track in zip(album['track'], soundtrack['tracks']):
            assert recording['recordingOf']['composer']['name'] == COMPOSER, 'Original composition must retain Matt Uelmen'
            assert recording['producer']['name'] == PRODUCER and recording['creditText'] == credit_text(locale), 'Recording production must name Douglas Pan and AI assistance'
            assert recording['audio']['creditText'] == credit_text(locale)
            assert recording['audio']['contentUrl'] == build.BASE + track['public_path']
            assert recording['recordingOf']['citation'] == SOURCE
            if track['environment'] == 'Menu':
                assert 'name' not in recording['recordingOf'], 'Do not guess an official composition title for Main Menu'
        home_raw = (root / (language + 'index.html')).read_text(encoding='utf-8')
        assert credit_text(locale) in home_raw, 'Featured music card needs original and production credits'
        assert sum(t == 'audio' for t, a in library.tags) == 1, 'Library must have one player'
        assert any(t == 'audio' and a.get('id') == 'soundtrack-player' and 'autoplay' not in a for t, a in library.tags)
        downloads = [a for t, a in library.tags if t == 'a' and 'download' in a]
        assert len(downloads) == len(soundtrack['tracks']), 'Every custom track needs a free direct download'
        expected_youtube = [build.SOUNDTRACK_YOUTUBE[track['id']] for track in soundtrack['tracks'] if track['id'] in build.SOUNDTRACK_YOUTUBE]
        youtube_links = [a.get('href') for t, a in library.tags if t == 'a' and 'data-soundtrack-youtube' in a]
        assert sorted(youtube_links) == sorted(expected_youtube), 'Soundtrack YouTube links must match each custom track once'
        assert {key: 'https://www.youtube.com/watch?v=' + value for key, value in VIDEO_IDS.items()} == build.SOUNDTRACK_YOUTUBE, 'Video cards and soundtrack links must share confirmed IDs'
        assert any(t == 'a' and a.get('href') == build.SOUNDTRACK_YOUTUBE_PLAYLIST for t, a in library.tags)
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
