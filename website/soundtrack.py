"""Publication contract for the engine's custom soundtrack, never Vanilla audio."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess

from mutagen.id3 import ID3, Encoding
from mutagen.mp3 import MP3
from mutagen import MutagenError

ROOT = Path(__file__).resolve().parent
CATALOG = ROOT.parent / 'Source/engine/music_catalog.hpp'
ARTIST = 'Douglas Pan'
ALBUM = 'Diablo 3D — Custom Soundtrack'
BACKGROUND = '/assets/audio/background-music.mp3'
PUBLIC_PREFIX = '/assets/audio/soundtrack/'
TAG_KEYS = {'artist', 'album_artist', 'author', 'title', 'album'}
TRACK_KEYS = {'id', 'music_id', 'environment', 'variant_id', 'variant',
              'game_asset', 'public_path', 'title', 'duration_seconds',
              'duration_label', 'size_bytes', 'sha256', 'source_sha256', 'tags'}
HASH = re.compile(r'[0-9a-f]{64}\Z')


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_file(root, relative):
    """Reject lexical escapes and symlinks before accessing a declared asset."""
    if not isinstance(relative, str) or '\\' in relative or ':' in relative:
        raise ValueError('Soundtrack path must be a relative POSIX path')
    part = PurePosixPath(relative)
    if part.is_absolute() or not relative or any(p in ('', '.', '..') for p in relative.split('/')):
        raise ValueError('Soundtrack path traversal is not allowed')
    root = Path(root).resolve()
    candidate = root.joinpath(*part.parts)
    if not candidate.resolve().is_relative_to(root):
        raise ValueError('Soundtrack path escapes its asset directory')
    return candidate


def engine_tracks(catalog_path=None):
    """Parse only custom columns in MusicTrackCatalog, retaining native row IDs."""
    source = Path(catalog_path or CATALOG).read_text(encoding='utf-8')
    match = re.search(r'\bMusicTrackCatalog\s*\{\s*\{(.*?)\}\s*\};', source, re.S)
    if not match:
        raise ValueError('Cannot read engine MusicTrackCatalog')
    rows = re.findall(r'\{((?:"(?:\\.|[^"\\])*"|[^{}])*)\}', match.group(1))
    if not rows:
        raise ValueError('Engine MusicTrackCatalog has no rows')
    result, paths, keys = [], set(), set()
    for music_id, row in enumerate(rows):
        literals = re.findall(r'"(?:\\.|[^"\\])*"', row)
        try:
            fields = [json.loads(literal) for literal in literals]
        except ValueError as exc:
            raise ValueError('Unsupported engine soundtrack string literal') from exc
        if len(fields) not in (7, 8):
            raise ValueError('Engine soundtrack columns changed; review the publisher')
        key, name = fields[:2]
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', key) or key in keys:
            raise ValueError('Invalid or duplicate engine soundtrack environment')
        keys.add(key)
        for index, variant_id, variant in ((5, 1, 'rock'), (6, 2, 'alternative'), (7, 4, 'third')):
            value = fields[index] if index < len(fields) else ''
            if not value:
                continue
            game_asset = value.replace('\\', '/')
            if not re.fullmatch(r'music/d3d/[a-z][a-z0-9-]*\.mp3', game_asset):
                raise ValueError('Custom engine music must stay in music/d3d/*.mp3')
            if game_asset in paths:
                raise ValueError('Duplicate custom engine soundtrack path')
            paths.add(game_asset)
            basename = PurePosixPath(game_asset).name
            if key == 'Menu':
                title = {1: 'Main Menu — Rock2', 2: 'Main Menu', 4: 'Main Menu — Third'}[variant_id]
            elif key == 'Town':
                title = f'Tristram { {1: 1, 2: 2, 4: 3}[variant_id] }'
            else:
                title = name + ' — ' + variant.capitalize()
            result.append(dict(id=PurePosixPath(basename).stem, music_id=music_id,
                               environment=key, variant_id=variant_id, variant=variant,
                               game_asset=game_asset, public_path=PUBLIC_PREFIX + basename,
                               title=title))
    if not {'Menu', 'Town'} <= keys:
        raise ValueError('Engine soundtrack must identify Menu and Town')
    return sorted(result, key=lambda track: (track['environment'] != 'Menu',
                                            track['music_id'], track['variant_id']))


def catalog_sha256(definitions):
    canonical = json.dumps(definitions, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(canonical.encode('utf-8')).hexdigest()


def expected_tags(track):
    return dict(artist=ARTIST, album_artist=ARTIST, author=ARTIST,
                title=track['title'], album=ALBUM)


def duration_label(seconds):
    seconds = int(seconds + 0.5)
    return f'{seconds // 60}:{seconds % 60:02}'


def _audio_frames(data, offset):
    """Require a complete Layer III stream, with no appended private tag/blob."""
    count = 0
    rates = (44100, 48000, 32000)
    mpeg1 = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320)
    mpeg2 = (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160)
    while offset < len(data):
        if len(data) - offset < 4:
            raise ValueError('Unexpected data after MP3 audio')
        header = int.from_bytes(data[offset:offset + 4], 'big')
        version, layer = (header >> 19) & 3, (header >> 17) & 3
        bitrate, sample = (header >> 12) & 15, (header >> 10) & 3
        if header >> 21 != 0x7ff or version == 1 or layer != 1 or not 1 <= bitrate <= 14 or sample == 3:
            raise ValueError('Invalid MP3 frame or undeclared trailing metadata')
        rate = rates[sample] // (1 if version == 3 else 2 if version == 2 else 4)
        bits = (mpeg1 if version == 3 else mpeg2)[bitrate] * 1000
        size = (144 if version == 3 else 72) * bits // rate + ((header >> 9) & 1)
        if size < 4 or offset + size > len(data):
            raise ValueError('Truncated MP3 audio frame')
        offset += size
        count += 1
    if count < 2:
        raise ValueError('Soundtrack must contain real MP3 audio frames')


def validate_mp3(path, tags):
    """Check actual ID3v2.3 bytes and decoded MP3 headers, not manifest claims."""
    path = Path(path)
    if not path.is_file():
        raise ValueError('Missing public soundtrack MP3: ' + path.name)
    data = path.read_bytes()
    if len(data) < 10 or data[:6] != b'ID3\x03\x00\x00' or any(b & 0x80 for b in data[6:10]):
        raise ValueError('Soundtrack requires clean ID3v2.3 credits')
    end = 10 + sum(value << shift for value, shift in zip(data[6:10], (21, 14, 7, 0)))
    if end > len(data):
        raise ValueError('Truncated soundtrack ID3 tag')
    cursor, frames = 10, []
    while cursor < end and data[cursor] != 0:
        if cursor + 10 > end:
            raise ValueError('Truncated soundtrack ID3 frame')
        frame, length, flags = data[cursor:cursor + 4], int.from_bytes(data[cursor + 4:cursor + 8], 'big'), data[cursor + 8:cursor + 10]
        if frame not in (b'TPE1', b'TPE2', b'TIT2', b'TALB', b'TXXX') or not length or flags != b'\0\0' or cursor + 10 + length > end:
            raise ValueError('Soundtrack contains a non-whitelisted ID3 frame')
        frames.append(frame)
        cursor += 10 + length
    if any(data[cursor:end]) or sorted(frames) != sorted((b'TPE1', b'TPE2', b'TIT2', b'TALB', b'TXXX')):
        raise ValueError('Soundtrack has extra, duplicate or missing ID3 frames')
    _audio_frames(data, end)
    try:
        actual = ID3(path, load_v1=False)
        audio = MP3(path)
    except (MutagenError, OSError) as exc:
        raise ValueError('Invalid public soundtrack MP3') from exc
    mapping = {'artist': 'TPE1', 'album_artist': 'TPE2', 'author': 'TXXX:AUTHOR',
               'title': 'TIT2', 'album': 'TALB'}
    if set(tags) != TAG_KEYS or set(actual) != set(mapping.values()) or actual.unknown_frames:
        raise ValueError('Soundtrack ID3 credits do not match the whitelist')
    for key, frame_id in mapping.items():
        frame = actual[frame_id]
        if frame.encoding != Encoding.UTF16 or list(frame.text) != [tags[key]]:
            raise ValueError('Soundtrack ID3 ' + key + ' credit is incorrect')
    if audio.info.channels not in (1, 2) or audio.info.sample_rate <= 0 or not math.isfinite(audio.info.length) or audio.info.length <= 0:
        raise ValueError('Invalid soundtrack MP3 audio properties')
    return dict(duration_seconds=round(audio.info.length, 6),
                sample_rate=audio.info.sample_rate, channels=audio.info.channels,
                size_bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def verify_audio_decode(path):
    executable = shutil.which('ffmpeg')
    if executable is None:
        raise ValueError('ffmpeg is required to validate the published soundtrack audio')
    result = subprocess.run([executable, '-v', 'error', '-xerror', '-nostdin',
                             '-i', str(path), '-map', '0:a:0', '-f', 'null', '-'],
                            capture_output=True)
    if result.returncode:
        raise ValueError('Public soundtrack full MP3 decode failed')


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate soundtrack manifest field')
        result[key] = value
    return result


def validate_public_allowlist(public, allowed):
    public = Path(public).resolve()
    for path in public.rglob('*'):
        if path.suffix.lower() == '.mp3':
            relative = '/' + path.relative_to(public).as_posix()
            if relative not in allowed:
                raise ValueError('Unlisted public MP3: ' + relative)
            safe_file(public, relative[1:])


def load_soundtrack(site_root=None, catalog_path=None, assets_root=None):
    """Validate before publishing; source checks are optional for public-only CI."""
    site_root = Path(site_root or ROOT)
    manifest_path = site_root / 'soundtrack.json'
    try:
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'), object_pairs_hook=_object)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError('Missing or invalid soundtrack.json; run prepare_soundtrack.py') from exc
    if not isinstance(manifest, dict) or set(manifest) != {'schema_version', 'artist', 'album', 'catalog_sha256', 'tracks', 'background'}:
        raise ValueError('Unexpected soundtrack manifest fields')
    if type(manifest['schema_version']) is not int or manifest['schema_version'] != 1 or manifest['artist'] != ARTIST or manifest['album'] != ALBUM:
        raise ValueError('Soundtrack manifest version or attribution is incorrect')
    definitions = engine_tracks(catalog_path)
    if manifest['catalog_sha256'] != catalog_sha256(definitions):
        raise ValueError('Engine soundtrack catalog changed; run prepare_soundtrack.py')
    by_asset = {track['game_asset']: track for track in definitions}
    tracks = manifest['tracks']
    if not isinstance(tracks, list) or not tracks:
        raise ValueError('Soundtrack manifest needs available custom tracks')
    public, seen, allowed = site_root / 'public', set(), set()
    for track in tracks:
        if not isinstance(track, dict) or set(track) != TRACK_KEYS:
            raise ValueError('Unexpected soundtrack track fields')
        game_asset = track['game_asset']
        safe_file(site_root, game_asset)
        if game_asset not in by_asset or game_asset in seen:
            raise ValueError('Soundtrack track is not a unique custom engine asset')
        seen.add(game_asset)
        for key, expected in by_asset[game_asset].items():
            if type(track[key]) is not type(expected) or track[key] != expected:
                raise ValueError('Soundtrack engine ' + key + ' mismatch')
        tags = expected_tags(track)
        if track['tags'] != tags:
            raise ValueError('Soundtrack manifest credits are incorrect')
        if not isinstance(track['sha256'], str) or not HASH.fullmatch(track['sha256']) or not isinstance(track['source_sha256'], str) or not HASH.fullmatch(track['source_sha256']):
            raise ValueError('Soundtrack hashes must be SHA-256')
        if type(track['size_bytes']) is not int or track['size_bytes'] <= 0 or type(track['duration_seconds']) not in (int, float) or not math.isfinite(track['duration_seconds']) or track['duration_seconds'] <= 0:
            raise ValueError('Invalid soundtrack size or duration')
        file = safe_file(public, track['public_path'][1:])
        actual = validate_mp3(file, tags)
        if actual['sha256'] != track['sha256'] or actual['size_bytes'] != track['size_bytes']:
            raise ValueError('Public soundtrack hash or size mismatch')
        if abs(actual['duration_seconds'] - track['duration_seconds']) > 0.00001 or track['duration_label'] != duration_label(actual['duration_seconds']):
            raise ValueError('Public soundtrack duration mismatch')
        verify_audio_decode(file)
        allowed.add(track['public_path'])
    order = [track['game_asset'] for track in definitions if track['game_asset'] in seen]
    if [track['game_asset'] for track in tracks] != order:
        raise ValueError('Soundtrack tracks must follow the engine publication order')
    third = next((track for track in tracks if track['environment'] == 'Town' and track['variant_id'] == 4), None)
    background = manifest['background']
    if third is None:
        if background is not None:
            raise ValueError('Background must reference the published Tristram 3 track')
    else:
        expected = dict(public_path=BACKGROUND, track_id=third['id'], sha256=third['sha256'], size_bytes=third['size_bytes'])
        if background != expected:
            raise ValueError('Background alias must match the credited Tristram 3 track')
        alias = safe_file(public, BACKGROUND[1:])
        actual = validate_mp3(alias, third['tags'])
        if actual['sha256'] != third['sha256'] or actual['size_bytes'] != third['size_bytes']:
            raise ValueError('Background alias differs from the credited Tristram 3 track')
        allowed.add(BACKGROUND)
    validate_public_allowlist(public, allowed)
    if assets_root is not None:
        available = set()
        for definition in definitions:
            source = safe_file(assets_root, definition['game_asset'])
            if source.is_file():
                available.add(definition['game_asset'])
        if available != seen:
            raise ValueError('Available custom engine tracks differ from soundtrack.json; run prepare_soundtrack.py')
        for track in tracks:
            # Original provenance remains historical after the exact credited copy
            # is installed in the game. Both accepted identities are byte-exact;
            # the public identity was validated above, including ID3 and decoding.
            accepted_hashes = {track['source_sha256'], track['sha256']}
            if sha256(safe_file(assets_root, track['game_asset'])) not in accepted_hashes:
                raise ValueError('Custom source hash changed; run prepare_soundtrack.py')
    return manifest
