"""Rebuild the visual-study index without touching production UI or private media."""
import hashlib
import json
import os
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
WORK = REPO.parent
PRIVATE = WORK / 'diagnostics/hud-all-screens-20261008/screen-inventory.json'

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

def relative(path):
    return os.path.relpath(path, HERE).replace('\\', '/')

def source_file(value):
    path = Path(value)
    if path.is_absolute():
        try:
            return path.relative_to(REPO).as_posix()
        except ValueError:
            raise ValueError('Source reference is outside the repository')
    return value.replace('\\', '/')

inventory = read(PRIVATE if PRIVATE.exists() else HERE / 'screen-inventory.json')
screens = inventory['screens']
frame_map = inventory['studyFrameToCanonicalScreen']
if isinstance(frame_map, list):
    frame_map = {x['studyFrameId']: x['screenId'] for x in frame_map}
media_path = HERE / 'publication-media.json'
media = read(media_path) if media_path.exists() else {}
media_entries = media.get('assets', media.get('outputs', media.get('entries', [])))
publication = {entry['id']: entry for entry in media_entries if entry.get('kind') == 'concept'}
assets = []
masters_verified = 0
for name in ('settings-art.json', 'frontend-art.json', 'panels-art.json', 'commerce-art.json', 'kit-art.json'):
    path = HERE / name
    if not path.exists():
        continue
    manifest = read(path)
    entries = next((manifest[k] for k in ('outputs', 'assets', 'images', 'artifacts') if isinstance(manifest.get(k), list)), [])
    for entry in entries:
        output = entry.get('output') or entry.get('file') or entry.get('path')
        if not output:
            continue
        asset_id = entry.get('id') or entry.get('slug') or Path(output).stem
        local = Path(output)
        if not local.is_absolute():
            local = HERE / output
        if not local.exists():
            local = HERE / 'images' / Path(output).name
        master_exists = local.exists()
        published = publication.get(asset_id)
        preview = HERE / published['output'] if published else local
        if not master_exists and not published:
            raise ValueError(f'Missing generated file: {output}')
        claimed = entry.get('sha256')
        master_sha = hashlib.sha256(local.read_bytes()).hexdigest() if master_exists else published['originalSha256']
        if claimed and claimed.lower() != master_sha:
            raise ValueError(f'Master hash mismatch: {output}')
        if master_exists:
            masters_verified += 1
        sha = hashlib.sha256(preview.read_bytes()).hexdigest()
        if published and (sha != published['outputSha256'] or master_sha != published['originalSha256'] or not published['decodedRGBAEqual']):
            raise ValueError(f'Publication identity mismatch: {asset_id}')
        with Image.open(preview) as im:
            size = list(im.size)
            alpha = im.getchannel('A').getextrema() if 'A' in im.getbands() else None
        ids = entry.get('canonicalIds', [])
        covered = entry.get('coveredIds', [])
        canon = set(ids)
        for fid in covered:
            mapped = frame_map.get(fid)
            if isinstance(mapped, str):
                canon.add(mapped)
            elif isinstance(mapped, dict):
                canon.add(mapped.get('screenId', ''))
        asset = {k: entry[k] for k in ('title', 'prompt', 'status', 'inspection', 'regions', 'normalizedRegions', 'promotionBlocked', 'issues', 'notes', 'reviewNotes', 'selected', 'superseded', 'revision', 'usage', 'directlyIllustratedIds', 'nativeTextMapping') if k in entry}
        if entry.get('inspection', {}).get('promotionBlocked'):
            asset['promotionBlocked'] = True
        is_reused = 'reused' in str(entry.get('status', '')).lower() or entry.get('generated') is False
        asset.update(id=asset_id, path=relative(preview), sha256=sha,
                     dimensions=size, bytes=preview.stat().st_size, canonicalIds=sorted(canon), coveredIds=covered,
                     tool='existing-study' if is_reused else 'image_gen built-in', installed=False, sourceManifest=name, alphaExtrema=alpha)
        if published:
            asset.update(masterPath=published['sourceRelativeRepoPath'], masterSha256=master_sha,
                         publicationEncoding='lossless-webp', decodedRGBAEqual=True)
        assets.append(asset)

reused = [
    {'id': 'reused-main-menu-r1', 'path': '../images/06-menu-principal-preview.jpg', 'canonicalIds': ['F03'], 'coveredIds': ['F03'], 'title': 'Menu principal — direção anterior preservada', 'status': 'reused-reference', 'tool': 'existing-study', 'installed': False},
]
for a in reused:
    if any(a['path'] == old['path'] for old in assets):
        continue
    p = HERE / a['path']
    a['sha256'] = hashlib.sha256(p.read_bytes()).hexdigest()
    with Image.open(p) as im:
        a['dimensions'] = list(im.size)
    assets.append(a)

catalog_screens = []
for item in screens:
    s = dict(item)
    s['code'] = [{**c, 'file': source_file(c.get('file', ''))} for c in s.get('code', [])] if isinstance(s.get('code'), list) else s.get('code')
    ref = Path(s['originalReferencePath'])
    if not ref.is_absolute():
        ref = HERE / ref
    public_ref = HERE / 'references' / (s['screenId'].lower() + '.webp')
    if public_ref.exists():
        ref = public_ref
    s['reference'] = {
        'type': s['originalReferenceType'], 'ready': s['originalComparisonReady'],
        'path': relative(ref) if s['originalComparisonReady'] else None,
        'reason': s['originalReferenceReason'],
        'coverage': s.get('referenceCoverageScope'),
        'coveredFrameIds': s.get('originalReferenceCoveredStudyFrameIds', []),
        'allVariants': s.get('allFunctionalVariantsHaveOriginalImage', False),
    }
    for key in ('originalReferencePath', 'originalReferenceType', 'originalReferenceReason'):
        s.pop(key, None)
    s['assetIds'] = [a['id'] for a in assets if s['screenId'] in a['canonicalIds']]
    catalog_screens.append(s)

missing_art = [s['screenId'] for s in catalog_screens if s['artStatus'] == 'needs-art' and not s['assetIds']]
data = {
    'revision': 'hud-study-r2', 'date': '2026-10-08', 'status': 'candidate-study', 'installed': False,
    'counts': {'canonicalScreens': len(screens), 'studyFrames': len(frame_map),
               'nativeReferences': sum(bool(s['reference']['ready']) for s in catalog_screens),
               'missingNativeReferences': sum(not s['reference']['ready'] for s in catalog_screens),
               'generatedImages': len([a for a in assets if a['tool'] == 'image_gen built-in']),
               'screensWithArt': sum(bool(s['assetIds']) for s in catalog_screens), 'missingArt': missing_art},
    'limitations': [
        'Artes e pranchas são conceitos gerados; não representam telas implementadas.',
        'Referências nativas existentes são capturas históricas ou fixtures offscreen; não equivalem todas ao Diablo vanilla nem à janela atual.',
        'Uma referência de um arquétipo não comprova comparação de todas as suas variantes.',
        'Onde falta captura nativa, a galeria mostra o contrato do código e identifica a lacuna; não inventa o original.',
        'Textos, grids e input finais devem ser nós/código vivo. Letras ou células incorretas na ilustração bloqueiam sua promoção direta.',
        'Dimensões reais dos PNGs são registradas; pedidos Full HD/4K não são garantias de resolução devolvida pela ferramenta.',
    ],
    'screens': catalog_screens, 'assets': assets, 'frameToScreen': frame_map,
    'corrections': [{**item, 'code': [source_file(path) for path in item.get('code', [])]}
                    for item in inventory.get('currentSourceCorrections', [])],
}
write(HERE / 'catalog.json', data)
(HERE / 'catalog.js').write_text('window.HUD_STUDY = ' + json.dumps(data, ensure_ascii=False) + ';\n', encoding='utf-8', newline='\n')
write(HERE / 'validation.json', {'counts': data['counts'], 'missingArt': missing_art,
      'displayHashesVerified': len(assets), 'localMasterHashesVerified': masters_verified,
      'nativeMediaCopied': (HERE / 'references').exists(), 'productionChanged': False,
      'originalComparisonsComplete': False, 'godotStructuralChecks': 819,
      'godotVisualReviewExecuted': False})
print(json.dumps(data['counts'], ensure_ascii=False))
