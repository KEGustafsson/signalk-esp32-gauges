"""Combine the preview's PPM renders into one PNG (needs Pillow).

    python3 contact_sheet.py out/480 sheet.png
"""
import glob
import sys

from PIL import Image

prefix, target = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(prefix + "_*.ppm"),
               key=lambda p: int(p.rsplit("/", 1)[-1].split("_")[1]))
if not files:
    sys.exit("no %s_*.ppm renders" % prefix)
images = [Image.open(f).convert("RGB") for f in files]
for f, image in zip(files, images):
    # A page that failed to draw comes out as one flat colour.
    if all(lo == hi for lo, hi in image.getextrema()):
        sys.exit("%s is blank" % f)
size = images[0].width
cols = 3
rows = (len(images) + cols - 1) // cols
sheet = Image.new("RGB", (size * cols, size * rows))
for i, image in enumerate(images):
    sheet.paste(image, ((i % cols) * size, (i // cols) * size))
sheet.save(target)
print("wrote", target)
