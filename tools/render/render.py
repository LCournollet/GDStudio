"""Offline preview of a GD level string, drawn with the game's own sprites.

    python tools/render/render.py LEVEL.txt OUT.png [--resources "<GD>/Resources"]
                                  [--x0 0 --x1 3000] [--ppu 1.0] [--only-gameplay] [--mark-generated]

It approximates the game's renderer (main sprite + detail sprite, channel color,
channel opacity, additive blending, HSV brightness, Z layer / Z order, rotation,
scale, flips). Not rendered: shaders, pulses, animations, object glow sprites,
camera effects. Good enough to judge composition, alignment and readability.
"""
import argparse
import json
import os
import plistlib
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
DEFAULT_RES = r"D:\SteamLibrary\steamapps\common\Geometry Dash\Resources"

ZRANK = {-5: 0, -3: 1, -1: 2, 1: 3, 3: 4, 0: 3, 5: 5, 7: 6, 9: 7, 11: 8, 4: 4}


def parse_level(text):
    parts = text.strip().split(";")
    header = {}
    objects = []
    start = 0
    if parts and parts[0].startswith("k"):
        kv = parts[0].split(",")
        header = {kv[i]: kv[i + 1] for i in range(0, len(kv) - 1, 2)}
        start = 1
    for p in parts[start:]:
        if not p:
            continue
        kv = p.split(",")
        d = {}
        for i in range(0, len(kv) - 1, 2):
            try:
                d[int(kv[i])] = kv[i + 1]
            except ValueError:
                pass
        if 1 in d:
            objects.append(d)
    return header, objects


def parse_colors(header):
    chans = {
        1: ((255, 255, 255), 1.0, False), 1004: ((255, 255, 255), 1.0, False), 1005: ((125, 255, 0), 1.0, False),
        1006: ((0, 255, 255), 1.0, False), 1010: ((0, 0, 0), 1.0, False), 1011: ((255, 255, 255), 1.0, False),
        1000: ((40, 125, 255), 1.0, False), 1001: ((0, 102, 255), 1.0, False), 1002: ((255, 255, 255), 1.0, False),
        1009: ((0, 102, 255), 1.0, False), 1003: ((255, 255, 255), 1.0, False), 1012: ((255, 255, 255), 0.5, True),
    }
    raw = header.get("kS38", "")
    for entry in raw.split("|"):
        if not entry:
            continue
        kv = entry.split("_")
        d = {}
        for i in range(0, len(kv) - 1, 2):
            d[kv[i]] = kv[i + 1]
        if "6" not in d:
            continue
        cid = int(float(d["6"]))
        rgb = (int(float(d.get("1", 255))), int(float(d.get("2", 255))), int(float(d.get("3", 255))))
        chans[cid] = (rgb, float(d.get("7", 1)), d.get("5", "0") == "1")
    return chans


class Sprites:
    def __init__(self, res):
        self.res = res
        self.frames = json.load(open(os.path.join(DATA, "frames.json")))
        self.sheets = {}
        self.cache = {}

    def sheet(self, pl):
        if pl not in self.sheets:
            d = plistlib.load(open(os.path.join(self.res, pl), "rb"))
            tex = d.get("metadata", {}).get("textureFileName") or pl.replace(".plist", ".png")
            self.sheets[pl] = Image.open(os.path.join(self.res, tex)).convert("RGBA")
        return self.sheets[pl]

    def get(self, name):
        """Sprite with its source-size padding (so its center is the object position)."""
        if name in self.cache:
            return self.cache[name]
        f = self.frames.get(name)
        img = None
        if f and f.get("rect"):
            sh = self.sheet(f["sheet"])
            x, y, w, h = [int(v) for v in f["rect"]]
            crop = sh.crop((x, y, x + h, y + w)).rotate(90, expand=True) if f["rotated"] else sh.crop((x, y, x + w, y + h))
            sw, shh = [int(v) for v in f["sourceSize"]]
            ox, oy = f["offset"]
            img = Image.new("RGBA", (max(sw, 1), max(shh, 1)), (0, 0, 0, 0))
            img.paste(crop, (int(round((sw - crop.width) / 2 + ox)), int(round((shh - crop.height) / 2 - oy))), crop)
        self.cache[name] = img
        return img


def fnum(d, k, default):
    try:
        return float(d.get(k, default))
    except ValueError:
        return default


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("out")
    ap.add_argument("--resources", default=DEFAULT_RES)
    ap.add_argument("--x0", type=float)
    ap.add_argument("--x1", type=float)
    ap.add_argument("--y0", type=float, default=-60)
    ap.add_argument("--y1", type=float)
    ap.add_argument("--ppu", type=float, default=1.0, help="pixels per GD unit")
    ap.add_argument("--only-gameplay", action="store_true")
    ap.add_argument("--mark-generated", type=str, default="", help="comma separated tag groups to outline in red")
    args = ap.parse_args()

    header, objects = parse_level(open(args.level).read())
    chans = parse_colors(header)
    objinfo = json.load(open(os.path.join(DATA, "objects.json")))
    sprites = Sprites(args.resources)

    xs = [fnum(o, 2, 0) for o in objects] or [0]
    ys = [fnum(o, 3, 0) for o in objects] or [0]
    X0 = args.x0 if args.x0 is not None else min(xs) - 150
    X1 = args.x1 if args.x1 is not None else max(xs) + 150
    Y0 = args.y0
    Y1 = args.y1 if args.y1 is not None else max(max(ys) + 150, 400)
    s = args.ppu
    W, H = int((X1 - X0) * s), int((Y1 - Y0) * s)
    canvas = np.zeros((H, W, 3), dtype=np.float32)
    bg = np.array(chans[1000][0], dtype=np.float32) / 255
    canvas[:, :] = bg

    tags = set(int(t) for t in args.mark_generated.split(",") if t)
    order = []
    for i, o in enumerate(objects):
        oid = int(float(o[1]))
        info = objinfo.get(str(oid))
        if not info:
            continue
        is_deco = info["cat"] == "decoration"
        groups = set(int(g) for g in o.get(57, "").split(".") if g)
        if args.only_gameplay and (is_deco and groups & tags):
            continue
        z = int(float(o.get(24, 0)))
        zo = int(float(o.get(25, 0)))
        if z == 0:
            z = 1  # unknown default: B2
        order.append((ZRANK.get(z, 3), zo, i, o, info, groups))
    order.sort(key=lambda t: (t[0], t[1], t[2]))

    ground_drawn = False
    for rank, zo, i, o, info, groups in order:
        if rank >= 3 and not ground_drawn:
            pass
        oid = int(float(o[1]))
        frame = info["frame"]
        img = sprites.get(frame)
        if img is None:
            continue
        sx = fnum(o, 128, fnum(o, 32, 1.0))
        sy = fnum(o, 129, fnum(o, 32, 1.0))
        rot = fnum(o, 6, 0)
        fx, fy = o.get(4) == "1", o.get(5) == "1"
        main_ch = int(float(o.get(21, 0))) or (info.get("col") or 1004)
        detail_ch = int(float(o.get(22, 0))) or 1
        layers = [(img, main_ch)]
        dname = frame.replace("_001.png", "_color_001.png")
        dimg = sprites.get(dname) if dname in sprites.frames else None
        if dimg is not None:
            layers.append((dimg, detail_ch))
        hsv_v = 1.0
        if 43 in o:
            p = o[43].split("a")
            try:
                hsv_v = float(p[2])
            except (ValueError, IndexError):
                pass
        for spr, ch in layers:
            rgb, opacity, blending = chans.get(ch, ((255, 255, 255), 1.0, False))
            w = max(1, int(round(spr.width / 4 * sx * s)))
            h = max(1, int(round(spr.height / 4 * sy * s)))
            if w * h > 6000 * 6000:
                continue
            im = spr
            if fx:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            if fy:
                im = im.transpose(Image.FLIP_TOP_BOTTOM)
            im = im.resize((w, h), Image.BILINEAR)
            if rot:
                im = im.rotate(-rot, resample=Image.BICUBIC, expand=True)
            a = np.asarray(im, dtype=np.float32) / 255
            cx = (fnum(o, 2, 0) - X0) * s
            cy = (Y1 - fnum(o, 3, 0)) * s
            x0 = int(round(cx - a.shape[1] / 2))
            y0 = int(round(cy - a.shape[0] / 2))
            xa, ya = max(0, x0), max(0, y0)
            xb, yb = min(W, x0 + a.shape[1]), min(H, y0 + a.shape[0])
            if xa >= xb or ya >= yb:
                continue
            sub = a[ya - y0:yb - y0, xa - x0:xb - x0]
            col = np.array(rgb, dtype=np.float32) / 255 * hsv_v
            src = sub[:, :, :3] * col
            alpha = sub[:, :, 3:4] * opacity
            dst = canvas[ya:yb, xa:xb]
            if blending:
                canvas[ya:yb, xa:xb] = np.clip(dst + src * alpha, 0, 1)
            else:
                canvas[ya:yb, xa:xb] = src * alpha + dst * (1 - alpha)
            if groups & tags:
                canvas[ya, xa:xb] = (1, 0, 0)

    # Ground: GD draws it over everything below y = 0.
    gy = int(round((Y1 - 0) * s))
    if gy < H:
        g = np.array(chans[1001][0], dtype=np.float32) / 255
        canvas[gy:, :] = g * 0.85
        line = np.array(chans[1002][0], dtype=np.float32) / 255
        canvas[max(0, gy - 1):gy + 1, :] = line

    out = Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8), "RGB")
    out.save(args.out)
    print(args.out, out.size, len(order), "objects")


if __name__ == "__main__":
    main()
