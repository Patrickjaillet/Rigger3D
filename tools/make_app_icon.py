#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Génère l'icône de l'application (assets/app/rigger.svg, rigger-256.png, rigger.ico) sans dépendance externe.

Dessin : carré arrondi bleu, un os (capsule diagonale + deux articulations) et un point d'articulation central.
Rastérisation par champs de distance signés avec suréchantillonnage 4x4 ; PNG écrits avec zlib ; ICO = conteneur de PNG.
"""
import math
import os
import struct
import sys
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]
BLUE = (47, 111, 237)
DARK = (28, 38, 66)
WHITE = (255, 255, 255)
ACCENT = (255, 196, 61)


def sd_round_box(px, py, half, radius):
    dx = abs(px) - (half - radius)
    dy = abs(py) - (half - radius)
    outside = math.hypot(max(dx, 0.0), max(dy, 0.0))
    return outside + min(max(dx, dy), 0.0) - radius


def sd_segment(px, py, ax, ay, bx, by):
    pax, pay, bax, bay = px - ax, py - ay, bx - ax, by - ay
    h = max(0.0, min(1.0, (pax * bax + pay * bay) / (bax * bax + bay * bay)))
    return math.hypot(pax - bax * h, pay - bay * h)


def shade(u, v):
    """Couleur RGBA au point (u, v) dans [-1, 1]^2 (pas d'anticrénelage ici : fait par suréchantillonnage)."""
    if sd_round_box(u, v, 0.96, 0.34) > 0:
        return (0, 0, 0, 0)
    t = (v + 1) / 2
    base = tuple(int(BLUE[i] * (1 - t) + DARK[i] * t) for i in range(3))
    # point d'articulation central (couleur d'accent), au-dessus de l'os
    if math.hypot(u, v) < 0.09:
        return ACCENT + (255,)
    # os : capsule du bas-gauche au haut-droit, rayon 0,12, extrémités plus épaisses (articulations)
    ax, ay, bx, by = -0.50, 0.50, 0.50, -0.50
    if sd_segment(u, v, ax, ay, bx, by) < 0.12:
        return WHITE + (255,)
    for cx, cy in ((ax, ay), (bx, by)):
        if math.hypot(u - cx, v - cy) < 0.24:
            return WHITE + (255,)
    return base + (255,)


def render(size, samples=4):
    pixels = bytearray()
    for y in range(size):
        row = bytearray()
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(samples):
                for sx in range(samples):
                    u = ((x + (sx + 0.5) / samples) / size) * 2 - 1
                    v = ((y + (sy + 0.5) / samples) / size) * 2 - 1
                    r, g, b, a = shade(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = samples * samples
            alpha = acc[3] / n
            if acc[3] > 0:
                row += bytes((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(alpha)))
            else:
                row += bytes((0, 0, 0, 0))
        pixels += b"\x00" + row  # filtre PNG « None »
    return bytes(pixels)


def png(size, raw):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def ico(images):
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    blobs = b""
    for size, data in images:
        out += struct.pack("<BBBBHHII", 0 if size >= 256 else size, 0 if size >= 256 else size, 0, 0, 1, 32, len(data), offset + len(blobs))
        blobs += data
    return out + blobs


SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="-1 -1 2 2" width="256" height="256">
  <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2f6fed"/><stop offset="1" stop-color="#1c2642"/></linearGradient></defs>
  <rect x="-0.96" y="-0.96" width="1.92" height="1.92" rx="0.34" fill="url(#g)"/>
  <line x1="-0.5" y1="0.5" x2="0.5" y2="-0.5" stroke="#fff" stroke-width="0.24" stroke-linecap="round"/>
  <circle cx="-0.5" cy="0.5" r="0.24" fill="#fff"/><circle cx="0.5" cy="-0.5" r="0.24" fill="#fff"/>
  <circle cx="0" cy="0" r="0.10" fill="#ffc43d"/>
</svg>
"""


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "app")
    os.makedirs(root, exist_ok=True)
    images = []
    for size in SIZES:
        data = png(size, render(size))
        images.append((size, data))
        if size == 256:
            open(os.path.join(root, "rigger-256.png"), "wb").write(data)
        if size == 64:
            open(os.path.join(root, "rigger-64.png"), "wb").write(data)
    open(os.path.join(root, "rigger.ico"), "wb").write(ico(images))
    open(os.path.join(root, "rigger.svg"), "w", encoding="utf-8").write(SVG)
    print("icônes écrites dans", os.path.normpath(root))


if __name__ == "__main__":
    sys.exit(main())
