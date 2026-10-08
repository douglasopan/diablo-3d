#!/usr/bin/env python3
"""Compile one PO without gettext installed, validating before atomic delivery."""

import gettext
import importlib.util
import os
from pathlib import Path
import sys
import tempfile


def compile_catalog(source: Path, destination: Path) -> None:
    compiler_path = Path(__file__).parent / "third_party" / "cpython" / "msgfmt.py"
    spec = importlib.util.spec_from_file_location("d3d_msgfmt", compiler_path)
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    # Header metadata is required even if the translator marked it fuzzy.
    # Keep omitting fuzzy messages; otherwise UTF-8/plurals would lose their header.
    upstream_add = compiler.add
    compiler.add = lambda context, key, value, fuzzy: upstream_add(
        context, key, value, fuzzy and key != b""
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    normalized = None
    try:
        # Upstream resets entry flags on a comment following msgstr. PO entries
        # without reference comments are valid too, so supply boundary comments.
        with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".tmp.po", delete=False) as po:
            normalized = Path(po.name)
            for line in source.read_bytes().splitlines(keepends=True):
                po.write(line)
                if not line.strip():
                    po.write(b"#\n")
        with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".gmo.tmp", delete=False) as out:
            temporary = Path(out.name)
        compiler.make(str(normalized), str(temporary))
        try:
            with temporary.open("rb") as stream:
                gettext.GNUTranslations(stream)
        except Exception as error:
            raise ValueError(f"Invalid compiled catalog: {source}") from error
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        if normalized is not None:
            normalized.unlink(missing_ok=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: compile_translations.py input.po output.gmo")
    compile_catalog(Path(sys.argv[1]), Path(sys.argv[2]))
