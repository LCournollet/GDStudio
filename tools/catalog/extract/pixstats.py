"""Pixel statistics of sprite frames (read-only on GD Resources).
Usage: python pixstats.py <Resources dir> <frames_uhd.json> <frame list json> <out.json>"""
import sys, json, os, plistlib, colorsys
from PIL import Image
res, fj, lj, out = sys.argv[1:5]
frames = json.load(open(fj)); want = json.load(open(lj))
sheets = {}
def sheet_img(plist):
    if plist not in sheets:
        d = plistlib.load(open(os.path.join(res, plist), 'rb'))
        tex = d.get('metadata', {}).get('textureFileName') or plist.replace('.plist', '.png')
        sheets[plist] = Image.open(os.path.join(res, tex)).convert('RGBA')
    return sheets[plist]
stats = {}
for name in want:
    f = frames.get(name)
    if not f or not f.get('rect'): continue
    img = sheet_img(f['sheet'])
    x, y, w, h = [int(v) for v in f['rect']]
    if f['rotated']:
        crop = img.crop((x, y, x + h, y + w)).rotate(90, expand=True)
    else:
        crop = img.crop((x, y, x + w, y + h))
    px = list(crop.getdata())
    n = len(px) or 1
    op = [(r, g, b, a) for r, g, b, a in px if a > 32]
    cov = len(op) / n
    if op:
        sat = sum(colorsys.rgb_to_hsv(r/255, g/255, b/255)[1] * a for r, g, b, a in op) / sum(a for *_, a in op)
        val = sum(max(r, g, b) / 255 * a for r, g, b, a in op) / sum(a for *_, a in op)
        avga = sum(a for *_, a in op) / len(op) / 255
    else:
        sat = val = avga = 0
    bbox = crop.getchannel('A').point(lambda a: 255 if a > 32 else 0).getbbox()
    sw, sh = f['sourceSize']
    stats[name] = {'trim_px': [w, h], 'source_px': [sw, sh], 'coverage_trim': round(cov, 3),
                   'coverage_source': round(cov * w * h / (sw * sh), 3) if sw * sh else None,
                   'mean_saturation': round(sat, 3), 'mean_value': round(val, 3), 'mean_alpha': round(avga, 3),
                   'visible_bbox_px': [bbox[2] - bbox[0], bbox[3] - bbox[1]] if bbox else [0, 0]}
json.dump(stats, open(out, 'w'), indent=0)
print(len(stats))
