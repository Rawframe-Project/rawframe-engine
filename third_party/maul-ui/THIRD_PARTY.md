# Third-party code

Maul UI's text component (`MAUL_UI_TEXT`, on by default) builds two
outside libraries into the library, the family's recorded exception
(record mui-0006). With the component off, the library contains none.
Neither is kept in this repository: the build fetches each release
archive and checks its SHA-256.

| Library | Version | License | Archive SHA-256 |
| --- | --- | --- | --- |
| [FreeType](https://freetype.org) | 2.14.3 | FreeType License (BSD-style, with credit), or GPL 2 | `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f` |
| [HarfBuzz](https://harfbuzz.github.io) | 14.5.1 | MIT ("Old MIT") | `7e2fa4e8c7c98e8d8140671f5772542afaaa6acccfbd746506886b6d85f7f8d6` |

A program that ships Maul UI with text carries FreeType's credit: "Portions
of this software are copyright © The FreeType Project
(https://freetype.org). All rights reserved."

Maul Unicode 0.2.1, also fetched, is part of the Maul family (MIT).

Test data from outside is listed in `test/fonts/README.md`. The samples
embed one of those fonts, Liberation Sans (SIL Open Font License 1.1,
`test/fonts/LiberationSans-LICENSE.txt`), in their programs; the library
contains no font.
