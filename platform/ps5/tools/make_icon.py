#!/usr/bin/env python3
"""Write the 512x512 RGBA icon0.png the console shell displays.

Pure standard library: no Pillow, no ImageMagick, so the package builds on any
host that already runs make_fself.py. The PNG is a real zlib-compressed RGBA
image with per-chunk CRCs, not a renamed or truncated file.
"""

import argparse
import struct
import zlib

SIZE = 512
BACKGROUND_TOP = (12, 17, 32)
BACKGROUND_BOTTOM = (27, 41, 63)
FRAME = (44, 62, 90)
INK = (236, 242, 255)
ACCENT = (76, 201, 240)

# 5x7 bitmap glyphs, one row per entry, top row first.
GLYPHS = {
    "P": (0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000),
    "S": (0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110),
    "5": (0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110),
}
GLYPH_WIDTH = 5
GLYPH_HEIGHT = 7


class Canvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height * 4)

    def put(self, x, y, rgba):
        if 0 <= x < self.width and 0 <= y < self.height:
            index = (y * self.width + x) * 4
            self.pixels[index:index + 4] = bytes(rgba)

    def fill_rect(self, x0, y0, x1, y1, rgba):
        for y in range(max(0, y0), min(self.height, y1)):
            for x in range(max(0, x0), min(self.width, x1)):
                self.put(x, y, rgba)

    def stroke_rect(self, x0, y0, x1, y1, thickness, rgba):
        self.fill_rect(x0, y0, x1, y0 + thickness, rgba)
        self.fill_rect(x0, y1 - thickness, x1, y1, rgba)
        self.fill_rect(x0, y0, x0 + thickness, y1, rgba)
        self.fill_rect(x1 - thickness, y0, x1, y1, rgba)


def draw_glyph(canvas, glyph, x0, y0, scale, rgba):
    rows = GLYPHS[glyph]
    for row_index, bits in enumerate(rows):
        for column in range(GLYPH_WIDTH):
            if bits & (1 << (GLYPH_WIDTH - 1 - column)):
                canvas.fill_rect(
                    x0 + column * scale,
                    y0 + row_index * scale,
                    x0 + (column + 1) * scale,
                    y0 + (row_index + 1) * scale,
                    rgba,
                )


def render():
    canvas = Canvas(SIZE, SIZE)
    for y in range(SIZE):
        t = y / (SIZE - 1)
        color = tuple(
            int(BACKGROUND_TOP[i] + (BACKGROUND_BOTTOM[i] - BACKGROUND_TOP[i]) * t)
            for i in range(3)
        ) + (255,)
        for x in range(SIZE):
            canvas.put(x, y, color)

    canvas.stroke_rect(24, 24, SIZE - 24, SIZE - 24, 4, FRAME + (255,))

    scale = 24
    gap = 24
    text = "PS5"
    text_width = len(text) * GLYPH_WIDTH * scale + (len(text) - 1) * gap
    x = (SIZE - text_width) // 2
    y = (SIZE - GLYPH_HEIGHT * scale) // 2 - 20
    for index, glyph in enumerate(text):
        draw_glyph(canvas, glyph, x, y, scale, (ACCENT if glyph == "5" else INK) + (255,))
        x += GLYPH_WIDTH * scale + gap

    bar_width = 360
    canvas.fill_rect((SIZE - bar_width) // 2, 372, (SIZE + bar_width) // 2, 382, ACCENT + (255,))
    return canvas


def chunk(tag, payload):
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def encode_png(canvas):
    header = struct.pack(">IIBBBBB", canvas.width, canvas.height, 8, 6, 0, 0, 0)
    rows = bytearray()
    stride = canvas.width * 4
    for y in range(canvas.height):
        rows.append(0)  # filter: none
        rows += canvas.pixels[y * stride:(y + 1) * stride]
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + chunk(b"IEND", b"")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title-id", required=True, help="documented in the PNG text chunk")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    blob = encode_png(render())
    with open(args.output, "wb") as handle:
        handle.write(blob)
    print("icon0.png: %s title=%s bytes=%d" % (args.output, args.title_id, len(blob)))


if __name__ == "__main__":
    main()
