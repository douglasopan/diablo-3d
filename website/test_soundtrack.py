"""Real MP3 fixtures exercise the soundtrack's publication and credit boundary."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from mutagen.id3 import ID3, APIC, TALB, TIT2, TPE1, TPE2, TXXX, Encoding

import prepare_soundtrack
import soundtrack


@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'), 'ffmpeg and ffprobe are required for real MP3 fixtures')
class SoundtrackPublication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = tempfile.TemporaryDirectory()
        cls.base = Path(cls.fixture.name)
        cls.assets = cls.base / 'assets'
        cls.site = cls.base / 'website'
        cls.catalog = cls.base / 'music_catalog.hpp'
        cls.catalog.write_bytes(soundtrack.CATALOG.read_bytes())
        cls.definitions = soundtrack.engine_tracks(cls.catalog)
        sample = cls.base / 'sample.mp3'
        subprocess.run([shutil.which('ffmpeg'), '-v', 'error', '-nostdin', '-f', 'lavfi',
                        '-i', 'sine=frequency=440:duration=0.25', '-ar', '48000', '-ac', '2',
                        '-c:a', 'libmp3lame', '-metadata', 'artist=PRIVATE_FIXTURE_ARTIST',
                        '-metadata', 'comment=PRIVATE_FIXTURE_COMMENT', str(sample)], check=True)
        cls.current = [track for track in cls.definitions if track['environment'] in ('Menu', 'Town')]
        cls.source_hashes = {}
        for track in cls.current:
            target = soundtrack.safe_file(cls.assets, track['game_asset'])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sample, target)
            cls.source_hashes[track['game_asset']] = soundtrack.sha256(target)
        cls.receipt = cls.base / 'receipt.json'
        cls.manifest = prepare_soundtrack.prepare(cls.assets, cls.site, cls.catalog, cls.receipt)

    @classmethod
    def tearDownClass(cls):
        cls.fixture.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.site = self.root / 'website'
        self.assets = self.root / 'assets'
        self.catalog = self.root / 'music_catalog.hpp'
        self.catalog.write_bytes(type(self).catalog.read_bytes())
        shutil.copytree(type(self).site, self.site)
        shutil.copytree(type(self).assets, self.assets)
        self.manifest = copy.deepcopy(type(self).manifest)

    def tearDown(self):
        self.temp.cleanup()

    def save_manifest(self):
        (self.site / 'soundtrack.json').write_text(json.dumps(self.manifest, ensure_ascii=False), encoding='utf-8')

    def file(self, index=0):
        return self.site / 'public' / self.manifest['tracks'][index]['public_path'][1:]

    def load(self, sources=False):
        return soundtrack.load_soundtrack(self.site, self.catalog, self.assets if sources else None)

    def test_prepared_real_audio_has_author_credits_and_preserved_packets(self):
        result = self.load(sources=True)
        self.assertEqual([track['environment'] for track in result['tracks']], ['Menu', 'Menu', 'Town', 'Town', 'Town'])
        self.assertEqual([track['variant_id'] for track in result['tracks']], [1, 2, 1, 2, 4])
        self.assertEqual([track['title'] for track in result['tracks']], ['Main Menu — Rock2', 'Main Menu', 'Tristram 1', 'Tristram 2', 'Tristram 3'])
        for track in result['tracks']:
            self.assertEqual(track['source_sha256'], self.source_hashes[track['game_asset']])
            self.assertEqual(soundtrack.sha256(self.assets / track['game_asset']), self.source_hashes[track['game_asset']])
            self.assertEqual(track['tags']['author'], 'Douglas Pan')
        receipt = json.loads(type(self).receipt.read_text(encoding='utf-8'))
        self.assertTrue(receipt['sources_unchanged'])
        self.assertTrue(all(track['audio_packets_preserved'] and track['full_decode_passed'] for track in receipt['tracks']))
        self.assertNotIn(str(type(self).base), json.dumps(receipt))
        self.assertNotIn('PRIVATE_FIXTURE', json.dumps(receipt))

    def test_uncredited_bare_mp3_rejected(self):
        ID3(self.file()).delete(self.file(), delete_v1=True, delete_v2=True)
        with self.assertRaisesRegex(ValueError, 'ID3v2.3 credits'):
            self.load()

    def test_wrong_artist_album_artist_author_title_and_album_rejected(self):
        for frame in (TPE1(encoding=Encoding.UTF16, text=['Someone']),
                      TPE2(encoding=Encoding.UTF16, text=['Someone']),
                      TXXX(encoding=Encoding.UTF16, desc='AUTHOR', text=['Someone']),
                      TIT2(encoding=Encoding.UTF16, text=['Wrong title']),
                      TALB(encoding=Encoding.UTF16, text=['Other album'])):
            with self.subTest(frame=frame.FrameID):
                original = self.file().read_bytes()
                tags = ID3(self.file())
                tags.add(frame)
                tags.save(self.file(), v2_version=3, v1=0)
                with self.assertRaisesRegex(ValueError, 'credit is incorrect'):
                    self.load()
                self.file().write_bytes(original)

    def test_covers_and_extra_private_frames_rejected(self):
        tags = ID3(self.file())
        tags.add(APIC(encoding=Encoding.UTF16, mime='image/jpeg', type=3, desc='cover', data=b'synthetic'))
        tags.save(self.file(), v2_version=3, v1=0)
        with self.assertRaisesRegex(ValueError, 'non-whitelisted ID3 frame'):
            self.load()

    def test_audio_tamper_rejected_even_with_valid_credits(self):
        data = bytearray(self.file().read_bytes())
        data[-20] ^= 1
        self.file().write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'hash or size mismatch'):
            self.load()

    def test_fake_mp3_headers_with_undecodable_payload_rejected(self):
        data = self.file().read_bytes()
        end = 10 + sum(value << shift for value, shift in zip(data[6:10], (21, 14, 7, 0)))
        self.file().write_bytes(data[:end] + (b'\xff\xfb\x90\x00' + b'\xff' * 413) * 10)
        track = self.manifest['tracks'][0]
        actual = soundtrack.validate_mp3(self.file(), track['tags'])
        track.update({key: actual[key] for key in ('sha256', 'size_bytes', 'duration_seconds')})
        track['duration_label'] = soundtrack.duration_label(actual['duration_seconds'])
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'full MP3 decode failed'):
            self.load()

    def test_trailing_id3v1_and_arbitrary_private_data_rejected(self):
        original = self.file().read_bytes()
        for trailing in (b'TAG' + b'x' * 125, b'APETAGEX' + b'x' * 24, b'PRIVATE_FIXTURE'):
            with self.subTest(trailing=trailing[:8]):
                self.file().write_bytes(original + trailing)
                with self.assertRaisesRegex(ValueError, 'metadata|after MP3 audio'):
                    self.load()

    def test_duplicate_id3_frames_rejected_before_mutagen_merges_them(self):
        data = self.file().read_bytes()
        end = 10 + sum(value << shift for value, shift in zip(data[6:10], (21, 14, 7, 0)))
        size = int.from_bytes(data[14:18], 'big') + 10
        duplicate = data[10:10 + size]
        payload = duplicate + data[10:end]
        length = len(payload)
        header = data[:6] + bytes((length >> 21 & 127, length >> 14 & 127, length >> 7 & 127, length & 127))
        self.file().write_bytes(header + payload + data[end:])
        with self.assertRaisesRegex(ValueError, 'duplicate or missing'):
            self.load()

    def test_unlisted_public_mp3_rejected_case_insensitively(self):
        extra = self.site / 'public/assets/unauthorized.MP3'
        shutil.copyfile(self.file(), extra)
        with self.assertRaisesRegex(ValueError, 'Unlisted public MP3'):
            self.load()

    def test_manifest_cannot_publish_vanilla_or_escape_public(self):
        for key, value in (('game_asset', 'music/dintro.wav'),
                           ('game_asset', '../private.mp3'),
                           ('public_path', '/assets/audio/../../private.mp3'),
                           ('public_path', 'C:\\private.mp3')):
            with self.subTest(key=key, value=value):
                original = self.manifest['tracks'][0][key]
                self.manifest['tracks'][0][key] = value
                self.save_manifest()
                with self.assertRaises(ValueError):
                    self.load()
                self.manifest['tracks'][0][key] = original

    def test_variant_and_title_must_match_engine(self):
        for key, value in (('variant_id', 2), ('variant_id', True), ('title', 'Invented title')):
            with self.subTest(key=key):
                original = self.manifest['tracks'][0][key]
                self.manifest['tracks'][0][key] = value
                self.save_manifest()
                with self.assertRaisesRegex(ValueError, 'engine .* mismatch'):
                    self.load()
                self.manifest['tracks'][0][key] = original

    def test_catalog_drift_detected_even_for_pending_track(self):
        self.catalog.write_text(self.catalog.read_text(encoding='utf-8').replace('cathedral-rock.mp3', 'cathedral-v2-rock.mp3'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'catalog changed'):
            self.load()

    def test_new_available_custom_source_requires_manifest_update(self):
        missing = next(track for track in self.definitions if track['environment'] == 'Cathedral')
        target = soundtrack.safe_file(self.assets, missing['game_asset'])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(next(self.assets.rglob('*.mp3')), target)
        with self.assertRaisesRegex(ValueError, 'Available custom engine tracks differ'):
            self.load(sources=True)
        updated = prepare_soundtrack.prepare(self.assets, self.site, self.catalog, self.root / 'receipt.json')
        self.assertEqual(len(updated['tracks']), 6)
        self.assertEqual(updated['tracks'][-1]['environment'], 'Cathedral')
        self.assertEqual(updated['tracks'][-1]['title'], 'Cathedral — Rock')
        self.load(sources=True)

    def test_changed_custom_source_rejected_by_check(self):
        source = self.assets / self.manifest['tracks'][0]['game_asset']
        source.write_bytes(source.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'Custom source hash changed'):
            self.load(sources=True)

    def test_exact_credited_public_copies_are_accepted_as_custom_sources(self):
        provenance = [track['source_sha256'] for track in self.manifest['tracks']]
        for track in self.manifest['tracks']:
            public_copy = self.site / 'public' / track['public_path'][1:]
            shutil.copyfile(public_copy, self.assets / track['game_asset'])
        accepted = self.load(sources=True)
        self.assertEqual([track['source_sha256'] for track in accepted['tracks']], provenance)
        # Another valid, credited track still cannot replace this slot silently.
        first, second = self.manifest['tracks'][:2]
        shutil.copyfile(self.site / 'public' / second['public_path'][1:], self.assets / first['game_asset'])
        with self.assertRaisesRegex(ValueError, 'Custom source hash changed'):
            self.load(sources=True)

    def test_receipt_cannot_overwrite_sources_catalog_or_website(self):
        source = self.assets / self.manifest['tracks'][0]['game_asset']
        manifest = self.site / 'soundtrack.json'
        for receipt in (source, self.catalog, manifest, self.site / 'private.json', self.assets / 'private.json'):
            with self.subTest(receipt=receipt.name):
                before = {path: path.read_bytes() for path in (source, self.catalog, manifest)}
                with self.assertRaisesRegex(ValueError, 'Verification receipts need'):
                    prepare_soundtrack.prepare(self.assets, self.site, self.catalog, receipt)
                self.assertEqual({path: path.read_bytes() for path in before}, before)

    def test_promotion_failure_restores_previous_library_manifest_and_background(self):
        before = {path.relative_to(self.site).as_posix(): path.read_bytes()
                  for path in self.site.rglob('*') if path.is_file()}
        # Make the proposed replacement observably different from the old library.
        source = self.assets / self.manifest['tracks'][0]['game_asset']
        subprocess.run([shutil.which('ffmpeg'), '-v', 'error', '-nostdin', '-f', 'lavfi',
                        '-i', 'sine=frequency=880:duration=0.25', '-ar', '48000', '-ac', '2',
                        '-c:a', 'libmp3lame', '-y', str(source)], check=True)
        replace = prepare_soundtrack.os.replace
        for failed_call in (1, 2):
            with self.subTest(promotion_step=failed_call):
                calls = 0
                def fail_once(source, destination):
                    nonlocal calls
                    calls += 1
                    if calls == failed_call:
                        raise OSError('Synthetic promotion failure')
                    return replace(source, destination)
                with patch.object(prepare_soundtrack.os, 'replace', side_effect=fail_once):
                    with self.assertRaisesRegex(OSError, 'Synthetic'):
                        prepare_soundtrack.prepare(self.assets, self.site, self.catalog, self.root / 'receipt.json')
                after = {path.relative_to(self.site).as_posix(): path.read_bytes()
                         for path in self.site.rglob('*') if path.is_file()}
                self.assertEqual(after, before)
                self.load()
        load = prepare_soundtrack.load_soundtrack
        def fail_final_gate(site_root, *args):
            if Path(site_root) == self.site:
                raise ValueError('Synthetic final gate failure')
            return load(site_root, *args)
        with patch.object(prepare_soundtrack, 'load_soundtrack', side_effect=fail_final_gate):
            with self.assertRaisesRegex(ValueError, 'Synthetic final gate'):
                prepare_soundtrack.prepare(self.assets, self.site, self.catalog, self.root / 'receipt.json')
        after = {path.relative_to(self.site).as_posix(): path.read_bytes()
                 for path in self.site.rglob('*') if path.is_file()}
        self.assertEqual(after, before)
        self.load()

    def test_background_must_be_identical_to_credited_third_track(self):
        background = self.site / 'public' / soundtrack.BACKGROUND[1:]
        shutil.copyfile(self.file(), background)
        with self.assertRaisesRegex(ValueError, 'credit is incorrect|Background alias differs'):
            self.load()

    def test_symlink_cannot_escape_public_directory(self):
        destination = self.file()
        outside = self.root / 'outside.mp3'
        destination.rename(outside)
        try:
            destination.symlink_to(outside)
        except OSError:
            self.skipTest('File symlinks unavailable on this Windows account')
        with self.assertRaisesRegex(ValueError, 'escapes its asset directory'):
            self.load()


if __name__ == '__main__':
    unittest.main()
