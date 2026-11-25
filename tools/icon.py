#!/usr/bin/env python3
"""The card's icons, drawn: a 3 x 3 board of tiles, some green, some yellow, some grey, in a
rounded square. card/icon.png (52 px, Library's Games row) and card/icon-40.png (40 px).
Usage: tools/icon.py"""
import pathlib
from PIL import Image, ImageDraw

G, Y, X, E = (106, 170, 100), (201, 180, 88), (120, 124, 126), (255, 255, 255)
BOARD = [[X, Y, X], [G, X, Y], [G, G, G]]

def icon(size):
    S = size * 8  # drawn large, then scaled down smooth
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=S * 0.22, fill=(255, 255, 255, 255))
    t, gap = S * 0.235, S * 0.045
    x0 = (S - 3 * t - 2 * gap) / 2
    for r, row in enumerate(BOARD):
        for c, col in enumerate(row):
            x, y = x0 + c * (t + gap), x0 + r * (t + gap)
            d.rectangle([x, y, x + t, y + t], fill=col)
    return im.resize((size, size), Image.LANCZOS)

card = pathlib.Path(__file__).resolve().parent.parent / 'card'
icon(52).save(card / 'icon.png')
icon(40).save(card / 'icon-40.png')
print('icons: card/icon.png, card/icon-40.png')
