# CPython message-catalog compiler

`msgfmt.py` is an unmodified copy from CPython **v3.13.8**:
https://github.com/python/cpython/blob/v3.13.8/Tools/i18n/msgfmt.py

Original author: Martin v. Löwis. The complete upstream license is included in
`LICENSE`. Upstream script SHA-256:
`154221f71d949780b5371d5dc5af129aa8393a7d58b94a4d5bb66ba1d381ac0f`.
Changes should
be made in our wrapper, `../../compile_translations.py`, rather than this copy.

Used only when GNU gettext tools are unavailable. The wrapper validates the MO
with Python's GNUTranslations and atomically delivers the result. It preserves
header metadata even when marked fuzzy, while omitting fuzzy translated messages.
It supplies entry-boundary comments in a temporary input, so fuzzy flags cannot
leak into subsequent entries without reference comments. The source PO is unchanged.
Contexts,
plural strings, UTF-8 and fuzzy-entry omission are exercised by the catalog test.
