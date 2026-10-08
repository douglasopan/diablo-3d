#!/usr/bin/env python3
"""Exercise the fallback compiler with real catalogs and contextual plurals."""

import gettext
from pathlib import Path
import tempfile

from compile_translations import compile_catalog


def load(path):
    with path.open("rb") as stream:
        return gettext.GNUTranslations(stream)


def main():
    root = Path(__file__).resolve().parents[1]
    checks = 0
    with tempfile.TemporaryDirectory(prefix="d3d-catalog-test-") as work:
        work = Path(work)
        for source in sorted((root / "Translations").glob("*.po")):
            target = work / (source.stem + ".gmo")
            compile_catalog(source, target)
            translation = load(target)
            assert translation.info().get("content-type", "").lower().endswith("charset=utf-8"), source
            assert translation.gettext("D3D missing key") == "D3D missing key", source
            checks += 2
        portuguese = load(work / "pt_BR.gmo")
        assert portuguese.gettext("Settings") == "Configurações"
        assert portuguese.gettext("Language") == "Idioma"
        assert portuguese.gettext("Single Player") == "Um Jogador"
        checks += 3

        source = work / "sample.po"
        source.write_text('''#, fuzzy
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\\n"
"Plural-Forms: nplurals=2; plural=(n > 1);\\n"

msgctxt "menu"
msgid "Open"
msgstr "Abrir"

msgid "one flame"
msgid_plural "many flames"
msgstr[0] "uma chama"
msgstr[1] "várias chamas"

#, fuzzy
msgid "Unreviewed"
msgstr "Não aprovado"
''', encoding="utf-8")
        target = work / "sample.gmo"
        compile_catalog(source, target)
        translation = load(target)
        assert translation.pgettext("menu", "Open") == "Abrir"
        assert translation.gettext("Open") == "Open"
        assert translation.ngettext("one flame", "many flames", 0) == "uma chama"
        assert translation.ngettext("one flame", "many flames", 1) == "uma chama"
        assert translation.ngettext("one flame", "many flames", 2) == "várias chamas"
        assert translation.gettext("Unreviewed") == "Unreviewed"
        checks += 6
        original = target.read_bytes()
        source.write_text('msgid "broken\\n', encoding="utf-8")
        try:
            compile_catalog(source, target)
        except (SyntaxError, SystemExit):
            pass
        else:
            raise AssertionError("Malformed input was accepted")
        assert target.read_bytes() == original
        assert not list(work.glob("*.tmp"))
        checks += 2
    print(f"PASS {checks} catalog checks; private temporary files only")


if __name__ == "__main__":
    main()
