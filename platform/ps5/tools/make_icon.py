#!/usr/bin/env python3
"""Write the 512x512 RGBA icon0.png the console shell displays.

Pure standard library: no Pillow, no ImageMagick, so the package builds on any
host that already runs make_fself.py. The artwork is rendered at 2x and box
filtered down, giving the glyph and glows clean antialiased edges without an
image dependency.

The visual language is deliberately close to the PS5 system UI: a near-black
field, a cool low-contrast glow, one white geometric monogram and a small blue
accent. Keep the mark inside the central safe area because the shell masks the
tile corners.
"""

import argparse
import math
import struct
import zlib

SIZE = 512
SUPERSAMPLE = 2

BACKGROUND_TOP = (5, 7, 12)
BACKGROUND_BOTTOM = (13, 20, 33)
GLOW = (31, 101, 196)
WHITE_TOP = (255, 255, 255)
WHITE_BOTTOM = (196, 211, 232)
BLUE_TOP = (63, 139, 255)
BLUE_BOTTOM = (21, 91, 216)


def clamp_byte(value):
    if value <= 0:
        return 0
    if value >= 255:
        return 255
    return int(round(value))


class Canvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height * 4)

    def blend(self, x, y, rgba):
        if 0 <= x < self.width and 0 <= y < self.height:
            index = (y * self.width + x) * 4
            alpha = rgba[3] / 255.0
            inverse = 1.0 - alpha
            self.pixels[index] = clamp_byte(rgba[0] * alpha + self.pixels[index] * inverse)
            self.pixels[index + 1] = clamp_byte(
                rgba[1] * alpha + self.pixels[index + 1] * inverse
            )
            self.pixels[index + 2] = clamp_byte(
                rgba[2] * alpha + self.pixels[index + 2] * inverse
            )
            self.pixels[index + 3] = clamp_byte(
                rgba[3] + self.pixels[index + 3] * inverse
            )

    def fill_rect(self, x0, y0, x1, y1, rgba):
        for y in range(max(0, int(y0)), min(self.height, int(math.ceil(y1)))):
            for x in range(max(0, int(x0)), min(self.width, int(math.ceil(x1)))):
                self.blend(x, y, rgba)

    def fill_round_rect(self, x0, y0, x1, y1, radius, color_at):
        left = max(0, int(x0))
        right = min(self.width, int(math.ceil(x1)))
        top = max(0, int(y0))
        bottom = min(self.height, int(math.ceil(y1)))
        radius = min(radius, (x1 - x0) / 2.0, (y1 - y0) / 2.0)
        for y in range(top, bottom):
            for x in range(left, right):
                cx = min(max(x + 0.5, x0 + radius), x1 - radius)
                cy = min(max(y + 0.5, y0 + radius), y1 - radius)
                dx = x + 0.5 - cx
                dy = y + 0.5 - cy
                if dx * dx + dy * dy <= radius * radius:
                    self.blend(x, y, color_at(y))

    def fill_circle(self, cx, cy, radius, color_at):
        left = max(0, int(cx - radius))
        right = min(self.width, int(math.ceil(cx + radius)))
        top = max(0, int(cy - radius))
        bottom = min(self.height, int(math.ceil(cy + radius)))
        radius_sq = radius * radius
        for y in range(top, bottom):
            for x in range(left, right):
                dx = x + 0.5 - cx
                dy = y + 0.5 - cy
                if dx * dx + dy * dy <= radius_sq:
                    self.blend(x, y, color_at(y))

    def arc(self, cx, cy, radius, thickness, start_degrees, end_degrees, rgba):
        left = max(0, int(cx - radius - thickness))
        right = min(self.width, int(math.ceil(cx + radius + thickness)))
        top = max(0, int(cy - radius - thickness))
        bottom = min(self.height, int(math.ceil(cy + radius + thickness)))
        for y in range(top, bottom):
            for x in range(left, right):
                dx = x + 0.5 - cx
                dy = y + 0.5 - cy
                distance = math.sqrt(dx * dx + dy * dy)
                if abs(distance - radius) > thickness / 2.0:
                    continue
                angle = math.degrees(math.atan2(dy, dx)) % 360.0
                if start_degrees <= angle <= end_degrees:
                    self.blend(x, y, rgba)


def vertical_gradient(top, bottom):
    def color_at(y):
        # y is supplied in supersampled pixels by the shape helpers.
        t = min(1.0, max(0.0, y / (SIZE * SUPERSAMPLE - 1)))
        return (
            int(round(top[0] + (bottom[0] - top[0]) * t)),
            int(round(top[1] + (bottom[1] - top[1]) * t)),
            int(round(top[2] + (bottom[2] - top[2]) * t)),
            255,
        )

    return color_at


def paint_background(canvas):
    width = canvas.width
    height = canvas.height
    center_x = width * 0.50
    center_y = height * 0.43
    max_radius = math.hypot(width * 0.52, height * 0.52)
    for y in range(height):
        t = y / (height - 1)
        base = (
            BACKGROUND_TOP[0] + (BACKGROUND_BOTTOM[0] - BACKGROUND_TOP[0]) * t,
            BACKGROUND_TOP[1] + (BACKGROUND_BOTTOM[1] - BACKGROUND_TOP[1]) * t,
            BACKGROUND_TOP[2] + (BACKGROUND_BOTTOM[2] - BACKGROUND_TOP[2]) * t,
        )
        for x in range(width):
            dx = x + 0.5 - center_x
            dy = y + 0.5 - center_y
            distance = math.sqrt(dx * dx + dy * dy) / max_radius
            glow = max(0.0, 1.0 - distance)
            glow *= glow * 0.22
            # A second, tighter lobe keeps the glow from looking like a flat
            # radial fill and gives the tile a little depth.
            tight = max(0.0, 1.0 - distance * 2.5)
            tight *= tight * 0.10
            index = (y * width + x) * 4
            canvas.pixels[index] = clamp_byte(base[0] + GLOW[0] * (glow + tight))
            canvas.pixels[index + 1] = clamp_byte(
                base[1] + GLOW[1] * (glow + tight)
            )
            canvas.pixels[index + 2] = clamp_byte(
                base[2] + GLOW[2] * (glow + tight)
            )
            canvas.pixels[index + 3] = 255


def paint_vignette(canvas):
    width = canvas.width
    height = canvas.height
    center_x = width / 2.0
    center_y = height / 2.0
    max_distance = math.hypot(center_x, center_y)
    for y in range(height):
        for x in range(width):
            distance = math.hypot(x + 0.5 - center_x, y + 0.5 - center_y) / max_distance
            strength = max(0.0, distance - 0.42)
            alpha = int(round(120 * strength * strength))
            if alpha:
                index = (y * width + x) * 4
                inverse = 1.0 - alpha / 255.0
                canvas.pixels[index] = clamp_byte(canvas.pixels[index] * inverse)
                canvas.pixels[index + 1] = clamp_byte(
                    canvas.pixels[index + 1] * inverse
                )
                canvas.pixels[index + 2] = clamp_byte(
                    canvas.pixels[index + 2] * inverse
                )


def paint_mark(canvas):
    scale = SUPERSAMPLE
    # The H sits slightly above centre, leaving visual breathing room below.
    bar_width = 42 * scale
    left_x = 163 * scale
    right_x = 307 * scale
    top = 126 * scale
    bottom = 386 * scale
    bridge_top = 235 * scale
    bridge_bottom = 277 * scale
    # The bridge sits between the rails rather than across their outer edges;
    # a small overlap keeps the join closed after antialiasing.
    bridge_left = left_x + bar_width - 7 * scale
    bridge_right = right_x + 7 * scale
    radius = 18 * scale

    # Soft drop shadow. Several low-alpha offsets approximate a blur while
    # keeping the renderer allocation-free.
    for offset, alpha in ((10 * scale, 55), (18 * scale, 28), (28 * scale, 14)):
        shadow = (0, 0, 0, alpha)
        canvas.fill_round_rect(
            left_x, top + offset, left_x + bar_width, bottom + offset, radius,
            lambda _y, rgba=shadow: rgba,
        )
        canvas.fill_round_rect(
            right_x, top + offset, right_x + bar_width, bottom + offset, radius,
            lambda _y, rgba=shadow: rgba,
        )
        canvas.fill_round_rect(
            bridge_left, bridge_top + offset, bridge_right, bridge_bottom + offset,
            radius, lambda _y, rgba=shadow: rgba,
        )

    white = vertical_gradient(WHITE_TOP, WHITE_BOTTOM)
    blue = vertical_gradient(BLUE_TOP, BLUE_BOTTOM)
    canvas.fill_round_rect(
        left_x, top, left_x + bar_width, bottom, radius, white
    )
    canvas.fill_round_rect(
        right_x, top, right_x + bar_width, bottom, radius, white
    )
    canvas.fill_round_rect(
        bridge_left, bridge_top, bridge_right, bridge_bottom, radius, blue
    )

    # A single narrow highlight along the left rail catches the light like the
    # PS5 shell's white-on-glass icon treatment.
    canvas.fill_round_rect(
        left_x + 8 * scale,
        top + 12 * scale,
        left_x + 13 * scale,
        bottom - 12 * scale,
        2 * scale,
        lambda _y: (255, 255, 255, 110),
    )


def downsample(canvas):
    output = Canvas(SIZE, SIZE)
    factor = SUPERSAMPLE
    for y in range(SIZE):
        for x in range(SIZE):
            totals = [0, 0, 0, 0]
            for offset_y in range(factor):
                for offset_x in range(factor):
                    index = (
                        (y * factor + offset_y) * canvas.width
                        + (x * factor + offset_x)
                    ) * 4
                    for channel in range(4):
                        totals[channel] += canvas.pixels[index + channel]
            count = factor * factor
            index = (y * SIZE + x) * 4
            for channel in range(4):
                output.pixels[index + channel] = totals[channel] // count
    return output


def render():
    canvas = Canvas(SIZE * SUPERSAMPLE, SIZE * SUPERSAMPLE)
    paint_background(canvas)
    paint_vignette(canvas)
    paint_mark(canvas)
    return downsample(canvas)


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
