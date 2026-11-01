"""efontCN_12 text widths and glyph presence, a port of firmware/sim/lib/simfb/efont.cpp.

Usage: import efont; f = efont.Font(path_to_efont_cn12_c); f.width("Enter"); f.has(0x266A)
"""
import re

K_BITS_W, K_BITS_H, K_BITS_X, K_BITS_Y, K_BITS_DX = 4, 5, 6, 7, 8
K_MAX_W, K_POS_UA, K_POS_LA, K_POS_U, K_GLYPHS = 9, 17, 19, 21, 23


def load_c_array(path):
    src = open(path, encoding="utf-8").read()
    body = src[src.index("=") + 1: src.rindex(";")]
    out = bytearray()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body, re.S):
        i = 0
        while i < len(lit):
            c = lit[i]
            if c != "\\":
                out += c.encode("latin1"); i += 1; continue
            n = lit[i + 1]
            if n in "01234567":
                m = re.match(r"[0-7]{1,3}", lit[i + 1:]).group(0)
                out.append(int(m, 8)); i += 1 + len(m)
            elif n == "x":
                m = re.match(r"[0-9a-fA-F]+", lit[i + 2:]).group(0)
                out.append(int(m, 16) & 0xFF); i += 2 + len(m)
            else:
                out += {"n": b"\n", "t": b"\t", "r": b"\r", "\\": b"\\", '"': b'"', "'": b"'", "?": b"?",
                        "a": b"\a", "b": b"\b", "f": b"\f", "v": b"\v"}[n]
                i += 2
    return bytes(out)


class Bits:
    def __init__(self, data, pos):
        self.d, self.p, self.bit = data, pos, 0

    def get(self, cnt):
        val = self.d[self.p] >> self.bit
        end = self.bit + cnt
        if end >= 8:
            end -= 8
            self.p += 1
            val |= self.d[self.p] << (8 - self.bit)
        self.bit = end
        return val & ((1 << cnt) - 1)

    def get_signed(self, cnt):
        return self.get(cnt) - (1 << (cnt - 1))


def be16(d, p):
    return (d[p] << 8) + d[p + 1]


class Font:
    def __init__(self, path):
        self.f = load_c_array(path) + b"\0\0"  # the C string literal's NUL

    def glyph(self, cp):
        f = self.f
        font = K_GLYPHS
        if cp <= 255:
            if cp >= ord("a"):
                font += be16(f, K_POS_LA)
            elif cp >= ord("A"):
                font += be16(f, K_POS_UA)
            while f[font + 1]:
                if f[font] == cp:
                    return font + 2
                font += f[font + 1]
            return None
        if cp > 0xFFFF:
            return None
        font += be16(f, K_POS_U)
        lut = font
        while True:
            font += be16(f, lut)
            e = be16(f, lut + 2)
            lut += 4
            if e >= cp:
                break
        while True:
            e = be16(f, font)
            if e == 0:
                return None
            if e == cp:
                return font + 3
            font += f[font + 2]

    def metric(self, cp):
        g = self.glyph(cp)
        if g is None:
            mw = self.f[K_MAX_W]
            return None
        b = Bits(self.f, g)
        w = b.get(self.f[K_BITS_W]); b.get(self.f[K_BITS_H])
        xo = b.get_signed(self.f[K_BITS_X]); b.get_signed(self.f[K_BITS_Y])
        adv = b.get_signed(self.f[K_BITS_DX])
        return w, xo, adv

    def has(self, cp):
        return self.glyph(cp) is not None

    def width(self, s):
        """LGFXBase::text_width over one run; missing glyphs drawn as the box (6 px, like the sim)."""
        left = right = 0
        for ch in s:
            cp = ord(ch)
            if 0xFE00 <= cp < 0xFE10:
                continue
            m = self.metric(cp) if cp >= 0x20 else None
            if m is None:
                m = self.metric(0x25A1)  # the sim draws □ for a missing glyph
            w, xo, adv = m
            if left == 0 and right == 0 and xo < 0:
                left = right = -xo
            right = left + max(adv, w + xo)
            left += adv
        return right
