"""Combine the preview's PPM renders into one PNG (needs Pillow).

    python3 contact_sheet.py out/480 sheet.png
"""
import glob
import sys

from PIL import Image

prefix, target = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(prefix + "_*.ppm"),
               key=lambda p: int(p.rsplit("/", 1)[-1].split("_")[1]))
images = [Image.open(f) for f in files]
size = images[0].width
cols = 3
rows = (len(images) + cols - 1) // cols
sheet = Image.new("RGB", (size * cols, size * rows))
for i, image in enumerate(images):
    sheet.paste(image, ((i % cols) * size, (i // cols) * size))
sheet.save(target)
print("wrote", target)
