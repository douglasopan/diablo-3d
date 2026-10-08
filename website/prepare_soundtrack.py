"""Create credited public copies from custom runtime assets, preserving inputs."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from mutagen.id3 import ID3, TALB, TIT2, TPE1, TPE2, TXXX, Encoding

from soundtrack import (ALBUM, ARTIST, BACKGROUND, CATALOG, ROOT, catalog_sha256,
                        duration_label, engine_tracks, expected_tags, load_soundtrack,
                        safe_file, sha256, validate_mp3, validate_public_allowlist)


def _run(tool, args, label):
    executable = shutil.which(tool)
    if not executable:
        raise ValueError(tool + ' is required to prepare the public soundtrack')
    result = subprocess.run([executable, *args], capture_output=True)
    if result.returncode:
        # Source metadata and absolute local paths must never reach receipts/logs.
        raise ValueError(tool + ' audio verification failed: ' + label)
    return result.stdout


def packet_fingerprint(path):
    data = json.loads(_run('ffprobe', ['-v', 'error', '-select_streams', 'a:0',
        '-show_packets', '-show_entries', 'packet=size,data_hash',
        '-show_data_hash', 'sha256', '-of', 'json', str(path)], Path(path).name))
    raw_packets = data.get('packets', [])
    if not raw_packets or any('size' not in packet or 'data_hash' not in packet for packet in raw_packets):
        raise ValueError('Cannot verify MP3 audio packets')
    # ffprobe may include gapless skip-sample side data; compare encoded bytes only.
    packets = [dict(size=packet['size'], data_hash=packet['data_hash']) for packet in raw_packets]
    canonical = json.dumps(packets, sort_keys=True, separators=(',', ':')).encode('ascii')
    return dict(sha256=hashlib.sha256(canonical).hexdigest(), packet_count=len(packets),
                audio_bytes=sum(int(packet['size']) for packet in packets))


def independent_verification(path, tags):
    probe = json.loads(_run('ffprobe', ['-v', 'error', '-show_entries',
        'stream=codec_name,codec_type,sample_rate,channels:format=duration:format_tags',
        '-of', 'json', str(path)], Path(path).name))
    streams = probe.get('streams', [])
    if len(streams) != 1 or streams[0].get('codec_type') != 'audio' or streams[0].get('codec_name') != 'mp3':
        raise ValueError('Public soundtrack must contain audio only, without covers')
    actual_tags = {key.lower(): value for key, value in probe.get('format', {}).get('tags', {}).items()}
    if actual_tags != tags:
        raise ValueError('Independent ffprobe attribution verification failed')
    _run('ffmpeg', ['-v', 'error', '-xerror', '-nostdin', '-i', str(path),
        '-map', '0:a:0', '-f', 'null', '-'], Path(path).name)
    return dict(codec='mp3', sample_rate=int(streams[0]['sample_rate']),
                channels=streams[0]['channels'], stream_count=1, tags=actual_tags,
                full_decode_passed=True)


def make_copy(source, target, tags):
    target.parent.mkdir(parents=True, exist_ok=True)
    _run('ffmpeg', ['-v', 'error', '-nostdin', '-i', str(source), '-map', '0:a:0',
        '-c:a', 'copy', '-map_metadata', '-1', '-map_chapters', '-1',
        '-id3v2_version', '0', '-write_id3v1', '0', '-y', str(target)], source.name)
    credits = ID3()
    for frame, value in ((TPE1, tags['artist']), (TPE2, tags['album_artist']),
                         (TIT2, tags['title']), (TALB, tags['album'])):
        credits.add(frame(encoding=Encoding.UTF16, text=[value]))
    credits.add(TXXX(encoding=Encoding.UTF16, desc='AUTHOR', text=[tags['author']]))
    credits.save(target, v1=0, v2_version=3, padding=lambda _: 1024)


def prepare(assets_root=None, site_root=None, catalog_path=None, receipt_path=None):
    assets_root = Path(assets_root or ROOT.parent / 'assets').resolve()
    site_root = Path(site_root or ROOT).resolve()
    definitions = engine_tracks(catalog_path)
    available = [(track, safe_file(assets_root, track['game_asset'])) for track in definitions]
    available = [(track, source) for track, source in available if source.is_file()]
    if not available:
        raise ValueError('No custom engine MP3s are available in --assets-root')
    original_hashes = {track['game_asset']: sha256(source) for track, source in available}
    allowed = {track['public_path'] for track, _ in available}
    has_third = any(track['environment'] == 'Town' and track['variant_id'] == 4 for track, _ in available)
    if has_third:
        allowed.add(BACKGROUND)
    validate_public_allowlist(site_root / 'public', allowed)
    receipt_path = Path(receipt_path or ROOT.parent.parent / 'diagnostics/soundtrack-publication/receipt.json').resolve()
    active_catalog = Path(catalog_path or CATALOG).resolve()
    if (receipt_path.suffix.lower() != '.json' or receipt_path.is_relative_to(site_root)
            or receipt_path.is_relative_to(assets_root) or receipt_path == active_catalog):
        raise ValueError('Verification receipts need a .json path outside engine assets, catalog and website')
    site_root.mkdir(parents=True, exist_ok=True)
    # No public file is replaced until all five copies and source hashes pass.
    with tempfile.TemporaryDirectory(prefix='.soundtrack-', dir=site_root) as temporary:
        stage = Path(temporary).resolve()
        if not stage.is_relative_to(site_root):
            raise ValueError('Soundtrack staging escaped the website directory')
        tracks, checks = [], []
        for definition, source in available:
            tags = expected_tags(definition)
            target = safe_file(stage / 'public', definition['public_path'][1:])
            before_packets = packet_fingerprint(source)
            make_copy(source, target, tags)
            actual = validate_mp3(target, tags)
            after_packets = packet_fingerprint(target)
            if before_packets != after_packets:
                raise ValueError('Audio packets changed while preparing ' + source.name)
            independent = independent_verification(target, tags)
            track = dict(definition, duration_seconds=actual['duration_seconds'],
                         duration_label=duration_label(actual['duration_seconds']),
                         size_bytes=actual['size_bytes'], sha256=actual['sha256'],
                         source_sha256=original_hashes[definition['game_asset']], tags=tags)
            tracks.append(track)
            checks.append(dict(game_asset=track['game_asset'], public_path=track['public_path'],
                               sha256=track['sha256'], source_sha256=track['source_sha256'],
                               audio_packets=after_packets, audio_packets_preserved=True,
                               id3_whitelist_passed=True, **independent))
        background = None
        third = next((track for track in tracks if track['environment'] == 'Town' and track['variant_id'] == 4), None)
        if third is not None:
            alias = safe_file(stage / 'public', BACKGROUND[1:])
            alias.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(safe_file(stage / 'public', third['public_path'][1:]), alias)
            validate_mp3(alias, third['tags'])
            background = dict(public_path=BACKGROUND, track_id=third['id'],
                              sha256=third['sha256'], size_bytes=third['size_bytes'])
        manifest = dict(schema_version=1, artist=ARTIST, album=ALBUM,
                        catalog_sha256=catalog_sha256(definitions), tracks=tracks,
                        background=background)
        (stage / 'soundtrack.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
        load_soundtrack(stage, catalog_path, assets_root)
        for track, source in available:
            if sha256(source) != original_hashes[track['game_asset']]:
                raise ValueError('Custom source preservation check failed')
        receipt = dict(schema_version=1,
                       verified_at_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                       catalog_sha256=manifest['catalog_sha256'], sources_unchanged=True,
                       tracks=checks, background=background)
        destination = safe_file(site_root / 'public', 'assets/audio/soundtrack')
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest_destination = site_root / 'soundtrack.json'
        alias_destination = safe_file(site_root / 'public', BACKGROUND[1:])
        previous_manifest = stage / 'previous-soundtrack.json'
        previous_alias = stage / 'previous-background.mp3'
        if manifest_destination.exists():
            shutil.copyfile(manifest_destination, previous_manifest)
        if alias_destination.exists():
            shutil.copyfile(alias_destination, previous_alias)
        if destination.exists():
            # The previous directory stays recoverable until promotion completes.
            destination.rename(stage / 'previous-soundtrack')
        try:
            (stage / 'public/assets/audio/soundtrack').rename(destination)
            if background:
                os.replace(stage / 'public' / BACKGROUND[1:], alias_destination)
            os.replace(stage / 'soundtrack.json', manifest_destination)
            load_soundtrack(site_root, catalog_path, assets_root)
        except BaseException:
            if destination.exists():
                destination.rename(stage / 'failed-soundtrack')
            previous = stage / 'previous-soundtrack'
            if previous.exists():
                previous.rename(destination)
            for backup, original in ((previous_manifest, manifest_destination), (previous_alias, alias_destination)):
                if backup.exists():
                    shutil.copyfile(backup, original)
                elif original.exists():
                    original.unlink()
            raise
        receipt_path.parent.mkdir(parents=True, exist_ok=True)
        receipt_path.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-root', type=Path, default=ROOT.parent / 'assets', help='Engine assets directory; only declared custom MP3s are read')
    parser.add_argument('--site-root', type=Path, default=ROOT)
    parser.add_argument('--catalog', type=Path, default=CATALOG)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--check', action='store_true', help='Validate published credits, hashes and available custom sources without writing')
    args = parser.parse_args()
    try:
        if args.check:
            manifest = load_soundtrack(args.site_root, args.catalog, args.assets_root)
            print(f"Verified {len(manifest['tracks'])} credited custom soundtrack files.")
        else:
            manifest = prepare(args.assets_root, args.site_root, args.catalog, args.receipt)
            print(f"Prepared {len(manifest['tracks'])} custom tracks; audio packets and source hashes preserved.")
    except (ValueError, OSError) as exc:
        parser.exit(1, 'Soundtrack preparation failed: ' + str(exc) + '\n')


if __name__ == '__main__':
    main()
