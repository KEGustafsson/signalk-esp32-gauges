"""Combine the preview's PPM renders into one PNG (needs Pillow).

    python3 contact_sheet.py out/480 sheet.png
    python3 contact_sheet.py out/radio720 sheet.png --tile 480 \
        --ids crew,crew_receiving,crew_mic_gain

--ids picks renders by their id (the part after the number), in that
order; --tile scales each render to that many pixels square.
"""
import argparse
import glob

from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("prefix")
ap.add_argument("target")
ap.add_argument("--tile", type=int, default=0)
ap.add_argument("--ids", default="")
ap.add_argument("--cols", type=int, default=3)
args = ap.parse_args()


def number(path):
    return int(path.replace("\\", "/").rsplit("/", 1)[-1].split("_")[1])


def ident(path):
    name = path.replace("\\", "/").rsplit("/", 1)[-1][: -len(".ppm")]
    return name.split("_", 2)[2]


files = sorted(glob.glob(args.prefix + "_*.ppm"), key=number)
if args.ids:
    by_id = {ident(f): f for f in files}
    wanted = args.ids.split(",")
    missing = [i for i in wanted if i not in by_id]
    if missing:
        raise SystemExit("no render for %s" % ", ".join(missing))
    files = [by_id[i] for i in wanted]
if not files:
    raise SystemExit("no %s_*.ppm renders" % args.prefix)
images = [Image.open(f).convert("RGB") for f in files]
for f, image in zip(files, images):
    # A page that failed to draw comes out as one flat colour.
    if all(lo == hi for lo, hi in image.getextrema()):
        raise SystemExit("%s is blank" % f)
if args.tile:
    images = [im.resize((args.tile, args.tile), Image.LANCZOS) for im in images]
size = images[0].width
cols = args.cols
rows = (len(images) + cols - 1) // cols
sheet = Image.new("RGB", (size * cols, size * rows))
for i, image in enumerate(images):
    sheet.paste(image, ((i % cols) * size, (i // cols) * size))
sheet.save(args.target, optimize=True)
print("wrote", args.target)
