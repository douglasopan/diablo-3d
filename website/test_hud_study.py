"""Guard the R2 publication boundary without copying source directories."""
import unittest

from hud_study import selected_file, load_study


class HudStudyTests(unittest.TestCase):
    def test_source_paths_exclude_masters_and_private_files(self):
        for path in ('../references/f03.webp', 'previews/../../save.sv', 'images/s01.png',
                     'assets/model.glb', 'assets/kit-panel.png.import', 'references/f03.webp/extra',
                     'C:/private.png', 'previews\\s01.webp'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                selected_file(path)

    def test_catalog_keeps_references_and_concepts_distinct(self):
        study = load_study()
        assets = study['catalog']['assets']
        self.assertEqual(sum(a['kind'] == 'concept' for a in assets), 60)
        self.assertEqual(sum(a['kind'] == 'component' for a in assets), 3)
        self.assertEqual(sum(a['kind'] == 'reused-concept' for a in assets), 1)
        refs = [s['reference'] for s in study['catalog']['screens'] if s['reference']]
        self.assertEqual(sum(r['type'] == 'realwindow' for r in refs), 1)
        self.assertEqual(sum(r['type'] == 'nativefixture' for r in refs), 13)
        self.assertEqual(len(study['files']), 78)
        for asset in assets:
            self.assertTrue(all(key in asset['titles'] for key in ('pt-BR', 'en')))


if __name__ == '__main__':
    unittest.main()
