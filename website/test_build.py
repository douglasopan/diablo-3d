"""Regression checks for the authoring contract and publication boundary."""
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from PIL import Image
import yaml
import build


class PublicationContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.posts = self.root / 'docs/devlog'
        self.posts.mkdir(parents=True)
        self.public = self.root / 'public'
        (self.public / 'assets').mkdir(parents=True)
        Image.new('RGB', (2, 2)).save(self.public / 'assets/test.png')
        self.data = dict(title='Registro de teste', date='2026-10-07', description='Uma mudança verificada.', slug='registro', image='/assets/test.png', image_alt='Imagem de teste.', category='Geometria')
        self.repo_patch = patch.object(build, 'REPO', self.root)
        self.public_patch = patch.object(build, 'PUBLIC', self.public)
        self.root_patch = patch.object(build, 'ROOT', self.root / 'website')
        self.repo_patch.start()
        self.public_patch.start()
        self.root_patch.start()

    def tearDown(self):
        self.repo_patch.stop()
        self.public_patch.stop()
        self.root_patch.stop()
        self.temp.cleanup()

    def post(self, name='one.md', body='## O que mudou\n\nUm relato com evidência.', **changes):
        data = dict(self.data, **changes)
        (self.posts / name).write_text('---\n' + yaml.safe_dump(data, allow_unicode=True) + '---\n' + body, encoding='utf-8')

    def test_complete_post_and_draft_exclusion(self):
        self.post()
        self.post('draft.md', status='draft', slug='rascunho')
        result = build.load_posts()
        self.assertEqual([p['slug'] for p in result], ['registro'])
        self.assertIn('id="o-que-mudou"', result[0]['body'])

    def test_duplicate_slug_rejected(self):
        self.post()
        self.post('two.md')
        with self.assertRaisesRegex(ValueError, 'duplicate slug'):
            build.load_posts()

    def test_missing_asset_rejected(self):
        self.post(image='/assets/absent.png')
        with self.assertRaisesRegex(ValueError, 'Missing public image'):
            build.load_posts()

    def test_asset_cannot_escape_public_directory(self):
        self.post(image='/assets/../test.png')
        with self.assertRaisesRegex(ValueError, 'public /assets/'):
            build.load_posts()

    def test_active_html_and_unsafe_link_rejected(self):
        for body in ('<script>alert(1)</script>', '[Abrir](javascript:alert%281%29)'):
            with self.subTest(body=body):
                self.post(body=body)
                with self.assertRaises(ValueError):
                    build.load_posts()

    def test_invalid_frontmatter_fails_before_publication(self):
        for changes in ({'date': '2026-99-99'}, {'image_alt': ''}, {'category': 'Indefinida'}, {'status': 'maybe'}, {'slug': 'Espaços e Maiúsculas'}):
            with self.subTest(changes=changes):
                self.post(**changes)
                with self.assertRaises(ValueError):
                    build.load_posts()

    def english_post(self, body='## What changed\n\nA verified change.', **changes):
        directory = self.root / 'website/content/en/devlog'
        directory.mkdir(parents=True, exist_ok=True)
        data = dict(self.data, title='Test journal', description='A verified change.', image_alt='Test image.', **changes)
        (directory / 'one.md').write_text('---\n' + yaml.safe_dump(data) + '---\n' + body, encoding='utf-8')

    def test_english_translation_required_for_every_published_post(self):
        self.post()
        with self.assertRaisesRegex(ValueError, 'complete English translation required'):
            build.load_posts('en')

    def test_translation_preserves_evidence_and_editorial_metadata(self):
        self.post()
        for changes in ({'date': '2026-10-08'}, {'image': '/assets/another.png'}, {'slug': 'changed'}):
            with self.subTest(changes=changes):
                self.english_post(**changes)
                with self.assertRaisesRegex(ValueError, 'must match original metadata'):
                    build.load_posts('en')

    def test_language_switch_preserves_article_anchors_and_local_links(self):
        self.post(body='## O que mudou\n\n[Galeria](/galeria/)\n\n## Próximo passo\n\nMais testes.')
        self.english_post(body='## What changed\n\n[Gallery](/galeria/)\n\n## Next step\n\n[Return](#what-changed).')
        result = build.load_posts('en')[0]
        self.assertIn('id="o-que-mudou"', result['body'])
        self.assertIn('id="proximo-passo"', result['body'])
        self.assertIn('href="#o-que-mudou"', result['body'])
        self.assertIn('href="' + build.PREFIX + '/en/galeria/"', result['body'])
        self.assertIn('href="#proximo-passo"', result['toc'])

    def test_translation_cannot_drop_article_sections(self):
        self.post(body='## Primeiro\n\nUm.\n\n## Segundo\n\nDois.')
        self.english_post()
        with self.assertRaisesRegex(ValueError, 'heading structure must match original'):
            build.load_posts('en')


if __name__ == '__main__':
    unittest.main()
