"""Render a labelled contact sheet of object frames (for visual verification).
Usage: python contact.py <Resources> <frames_uhd.json> <raw_toolbox.json> <ids comma|range a-b> <out.png> [cell]"""
import sys, json, os, plistlib
from PIL import Image, ImageDraw
res, fj, tj, idspec, out = sys.argv[1:6]
cell = int(sys.argv[6]) if len(sys.argv) > 6 else 96
frames = json.load(open(fj)); tb = json.load(open(tj))
ids = []
for part in idspec.split(','):
    if '-' in part:
        a, b = map(int, part.split('-')); ids += list(range(a, b + 1))
    else:
        ids.append(int(part))
ids = [i for i in ids if str(i) in tb]
sheets = {}
def get_sheet(pl):
    if pl not in sheets:
        d = plistlib.load(open(os.path.join(res, pl), 'rb'))
        tex = d.get('metadata', {}).get('textureFileName') or pl.replace('.plist', '.png')
        sheets[pl] = Image.open(os.path.join(res, tex)).convert('RGBA')
    return sheets[pl]
def frame_img(name):
    f = frames.get(name)
    if not f: return None
    img = get_sheet(f['sheet']); x, y, w, h = [int(v) for v in f['rect']]
    crop = img.crop((x, y, x + h, y + w)).rotate(90, expand=True) if f['rotated'] else img.crop((x, y, x + w, y + h))
    sw, sh = [int(v) for v in f['sourceSize']]; ox, oy = f['offset']
    canvas = Image.new('RGBA', (max(sw, 1), max(sh, 1)), (0, 0, 0, 0))
    canvas.paste(crop, (int(round((sw - crop.width) / 2 + ox)), int(round((sh - crop.height) / 2 - oy))), crop)
    return canvas
cols = 12
rows = (len(ids) + cols - 1) // cols
sheet = Image.new('RGBA', (cols * cell, rows * (cell + 14)), (60, 60, 90, 255))
d = ImageDraw.Draw(sheet)
for n, i in enumerate(ids):
    im = frame_img(tb[str(i)])
    cx, cy = (n % cols) * cell, (n // cols) * (cell + 14)
    d.rectangle([cx, cy, cx + cell - 1, cy + cell - 1], outline=(90, 90, 120, 255))
    if im:
        # scale: 120px (=30 units) -> cell/2 so relative sizes are visible; clamp big ones
        s = min((cell / 2) / 120 * 1.0 * 1, (cell - 4) / max(im.width, im.height))
        if s * max(im.width, im.height) < 6: s = 6 / max(im.width, im.height)
        im2 = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))))
        sheet.paste(im2, (cx + (cell - im2.width) // 2, cy + (cell - im2.height) // 2), im2)
    d.text((cx + 2, cy + cell), str(i), fill=(255, 255, 0, 255))
sheet.save(out)
print(out, len(ids))
