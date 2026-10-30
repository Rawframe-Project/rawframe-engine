# Licenses

Maul UI is under the MIT license ([LICENSE](../../LICENSE)).

## What a program ships

The library always contains [Maul Tween](https://github.com/Rawframe-Project/maul-tween) 0.12.0, under the MIT license, fetched at its release tag ([Tween.cmake](../../cmake/Tween.cmake)).

With the text component (`MAUL_UI_TEXT`, on by default), the library contains two outside libraries, built from their release archives, each checked by its SHA-256 at configure time ([TextDependencies.cmake](../../cmake/TextDependencies.cmake)). Neither is kept in this repository.

| Library | Version | License |
|---|---|---|
| [FreeType](https://freetype.org) | 2.14.3 | FreeType License (BSD-style, with credit), or GPL 2 |
| [HarfBuzz](https://harfbuzz.github.io) | 14.5.1 | MIT ("Old MIT") |
| [Maul Unicode](https://github.com/Rawframe-Project/maul-unicode) | 0.8.0 | MIT |

A program that ships Maul UI with text carries FreeType's credit, for example in its about box or its notices:

> Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved.

With the text component off, the library contains none of them. The library contains no font: fonts are the program's, under their own licenses.

The reference renderer and the window glue link Maul RHI and Maul Window, both MIT.

## What the repository holds

- The test fonts in [test/fonts](../../test/fonts) are listed with their sources and licenses in [Test fonts](../operations/test-fonts.md). Liberation Sans is under the SIL Open Font License 1.1, Ahem is in the public domain, and the fonts made by the scripts beside them are MIT. The samples embed Liberation Sans in their programs.
- The layout fixtures in `test/layout/` hold rectangles measured in Chrome, data under this repository's license.
