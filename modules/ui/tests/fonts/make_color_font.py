#!/usr/bin/env python3
# Writes RawframeColor.ttf (D669): the smallest valid color font the
# engine's tests need, one the cook's sanitizer takes whole. 1000 units per
# em, ascent 800 and descent 200; an A of COLR version 0 layers over one
# CPAL palette (an em box of entry 0, red; its right half of entry 1, blue
# at half alpha; a small box of the text's color, entry 0xFFFF) and a B
# with no color, its outline the same box. Maul UI's MaulColor.ttf holds
# more, among it a layer past its palettes on purpose, which the sanitizer
# refuses.
#
# Needs fontTools 4.66.1. The output is the same bytes each time.
#
# usage: make_color_font.py <output.ttf>

import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


def box(left, bottom, right, top):
    pen = TTGlyphPen(None)
    pen.moveTo((left, bottom))
    pen.lineTo((left, top))
    pen.lineTo((right, top))
    pen.lineTo((right, bottom))
    pen.closePath()
    return pen.glyph()


def main():
    names = [".notdef", "A", "B", "whole", "half", "dot"]
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({ord("A"): "A", ord("B"): "B"})
    outlines = {
        ".notdef": box(100, 0, 900, 800),
        "A": box(100, 0, 900, 800),
        "B": box(100, 0, 900, 800),
        "whole": box(100, 0, 900, 800),
        "half": box(500, 0, 900, 800),
        "dot": box(300, 200, 500, 400),
    }
    builder.setupGlyf(outlines)
    bearings = {"half": 500, "dot": 300}
    builder.setupHorizontalMetrics({name: (1000, bearings.get(name, 100)) for name in names})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupCPAL([[(1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0, 0.5)]])
    builder.setupCOLR({"A": [("whole", 0), ("half", 1), ("dot", 0xFFFF)]}, version=0)
    builder.setupNameTable({"familyName": "Rawframe Color", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    # A fixed date, so the bytes do not change with the day they are made.
    builder.updateHead(created=0, modified=0)
    builder.save(sys.argv[1])


main()
