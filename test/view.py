#!/usr/bin/env python3
"""A captured panel page (raw BGRA, 320 x 375 portrait, as fbshim.so writes it) back to the UI's
375 x 320 landscape view, as a PNG at 2x. The glass's 80 px rounded corners are shaded red, so
anything cut off by them shows. Usage: view.py page.raw out.png"""
import sys
from PIL import Image, ImageDraw

def convert(src, dst):
    raw = open(src, 'rb').read()
    panel = Image.frombytes('RGBA', (320, 375), raw, 'raw', 'BGRA').convert('RGB')
    # view (X, Y) is panel row X, column 319 - Y: the panel turned anticlockwise
    view = panel.transpose(Image.Transpose.ROTATE_90)  # (x, y) -> (y, 319 - x)
    assert view.size == (375, 320)
    mask = Image.new('L', view.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, 374, 319], radius=80, fill=255)
    red = Image.new('RGB', view.size, (120, 0, 0))
    shown = Image.composite(view, Image.blend(view, red, 0.65), mask)
    shown.resize((750, 640), Image.NEAREST).save(dst)

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
