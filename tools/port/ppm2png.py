"""Convert PPM/PGM dumps written by the tests to PNG (optionally scaled): python ppm2png.py in.ppm [out.png] [scale]"""
import sys, pathlib
from PIL import Image

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].isdigit() else src.with_suffix(".png")
scale = int(sys.argv[-1]) if sys.argv[-1].isdigit() else 1
im = Image.open(src)
if scale > 1:
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
im.save(dst)
print(dst)
