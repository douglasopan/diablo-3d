"""Publish the selected R2 study media once, directly from the documented source."""
from functools import lru_cache
import hashlib
from html import escape
import json
from pathlib import Path
import re
import shutil

from PIL import Image

ROOT = Path(__file__).resolve().parent
STUDY = ROOT.parent / 'docs/hud-study/r2'
PREFIX = '/assets/studies/hud-r2/'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def selected_file(relative):
    if not re.fullmatch(r'(?:previews|references)/[a-z0-9-]+\.webp|assets/kit-(?:panel|modal|controls)\.png', relative):
        raise ValueError('Unlisted R2 media path')
    path = STUDY / relative
    if path.is_symlink() or not path.resolve().is_relative_to(STUDY.resolve()):
        raise ValueError('R2 media path escapes the study')
    return path


@lru_cache(maxsize=1)
def load_study():
    config = json.loads((ROOT / 'hud-r2-source.json').read_text(encoding='utf-8'))
    for name, key in [('catalog.json', 'catalog_sha256'), ('publication-media.json', 'media_sha256')]:
        if digest(STUDY / name) != config[key]:
            raise ValueError('R2 source changed; review and identify the new snapshot before publishing')
    catalog = json.loads((STUDY / 'catalog.json').read_text(encoding='utf-8'))
    media = json.loads((STUDY / 'publication-media.json').read_text(encoding='utf-8'))
    labels = json.loads((ROOT / 'hud-r2-labels.json').read_text(encoding='utf-8'))
    assert catalog['installed'] is False and catalog['counts']['canonicalScreens'] == 65 and catalog['counts']['studyFrames'] == 223
    assert len(media['assets']) == 74 and not media['failures']
    assert sum(a['kind'] == 'concept' for a in media['assets']) == 60
    assert sum(a['kind'] == 'native-reference' for a in media['assets']) == 14
    files = {}
    for entry in media['assets']:
        relative = entry['output']
        source = selected_file(relative)
        assert entry['lossless'] and entry['decodedRGBAEqual'] and entry['sourceMasterUnchanged']
        assert not any(entry[key] for key in ('resize', 'crop', 'retouch'))
        assert source.stat().st_size == entry['outputBytes'] and digest(source) == entry['outputSha256']
        with Image.open(source) as im:
            im.load()
            assert list(im.size) == entry['dimensions']
            assert hashlib.sha256(im.convert('RGBA').tobytes()).hexdigest() == entry['outputDecodedRGBASHA256']
        files[PREFIX + relative] = {'source': source, 'sha256': entry['outputSha256'], 'dimensions': entry['dimensions']}
    assets = []
    for asset in catalog['assets']:
        relative = asset['path']
        if asset['id'] == 'f03-menu-principal-r1':
            assert relative == '../images/06-menu-principal-preview.jpg'
            source = STUDY.parent / 'images/06-menu-principal-preview.jpg'
            public = PREFIX + 'menu-r1.jpg'
            kind = 'reused-concept'
        else:
            source = selected_file(relative)
            public = PREFIX + relative
            kind = 'component' if relative.startswith('assets/') else 'concept'
        assert not source.is_symlink() and digest(source) == asset['sha256']
        with Image.open(source) as im:
            im.load()
            assert list(im.size) == asset['dimensions']
            if kind == 'component':
                assert im.convert('RGBA').getchannel('A').getextrema() == (0, 255)
        files[public] = {'source': source, 'sha256': asset['sha256'], 'dimensions': asset['dimensions']}
        assets.append(dict(id=asset['id'], kind=kind, file=public, sha256=asset['sha256'], dimensions=asset['dimensions'],
                           titles=labels['assetTitles'][asset['id']], prompt=asset.get('prompt', ''),
                           screenIds=[s['screenId'] for s in catalog['screens'] if asset['id'] in s['assetIds']], coveredStateIds=asset['coveredIds'],
                           sourceManifest=asset['sourceManifest'], status='candidate' if kind != 'reused-concept' else 'reused-r1'))
    assert len(assets) == 64 and len(files) == 78
    assert all(asset['screenIds'] for asset in assets if asset['kind'] != 'component')
    assert set(labels['assetTitles']) == {a['id'] for a in assets}
    assert set(labels['screenTitles']) == {s['screenId'] for s in catalog['screens']}
    assert set(labels['referenceNotes']) == {s['screenId'] for s in catalog['screens'] if s['reference'].get('path')}
    for group in labels.values():
        assert all(set(value) == {'pt-BR', 'en'} and all(value.values()) for value in group.values())
    screens = []
    for screen in catalog['screens']:
        ref = screen['reference']
        public_ref = None
        if ref.get('path'):
            assert PREFIX + ref['path'] in files
            public_ref = dict(file=PREFIX + ref['path'], type=ref['type'], notes=labels['referenceNotes'][screen['screenId']],
                              coveredStateIds=ref['coveredFrameIds'], allVariants=ref['allVariants'])
        screens.append(dict(id=screen['screenId'], titles=labels['screenTitles'][screen['screenId']], family=screen['family'],
                            stateIds=[v['studyFrameId'] for v in screen['variants']], assetIds=screen['assetIds'], reference=public_ref))
    public_catalog = dict(revision='hud-study-r2', date=catalog['date'], installed=False, status='candidate-study',
                          sourceCommit=config['source_commit'], counts=dict(archetypes=65, trackedStates=223, newConcepts=60,
                          components=3, reusedConcepts=1, nativeReferences=14, missingNativeReferences=51),
                          assets=assets, screens=screens)
    return dict(files=files, catalog=public_catalog)


def image_source(path):
    return load_study()['files'].get(path, {}).get('source') if path.startswith(PREFIX) else None


def copy_study(output):
    study = load_study()
    for public, entry in study['files'].items():
        target = output / public.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(entry['source'], target)
    target = output / (PREFIX + 'catalog.json').lstrip('/')
    target.write_text(json.dumps(study['catalog'], ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def gallery(language, url):
    catalog = load_study()['catalog']
    en = language == 'en'
    text = lambda pt, eng: eng if en else pt
    esc = lambda value: escape(str(value), quote=True)
    groups = [('frontend', text('Entrada, heróis e carregamento', 'Entry, heroes and loading'), 'frontend-art.json'),
              ('settings', text('Configurações e atalhos', 'Settings and bindings'), 'settings-art.json'),
              ('panels', text('Painéis e controles', 'Panels and controls'), 'panels-art.json'),
              ('commerce', text('Diálogo e comércio', 'Dialogue and trade'), 'commerce-art.json'),
              ('components', text('Peças sem texto', 'Components without text'), 'kit-art.json')]
    parts = ['<div class="study-gallery">', '<nav class="study-nav" aria-label="' + text('Famílias do estudo', 'Study families') + '">']
    parts.extend(f'<a href="#r2-{key}">{esc(title)}</a>' for key, title, _ in groups)
    parts.append(f'<a href="#r2-references">{text("Referências nativas", "Native references")}</a></nav>')
    def figure(file, title, note, extra=''):
        width, height = load_study()['files'][file]['dimensions']
        caption = title + ' — ' + note
        return f'<figure class="gallery-item"><a href="{esc(url(file))}" data-lightbox data-caption="{esc(caption)}"><img src="{esc(url(file))}" alt="{esc(caption)}" width="{width}" height="{height}" loading="lazy" decoding="async"><span class="image-open" aria-hidden="true">{text("Ampliar", "Enlarge")} ↗</span></a><figcaption><h4>{esc(title)}</h4><p>{esc(note)}</p>{extra}</figcaption></figure>'
    for key, title, manifest in groups:
        members = [a for a in catalog['assets'] if a['sourceManifest'] == manifest]
        parts.append(f'<section id="r2-{key}"><h3>{esc(title)} <span class="meta">· {len(members)}</span></h3><div class="gallery-grid">')
        for asset in members:
            note = text('Conceito gerado · R2 · candidato em revisão; não é captura do jogo.', 'Generated concept · R2 · candidate under review; not a game capture.')
            if asset['kind'] == 'component':
                note = text('Peça candidata sem texto, com alfa exterior; ainda exige revisão e montagem.', 'Candidate component without text, with exterior alpha; review and assembly are still needed.')
            elif asset['kind'] == 'reused-concept':
                note = text('Direção anterior R1 reutilizada; estudo visual, não captura de partida.', 'Earlier R1 direction reused; visual study, not a gameplay capture.')
            screen_ids = ', '.join(asset['screenIds']) or text('Componente compartilhado', 'Shared component')
            extra = f'<p class="meta">{text("Telas vinculadas", "Linked screens")}: {esc(screen_ids)}</p>'
            if asset['id'] in ('p08-stash', 'c-05-commerce-grid'):
                warning = text('A grade desenhada diverge do contrato nativo. Correção pendente antes da integração.', 'The drawn grid differs from the native contract. Correction is required before integration.')
                extra += f'<p class="study-warning">{esc(warning)}</p>'
            if asset['prompt']:
                extra += f'<details class="study-prompt"><summary>{text("Ver prompt original", "View original prompt")}</summary><pre><code>{esc(asset["prompt"])}</code></pre></details>'
            parts.append(figure(asset['file'], asset['titles'][language], note, extra))
        parts.append('</div></section>')
    parts.append(f'<section id="r2-references"><h3>{text("14 referências nativas completas", "14 complete native references")}</h3><p>{text("Uma captura histórica de janela e 13 composições offscreen. As outras 51 telas ainda não têm imagem nativa; uma referência não cobre automaticamente todas as variantes.", "One historical window capture and 13 offscreen compositions. The other 51 screens still have no native image; one reference does not automatically cover every variant.")}</p><div class="gallery-grid">')
    for screen in catalog['screens']:
        ref = screen['reference']
        if ref:
            kind = text('Referência histórica de janela', 'Historical window reference') if ref['type'] == 'realwindow' else text('Referência nativa offscreen', 'Native offscreen reference')
            parts.append(figure(ref['file'], screen['titles'][language], kind + '. ' + ref['notes'][language]))
    parts.append('</div></section><p class="meta"><a href="' + esc(url(PREFIX + 'catalog.json')) + '">' + text('Catálogo público: prompts, telas, estados e procedência', 'Public catalog: prompts, screens, states and provenance') + ' ↗</a></p></div>')
    return ''.join(parts)
