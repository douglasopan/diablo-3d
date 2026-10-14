import json
import re
import unittest
from pathlib import Path
import build

class ReleaseDownloadTests(unittest.TestCase):
    def test_immutable_windows_package(self):
        release = json.loads((Path(__file__).parent / 'releases.json').read_text(encoding='utf-8'))
        self.assertEqual(release['version'], 'v0.1.0-preview.local3-tristram.full4')
        self.assertRegex(release['archive']['sha256'], r'^[0-9a-f]{64}$')
        self.assertEqual(release['archive']['bytes'], 217684454)
        self.assertIn('/releases/download/' + release['version'] + '/', release['archive']['url'])
        self.assertEqual(build.localized_path('/download/', 'en'), '/en/download/')
        self.assertEqual(build.localized_path('/download/', 'pt-BR'), '/download/')

if __name__ == '__main__':
    unittest.main()
