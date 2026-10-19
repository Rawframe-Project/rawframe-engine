#!/usr/bin/env python3
# Draws Studio's icons (D915): each a 16-unit line drawing, the screen
# design's, white on nothing, drawn 32 pixels square by a headless Chrome
# (offline, as tools/make_sky.py draws the sky) and written whole as
# modules/studio/icons/icons.rgba, one icon after another, 32 by 32 RGBA8
# each, and modules/studio/src/icons.h naming them in that order. Studio
# tints each to the color it is shown in. Run again whenever an icon here
# changes; the check runs none of it.
#
# usage: RAWFRAME_CHROME=<chrome> tools/make_studio_icons.py

import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIDE = 32

# name, the drawing's elements in a 16-unit box, and whether it is filled
# rather than stroked.
ICONS = [
    ("select", '<path d="M3 2l9 5-4 1.2L6.5 13z"/>', False),
    ("move", '<path d="M8 1.5v13M1.5 8h13M8 1.5L6 3.5M8 1.5l2 2M8 14.5l-2-2M8 14.5l2-2M1.5 8l2-2M1.5 8l2 2M14.5 8l-2-2M14.5 8l-2 2"/>', False),
    ("rotate", '<path d="M13 8a5 5 0 1 1-1.5-3.5M13 2v3h-3"/>', False),
    ("scale", '<rect x="2" y="7" width="7" height="7" rx="1"/><path d="M9 2h5v5M14 2L8.5 7.5"/>', False),
    ("world", '<circle cx="8" cy="8" r="6"/><path d="M2 8h12M8 2c2 2 2 10 0 12M8 2c-2 2-2 10 0 12"/>', False),
    ("grid", '<path d="M2 2h12v12H2zM2 6h12M2 10h12M6 2v12M10 2v12"/>', False),
    ("angle", '<path d="M2 14h12M2 14L12 4"/><path d="M7 14a5 5 0 0 0-1.5-3.5"/>', False),
    ("cook", '<path d="M2.5 6.5h11l-1 7h-9z"/><path d="M5.5 6.5c0-2 1-3 2.5-3s2.5 1 2.5 3"/>', False),
    ("play", '<path d="M5 3l8 5-8 5z"/>', True),
    ("pause", '<path d="M5.5 3.5v9M10.5 3.5v9"/>', False),
    ("step", '<path d="M3.5 3.5l6 4.5-6 4.5z"/><path d="M12.5 3.5v9"/>', False),
    ("stop", '<rect x="3.5" y="3.5" width="9" height="9" rx="1.5"/>', True),
    ("undo", '<path d="M6 4L3 7l3 3M3 7h7a3 3 0 0 1 0 6H8"/>', False),
    ("redo", '<path d="M10 4l3 3-3 3M13 7H6a3 3 0 0 0 0 6h2"/>', False),
    ("layout", '<rect x="2" y="2" width="12" height="12" rx="1.5"/><path d="M2 6h12M6 6v8"/>', False),
    ("search", '<circle cx="7" cy="7" r="4.5"/><path d="M10.5 10.5L14 14"/>', False),
    ("plus", '<path d="M8 3v10M3 8h10"/>', False),
    ("close", '<path d="M4 4l8 8M12 4l-8 8"/>', False),
    ("chevron_down", '<path d="M4 6l4 4 4-4"/>', False),
    ("chevron_right", '<path d="M6 4l4 4-4 4"/>', False),
    ("eye", '<path d="M1.5 8s2.5-4.5 6.5-4.5S14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8z"/><circle cx="8" cy="8" r="2"/>', False),
    ("eye_off", '<path d="M1.5 8s2.5-4.5 6.5-4.5S14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8z"/><path d="M2.5 13.5l11-11"/>', False),
    ("lock", '<rect x="3.5" y="7" width="9" height="7" rx="1"/><path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2"/>', False),
    ("unlock", '<rect x="3.5" y="7" width="9" height="7" rx="1"/><path d="M5.5 7V5a2.5 2.5 0 0 1 4.8-1"/>', False),
    ("cube", '<path d="M8 1.5l5.5 3v7L8 14.5l-5.5-3v-7zM2.5 4.5L8 7.5l5.5-3M8 7.5v7"/>', False),
    ("folder", '<path d="M2 3.5h4l1.5 1.5H14v8H2z"/>', False),
    ("folder_open", '<path d="M2 13V3.5h4L7.5 5H13v2"/><path d="M2 13l2.5-6H15l-2.5 6z"/>', False),
    ("warning", '<path d="M8 2l6.5 11.5h-13z"/><path d="M8 6.5v3M8 11.6v.2"/>', False),
    ("error", '<circle cx="8" cy="8" r="6"/><path d="M8 4.5v4.5M8 11.2v.3"/>', False),
    ("check", '<path d="M3 8.5l3 3 7-7"/>', False),
    ("sun", '<circle cx="8" cy="8" r="3"/><path d="M8 1.5V3M8 13v1.5M1.5 8H3M13 8h1.5M3.4 3.4l1 1M11.6 11.6l1 1M3.4 12.6l1-1M11.6 4.4l1-1"/>', False),
    ("sky", '<path d="M4.5 12.5a3 3 0 0 1-.4-6 4 4 0 0 1 7.7 1 2.5 2.5 0 0 1-.3 5z"/>', False),
    ("sea", '<path d="M1.5 6c1.5-1.5 3-1.5 4.5 0s3 1.5 4.5 0 3-1.5 4.5 0M1.5 10.5c1.5-1.5 3-1.5 4.5 0s3 1.5 4.5 0 3-1.5 4.5 0"/>', False),
    ("camera", '<rect x="1.5" y="4.5" width="9" height="7" rx="1"/><path d="M10.5 7l4-2v6l-4-2"/>', False),
    ("bulb", '<path d="M6 12.5h4M6.5 14.5h3M8 1.5a4.5 4.5 0 0 0-2.5 8.2V11h5V9.7A4.5 4.5 0 0 0 8 1.5z"/>', False),
    ("person", '<circle cx="8" cy="5" r="2.5"/><path d="M3 14c.5-3 2.5-4.5 5-4.5s4.5 1.5 5 4.5"/>', False),
    ("agent", '<rect x="3" y="5" width="10" height="8" rx="2"/><path d="M8 2.5V5M6 9v.5M10 9v.5"/>', False),
    ("puzzle", '<path d="M3 5h3a1.5 1.5 0 1 1 3 0h3v3a1.5 1.5 0 1 0 0 3v3H9a1.5 1.5 0 1 0-3 0H3v-3a1.5 1.5 0 1 1 0-3z"/>', False),
    ("material", '<circle cx="8" cy="8" r="6"/><path d="M2.4 9.2c2.4 1.6 8.8 1.6 11.2 0M5.5 4.6a3.8 3.8 0 0 1 2.6-1"/>', False),
    ("sound", '<path d="M1.5 8h2l2-4 3 9 2.5-7 1.5 2h2"/>', False),
    ("script", '<path d="M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5"/>', False),
    ("ui", '<rect x="2" y="3" width="12" height="10" rx="1.5"/><path d="M2 6h12M4.5 9h4"/>', False),
    ("texture", '<rect x="2" y="2.5" width="12" height="11" rx="1.5"/><circle cx="5.5" cy="6" r="1.2"/><path d="M2 11.5l3.5-3 3 2.5 2-1.5 3.5 3"/>', False),
    ("animation", '<circle cx="8" cy="8" r="6"/><path d="M6.5 5.5l4 2.5-4 2.5z"/>', False),
    ("data", '<rect x="2" y="3" width="12" height="10" rx="1"/><path d="M2 6.5h12M2 10h12M6 3v10"/>', False),
    ("gear", '<circle cx="8" cy="8" r="2"/><circle cx="8" cy="8" r="4.6"/><path d="M8 1.5v1.9M8 12.6v1.9M1.5 8h1.9M12.6 8h1.9M3.4 3.4l1.3 1.3M11.3 11.3l1.3 1.3M3.4 12.6l1.3-1.3M11.3 4.7l1.3-1.3"/>', False),
    ("filter", '<path d="M2 3h12l-4.5 5.5V13l-3-1.5v-3z"/>', False),
    ("more", '<circle cx="8" cy="3.5" r="1.1"/><circle cx="8" cy="8" r="1.1"/><circle cx="8" cy="12.5" r="1.1"/>', True),
    ("bookmark", '<path d="M4 2h8v12l-4-3-4 3z"/>', False),
    ("branch", '<circle cx="4.5" cy="3.5" r="1.5"/><circle cx="4.5" cy="12.5" r="1.5"/><circle cx="11.5" cy="5" r="1.5"/><path d="M4.5 5v6M11.5 6.5c0 3-7 2-7 4.5"/>', False),
    ("split", '<rect x="2" y="2.5" width="12" height="11" rx="1"/><path d="M8 2.5v11"/>', False),
    ("layers", '<path d="M8 2l6 3-6 3-6-3zM2 8l6 3 6-3M2 11l6 3 6-3"/>', False),
    ("history", '<circle cx="8" cy="8" r="6"/><path d="M8 4.5V8l2.5 1.5"/>', False),
    ("console", '<rect x="1.5" y="2.5" width="13" height="11" rx="1.5"/><path d="M4.5 6l2.5 2-2.5 2M8.5 10.5h3"/>', False),
    ("network", '<circle cx="8" cy="6.5" r="1.5"/><path d="M8 8v6.5M4.5 3.5a5 5 0 0 0 0 6M11.5 3.5a5 5 0 0 1 0 6"/>', False),
    ("profiler", '<path d="M2.5 13.5h11M4.5 13.5V9M8 13.5V4.5M11.5 13.5V7"/>', False),
    ("mixer", '<path d="M4 2v12M8 2v12M12 2v12M2.5 10h3M6.5 5h3M10.5 8h3"/>', False),
    ("curves", '<path d="M2 13c3 0 4-10 6-10s3 10 6 10"/>', False),
    ("capture", '<rect x="2" y="2" width="12" height="12" rx="1"/><path d="M5 2v12M11 2v12M2 5.5h3M11 5.5h3M2 10.5h3M11 10.5h3"/>', False),
    ("link", '<path d="M6.5 9.5l3-3M5 11a2.5 2.5 0 0 1 0-3.5l1.5-1.5M11 5a2.5 2.5 0 0 1 0 3.5L9.5 10"/>', False),
    ("copy", '<rect x="5" y="5" width="9" height="9" rx="1.5"/><path d="M11 5V3.5A1.5 1.5 0 0 0 9.5 2h-6A1.5 1.5 0 0 0 2 3.5v6A1.5 1.5 0 0 0 3.5 11H5"/>', False),
    ("trash", '<path d="M2.5 4h11M6 4V2.5h4V4M4 4l.8 9.5h6.4L12 4"/>', False),
    ("import", '<path d="M8 2v8M5 7l3 3 3-3M2.5 11v2.5h11V11"/>', False),
    ("expand", '<path d="M2 6V2h4M14 6V2h-4M2 10v4h4M14 10v4h-4"/>', False),
    ("empty", '<circle cx="8" cy="8" r="4.5"/>', False),
    ("sphere", '<circle cx="8" cy="8" r="5.5"/><path d="M2.5 8c0 1.5 2.5 2.5 5.5 2.5s5.5-1 5.5-2.5"/>', False),
    ("plane", '<path d="M1.5 10L6 6h8.5L10 10z"/>', False),
    ("capsule", '<rect x="5" y="1.5" width="6" height="13" rx="3"/>', False),
    ("cylinder", '<ellipse cx="8" cy="4" rx="5" ry="2"/><path d="M3 4v8c0 1.1 2.2 2 5 2s5-.9 5-2V4"/>', False),
    ("blockout", '<path d="M2 5.5L8 2.5l6 3v5l-6 3-6-3z"/><path d="M8 6v4.5M5.8 8.2h4.4"/>', False),
    ("terrain", '<path d="M1.5 13.5l4-6 3 4 2-3 4 5z"/>', False),
    ("tiles", '<rect x="2" y="2" width="5" height="5"/><rect x="9" y="2" width="5" height="5"/><rect x="2" y="9" width="5" height="5"/><rect x="9" y="9" width="5" height="5"/>', False),
    ("scatter", '<circle cx="4" cy="5" r="1.5"/><circle cx="11" cy="3.5" r="1.5"/><circle cx="7.5" cy="9" r="1.5"/><circle cx="3.5" cy="12.5" r="1.5"/><circle cx="12" cy="11.5" r="1.5"/>', False),
    ("dock", '<path d="M2 10h12M3 10v4M7 10v4M11 10v4M2 7h12"/>', False),
    ("file", '<path d="M4 1.5h5l3 3v10H4z"/><path d="M9 1.5v3h3"/>', False),
    ("prefab", '<path d="M8 1.5l5.5 3v7L8 14.5l-5.5-3v-7zM2.5 4.5L8 7.5l5.5-3M8 7.5v7"/><circle cx="8" cy="10.5" r="1" />', False),
    ("minimize", '<path d="M3 8h10"/>', False),
    ("maximize", '<rect x="3" y="3" width="10" height="10"/>', False),
    ("keyboard", '<rect x="1.5" y="4" width="13" height="8" rx="1.5"/><path d="M4 7h1M7 7h1M10 7h2M4 9.5h8"/>', False),
    ("mesh", '<path d="M2 12L8 2l6 10z"/><path d="M5 7h6M8 2v10M2 12l6-5 6 5"/>', False),
    ("font", '<path d="M3 13.5L7.5 2.5h1L13 13.5M5 9h6"/>', False),
]


def page():
    cells = []
    for name, drawing, filled in ICONS:
        paint = 'fill="#ffffff" stroke="none"' if filled else 'fill="none" stroke="#ffffff" stroke-width="1.5"'
        cells.append(
            f'<svg width="{SIDE}" height="{SIDE}" viewBox="0 0 16 16" {paint} stroke-linecap="round" '
            f'stroke-linejoin="round" style="display:block;float:left">{drawing}</svg>'
        )
    width = SIDE * len(ICONS)
    return (
        "<!doctype html><html><head><style>html,body{margin:0;padding:0;background:transparent}"
        f"body{{width:{width}px;height:{SIDE}px;overflow:hidden}}</style></head><body>"
        + "".join(cells)
        + "</body></html>"
    )


def decode(png):
    # An 8-bit RGBA PNG, not interlaced, as Chrome writes a screenshot.
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    at, idat, width, height = 8, b"", 0, 0
    while at < len(png):
        length, kind = struct.unpack(">I4s", png[at : at + 8])
        body = png[at + 8 : at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color == 6, "the screenshot is not 8-bit RGBA"
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - 4] if x >= 4 else 0
            up = previous[x]
            corner = previous[x - 4] if x >= 4 else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                guess = left + up - corner
                pa, pb, pc = abs(guess - left), abs(guess - up), abs(guess - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(bytes(line))
        previous = line
    return width, height, rows


def main():
    chrome = os.environ.get("RAWFRAME_CHROME")
    if not chrome:
        sys.exit("RAWFRAME_CHROME names a Chrome to draw the icons with")
    with tempfile.TemporaryDirectory() as work:
        html = os.path.join(work, "icons.html")
        shot = os.path.join(work, "icons.png")
        with open(html, "w") as file:
            file.write(page())
        subprocess.run(
            [chrome, "--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
             "--default-background-color=00000000", "--force-device-scale-factor=1",
             f"--window-size={SIDE * len(ICONS)},{SIDE}", f"--screenshot={shot}", "file://" + html],
            check=True, capture_output=True)
        with open(shot, "rb") as file:
            width, height, rows = decode(file.read())
    assert width >= SIDE * len(ICONS) and height >= SIDE
    out = bytearray()
    for index in range(len(ICONS)):
        for y in range(SIDE):
            out += rows[y][index * SIDE * 4 : (index + 1) * SIDE * 4]
    os.makedirs(os.path.join(ROOT, "modules", "studio", "icons"), exist_ok=True)
    with open(os.path.join(ROOT, "modules", "studio", "icons", "icons.rgba"), "wb") as file:
        file.write(out)
    names = "\n".join(f"    {name.title().replace('_', '')}," for name, _, _ in ICONS)
    with open(os.path.join(ROOT, "modules", "studio", "src", "icons.h"), "w") as file:
        file.write(
            "#pragma once\n\n"
            "// Studio's icons (D915), in the order modules/studio/icons/icons.rgba\n"
            "// holds them: generated by tools/make_studio_icons.py, which draws them.\n\n"
            "#include <cstddef>\n#include <cstdint>\n\n"
            "namespace rawframe::studio {\n\n"
            "enum class Icon : std::uint8_t {\n" + names + "\n};\n\n"
            f"inline constexpr std::size_t kIconCount = {len(ICONS)};\n"
            f"inline constexpr std::uint32_t kIconSide = {SIDE};\n\n"
            "} // namespace rawframe::studio\n"
        )
    print(f"{len(ICONS)} icons, {len(out)} bytes")


if __name__ == "__main__":
    main()
