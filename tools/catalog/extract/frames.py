"""Collect all sprite frames from GD's -uhd plists (read-only)."""
import sys, plistlib, json, os, re, glob
res = sys.argv[1]; out = sys.argv[2]
frames = {}
sheets = sorted(glob.glob(os.path.join(res, '*-uhd.plist')))
def pair(s):
    return [float(x) for x in re.findall(r'-?[\d.]+', s)]
for p in sheets:
    try:
        d = plistlib.load(open(p, 'rb'))
    except Exception as e:
        continue
    if not isinstance(d, dict) or 'frames' not in d: continue
    sheet = os.path.basename(p)
    for name, f in d['frames'].items():
        if not isinstance(f, dict): continue
        src = f.get('spriteSourceSize') or f.get('sourceSize')
        size = f.get('spriteSize')
        rect = f.get('textureRect') or f.get('frame')
        if src is None: continue
        e = {'sheet': sheet, 'sourceSize': pair(src), 'spriteSize': pair(size) if size else None,
             'offset': pair(f.get('spriteOffset', '{0,0}')), 'rotated': bool(f.get('textureRotated', f.get('rotated', False))),
             'rect': pair(rect) if rect else None}
        if name in frames and frames[name]['sheet'] != sheet:
            frames[name].setdefault('also_in', []).append(sheet)
        else:
            frames[name] = e
json.dump(frames, open(out, 'w'), indent=0)
print(len(sheets), 'sheets', len(frames), 'frames')
for n in ['square_01_001.png', 'spike_01_001.png', 'sawblade_01_001.png', 'portal_01_front_001.png', 'blockOutline_01_001.png']:
    print(n, frames.get(n))
