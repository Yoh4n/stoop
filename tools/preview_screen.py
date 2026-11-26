#!/usr/bin/env python3
"""Render a PNG preview of the train arrival screen without hardware.

Decodes the same U8g2 fonts the firmware uses (from .pio/libdeps/.../U8g2) and
mirrors the layout math in src/apps/train_tracker.cpp. Keep the two in sync if
you change the layout.

Usage: python3 tools/preview_screen.py [out.png]   (run `pio run` once first)
"""

import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONT_SRC = ROOT / ".pio/libdeps/esp32-c6/U8g2/src/clib/u8g2_fonts.c"
W, H = 320, 172

# ---------------------------------------------------------------- U8g2 fonts
_src = None


def load_font(name):
    global _src
    if _src is None:
        _src = FONT_SRC.read_text(encoding="latin-1")
    m = re.search(
        r"u8g2_font_"
        + name
        + r'\[\d+\] U8G2_FONT_SECTION\("u8g2_font_'
        + name
        + r'"\) =\s*((?:"(?:[^"\\]|\\.)*"\s*)+);',
        _src,
    )
    raw = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
    return Font(raw.encode("latin-1").decode("unicode_escape").encode("latin-1"))


class Bits:
    def __init__(self, data, pos):
        self.d, self.p, self.bit = data, pos, 0

    def u(self, n):
        v = self.d[self.p] >> self.bit
        end = self.bit + n
        if end >= 8:
            self.p += 1
            v |= self.d[self.p] << (8 - self.bit)
            end -= 8
        self.bit = end
        return v & ((1 << n) - 1)

    def s(self, n):
        return self.u(n) - (1 << (n - 1))


class Font:
    def __init__(self, data):
        self.d = data
        (self.b0, self.b1, self.bw, self.bh, self.bx, self.by, self.bd) = data[2:9]
        self.cap = struct.unpack("b", data[13:14])[0]
        self.glyphs = {}
        p = 23
        while p < len(data) and data[p + 1]:
            self.glyphs[data[p]] = p
            p += data[p + 1]

    def glyph(self, ch):
        p = self.glyphs.get(ord(ch))
        if p is None:
            return None
        b = Bits(self.d, p + 2)
        w, h = b.u(self.bw), b.u(self.bh)
        gx, gy, dx = b.s(self.bx), b.s(self.by), b.s(self.bd)
        px, total, x, y = [], w * h, 0, 0
        while len(px) < total and w:
            a, c = b.u(self.b0), b.u(self.b1)
            while True:
                px += [0] * a + [1] * c
                if b.u(1) == 0:
                    break
        return w, h, gx, gy, dx, px[:total]

    def width(self, text):
        return sum((self.glyph(c) or (0, 0, 0, 0, 0, []))[4] for c in text)


# ---------------------------------------------------------------- canvas
def rgb(hexv):  # quantize through RGB565 like the panel does
    r, g, b = hexv >> 16, (hexv >> 8) & 255, hexv & 255
    r, g, b = r >> 3, g >> 2, b >> 3
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


WHITE, BLACK = rgb(0xFFFFFF), rgb(0)


class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.px = [[BLACK] * w for _ in range(h)]

    def rect(self, x, y, w, h, c):
        for yy in range(max(0, y), min(self.h, y + h)):
            for xx in range(max(0, x), min(self.w, x + w)):
                self.px[yy][xx] = c

    def circle(self, cx, cy, r, c, fill):
        for yy in range(cy - r, cy + r + 1):
            for xx in range(cx - r, cx + r + 1):
                d = (xx - cx) ** 2 + (yy - cy) ** 2
                if (d <= r * r + r) if fill else (r * r - r <= d <= r * r + r):
                    self.px[yy][xx] = c

    def text(self, font, s, x, base, c, align=-1):
        if align >= 0:
            x -= font.width(s) // (2 if align == 0 else 1)
        for ch in s:
            g = font.glyph(ch)
            if not g:
                continue
            w, h, gx, gy, dx, bits = g
            top = base - (h + gy)
            for i, on in enumerate(bits):
                if on:
                    xx, yy = x + gx + i % w, top + i // w
                    if 0 <= xx < self.w and 0 <= yy < self.h:
                        self.px[yy][xx] = c
            x += dx


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


# ---------------------------------------------------------------- layout (mirrors train_tracker.cpp)
F = {
    n: load_font(n)
    for n in [
        "helvB18_tr",
        "helvB14_tr",
        "helvR12_tr",
        "fub30_tr",
        "fub25_tr",
        "fub20_tr",
        "fub17_tr",
        "logisoso42_tr",
        "logisoso50_tn",
    ]
}
LINE = {
    "Red": 0xC60C30,
    "Blue": 0x00A1DE,
    "Brn": 0x62361B,
    "G": 0x009B3A,
    "Org": 0xF9461C,
    "Pink": 0xE27EA6,
    "P": 0x522398,
    "Y": 0xF9E300,
}


def arrival_screen(
    route,
    station,
    run,
    dest,
    mins,
    index,
    count,
    approaching=False,
    delayed=False,
    scheduled=False,
):
    cv = Canvas()
    color = LINE[route]
    muted = rgb(0x9A9A9A)
    header_h, header_max = 42, W - 20
    cv.rect(0, 0, W, header_h, rgb(color))
    hf, name = F["helvB18_tr"], station
    if hf.width(name) > header_max:
        hf = F["helvB14_tr"]
        while len(name) > 1 and hf.width(name + "...") > header_max:
            name = name[:-1]
        if name != station:
            name += "..."
    cv.text(hf, name, 10, (header_h + hf.cap) // 2, BLACK if route == "Y" else WHITE)

    k_right, k_left, cy = 308, 12, 97
    due = approaching or mins <= 1
    if due:
        f = F["logisoso42_tr"]
        right_w = f.width("Due")
        cv.text(f, "Due", k_right, cy + f.cap // 2, rgb(0xF9E300), 1)
    else:
        fn, fm, gap = F["logisoso50_tn"], F["helvB14_tr"], 10
        num_base = cy - (fn.cap + gap + fm.cap) // 2 + fn.cap
        right_w = max(fn.width(str(mins)), fm.width("min"))
        cv.text(fn, str(mins), k_right, num_base, WHITE, 1)
        cv.text(fm, "min", k_right, num_base + gap + fm.cap, muted, 1)

    max_w = k_right - right_w - 14 - k_left
    for name in ["fub30_tr", "fub25_tr", "fub20_tr", "fub17_tr"]:
        df = F[name]
        if df.width(dest) <= max_w:
            break
    if df.width(dest) > max_w:
        while len(dest) > 1 and df.width(dest + "...") > max_w:
            dest = dest[:-1]
        dest += "..."

    fr = F["helvR12_tr"]
    run_base = cy - (fr.cap + 12 + df.cap) // 2 + fr.cap
    dest_base = run_base + 12 + df.cap
    cv.text(fr, f"Run #{run} to", k_left, run_base, muted)
    cv.text(df, dest, k_left, dest_base, WHITE)
    flag = "Delayed" if delayed else "Scheduled" if scheduled else None
    if flag:
        cv.text(
            fr,
            flag,
            k_left,
            dest_base + 12 + fr.cap,
            rgb(0xF9A01C) if delayed else muted,
        )

    spacing, dy = 16, 161
    cx = W // 2 - (count - 1) * spacing // 2
    for i in range(count):
        if i == index:
            cv.circle(
                cx,
                dy,
                4,
                rgb(0xFFFFFF if color in (0x62361B, 0x522398) else color),
                True,
            )
        else:
            cv.circle(cx, dy, 4, muted, False)
        cx += spacing
    return cv


def sheet(screens, captions, scale=2, pad=24, out="preview.png"):
    cap_font = F["helvR12_tr"]
    sw, sh = W * scale, H * scale
    bezel = 10
    cols = 2
    rows_n = (len(screens) + cols - 1) // cols
    cell_w, cell_h = sw + 2 * bezel + pad, sh + 2 * bezel + pad + 24
    page = Canvas(cols * cell_w + pad, rows_n * cell_h + pad)
    page.rect(0, 0, page.w, page.h, rgb(0xEEEEEE))
    for i, (cv, caption) in enumerate(zip(screens, captions)):
        ox = pad + (i % cols) * cell_w
        oy = pad + (i // cols) * cell_h
        page.rect(ox, oy, sw + 2 * bezel, sh + 2 * bezel, rgb(0x1A1A1A))
        for y in range(sh):
            row = cv.px[y // scale]
            dst = page.px[oy + bezel + y]
            for x in range(sw):
                dst[ox + bezel + x] = row[x // scale]
        page.text(cap_font, caption, ox, oy + sh + 2 * bezel + 18, rgb(0x333333))
    write_png(out, page.px)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "docs/screen_preview.png")
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    screens = [
        (
            arrival_screen("Red", "Sheridan", "812", "Howard", 4, 0, 3),
            "Red Line, 4 min (1 of 3)",
        ),
        (
            arrival_screen("Red", "Addison", "815", "Howard", 1, 1, 3, approaching=True),
            "Due (2 of 3)",
        ),
        (
            arrival_screen(
                "Brn",
                "Harold Washington Library-State/Van Buren",
                "407",
                "Kimball",
                12,
                2,
                3,
                delayed=True,
            ),
            "Brown Line, long station, delayed",
        ),
        (
            arrival_screen(
                "Y", "Howard", "591", "Dempster-Skokie", 18, 0, 6, scheduled=True
            ),
            "Yellow Line, long name, 6 trains",
        ),
    ]
    sheet([s for s, _ in screens], [c for _, c in screens], out=out)
    print("wrote", out)
