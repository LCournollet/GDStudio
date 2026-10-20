"""Generates the engine's object tables from data extracted from the installed game.

Inputs (see docs/CATALOG.md for how they were produced):
  object_types.json  - per object id: frame, class, category, hitbox (static analysis of GeometryDash.exe)
  frames_uhd.json    - every sprite frame of the -uhd sheets (sheet, rect, offset, sizes)
  <GD>/Resources     - the sprite sheets themselves (read only), used to measure sprite orientation

Outputs:
  core/src/generated/ObjectTableData.inc  - gameplay kind + hitbox for every object id
  core/src/generated/DecoCatalogData.inc  - decoration catalog (curated shapes + full library)
  tools/data/objects.json, tools/data/frames.json - data for the offline renderer

Usage:
  python -I tools/catalog/gen_catalog.py --objdata <dir> --resources "<GD>/Resources" --root <project root>
"""
import argparse
import json
import os
import plistlib
import sys

from PIL import Image

# ---------------------------------------------------------------------------
# Curated catalog: (id, name, ui category, shape, tags, stretchable, blending)
# Shapes are canonical (see core/include/gdds/DecoCatalog.hpp): the script
# measures each sprite's native orientation and stores the base rotation.
CURATED = [
    # Fills
    (211, "Square", "Blocks", "square", "default,fill", True, False),
    (916, "Square (half)", "Blocks", "square", "small", True, False),
    (495, "Square (large)", "Blocks", "square", "large", True, False),
    (917, "Tiny square", "Details", "dot", "square-dot", False, False),
    # Lines
    (1753, "Thin line", "Outlines", "line", "default,thin", True, False),
    (1757, "Thin line (short)", "Outlines", "line", "thin,short", True, False),
    (507, "Line (2u)", "Outlines", "line", "medium", True, False),
    (1191, "Thick bar", "Outlines", "line", "thick", True, False),
    # Corner brackets
    (2857, "Corner bracket", "Corners", "corner", "default,thin", False, False),
    (2855, "Corner bracket B", "Corners", "corner", "thin", False, False),
    (2852, "Corner bracket (round)", "Corners", "corner", "thin,round", False, False),
    (2859, "Corner bracket D", "Corners", "corner", "thin", False, False),
    (1192, "Thick corner", "Corners", "corner", "thick", False, False),
    # Glow
    (1011, "Edge glow", "Glow", "glow-edge", "default,soft", True, True),
    (503, "Edge glow (short)", "Glow", "glow-edge", "short", True, True),
    (1291, "Edge glow (narrow)", "Glow", "glow-edge", "narrow", True, True),
    (3958, "Soft glow", "Glow", "glow-round", "default,soft", False, True),
    (3959, "Soft glow B", "Glow", "glow-round", "soft", False, True),
    (3825, "Small glow", "Glow", "glow-round", "small", False, True),
    (3829, "Small glow B", "Glow", "glow-round", "small", False, True),
    (3843, "Small glow C", "Glow", "glow-round", "small", False, True),
    # Details
    (1764, "Small ball", "Details", "dot", "default", False, False),
    (725, "Dot", "Details", "dot", "", False, False),
    (1766, "Small triangle", "Details", "triangle", "small", False, False),
    (1768, "Chevron", "Details", "ornament", "chevron", False, False),
    (1765, "Small cross", "Details", "ornament", "cross", False, False),
    (1767, "Small dash", "Details", "ornament", "dash", False, False),
    # Geometric
    (497, "Circle", "Geometric", "circle", "default", False, False),
    (236, "Ring", "Geometric", "ring", "default", False, False),
    (3802, "Circle (small)", "Geometric", "circle", "small", False, False),
    (3807, "Ring (small)", "Geometric", "ring", "small", False, False),
    (3803, "Triangle", "Geometric", "triangle", "default", False, False),
    (3808, "Triangle outline", "Geometric", "outline-shape", "triangle", False, False),
    (3805, "Square outline (small)", "Geometric", "outline-shape", "square,small", False, False),
    (2973, "Square outline (shaded)", "Geometric", "outline-shape", "square,shaded", False, False),
    (2975, "Square outline", "Geometric", "outline-shape", "square,default", False, False),
    (3938, "Square ring", "Geometric", "outline-shape", "square,thick", False, False),
    (3801, "Hexagon", "Geometric", "polygon", "default,hexagon", False, False),
    (3806, "Hexagon outline", "Geometric", "outline-shape", "hexagon", False, False),
    (3804, "Star", "Geometric", "polygon", "star", False, False),
    # Particles / atmosphere
    (3966, "Sparkle", "Particles", "ornament", "sparkle,default", False, True),
    (3967, "Sparkle B", "Particles", "ornament", "sparkle", False, True),
    (3883, "Four-point star", "Particles", "ornament", "sparkle", False, True),
    (3968, "Burst", "Particles", "glow-round", "burst", False, True),
    (3980, "Smoke puff", "Particles", "glow-round", "dust", False, False),
    (3981, "Smoke puff B", "Particles", "glow-round", "dust", False, False),
    (3982, "Smoke puff C", "Particles", "glow-round", "dust", False, False),
    # Background art
    (450, "Tower", "Background", "large", "tower", False, False),
    (451, "Tower B", "Background", "large", "tower", False, False),
    (452, "Tower C", "Background", "large", "tower", False, False),
    (113, "Skyline", "Background", "large", "skyline", False, False),
    (114, "Skyline B", "Background", "large", "skyline", False, False),
    (115, "Skyline C", "Background", "large", "skyline", False, False),
    (48, "Cloud", "Background", "large", "cloud,nature", False, False),
    (129, "Cloud B", "Background", "large", "cloud,nature", False, False),
    (936, "Art cloud", "Background", "large", "cloud,nature", False, False),
    # Hanging
    (41, "Chain", "Background", "chain", "default", False, False),
    (106, "Chain B", "Background", "chain", "", False, False),
    (110, "Chain C", "Background", "chain", "", False, False),
    (15, "Rod", "Background", "chain", "rod", False, False),
    (16, "Rod B", "Background", "chain", "rod", False, False),
]

SHAPE_ENUM = {
    "square": "Square", "line": "Line", "corner": "Corner", "dot": "Dot", "circle": "Circle", "ring": "Ring",
    "triangle": "Triangle", "diamond": "Diamond", "polygon": "Polygon", "outline-shape": "OutlineShape",
    "glow-edge": "GlowEdge", "glow-corner": "GlowCorner", "glow-round": "GlowRound", "chain": "Chain",
    "ornament": "Ornament", "large": "Large",
}

GAMEMODES = {"cube": 0, "ship": 1, "ball": 2, "ufo": 3, "wave": 4, "robot": 5, "spider": 6, "swing": 7}
SPEEDS = {"200": 1, "201": 0, "202": 2, "203": 3, "1334": 4}

# Offline assumption for default draw order (the mod reads real values from
# the game). Both at B2 = the most protective assumption for hazards.
DEFAULT_Z = {"solid": 1, "hazard": 1}


def load_sheet_cache(resources):
    cache = {}

    def get(sheet):
        if sheet not in cache:
            d = plistlib.load(open(os.path.join(resources, sheet), "rb"))
            tex = d.get("metadata", {}).get("textureFileName") or sheet.replace(".plist", ".png")
            cache[sheet] = Image.open(os.path.join(resources, tex)).convert("RGBA")
        return cache[sheet]

    return get


def frame_image(frames, get_sheet, name):
    """Trimmed sprite pixels in native orientation (no source-size padding)."""
    f = frames.get(name)
    if not f or not f.get("rect"):
        return None
    img = get_sheet(f["sheet"])
    x, y, w, h = [int(v) for v in f["rect"]]
    if f["rotated"]:
        return img.crop((x, y, x + h, y + w)).rotate(90, expand=True)
    return img.crop((x, y, x + w, y + h))


def alpha_stats(im):
    a = im.getchannel("A")
    w, h = a.size
    px = a.load()
    total = sx = sy = 0.0
    for yy in range(h):
        for xx in range(w):
            v = px[xx, yy] / 255.0
            total += v
            sx += v * (xx + 0.5)
            sy += v * (yy + 0.5)
    if total <= 0:
        return w, h, w / 2, h / 2, 0
    return w, h, sx / total, sy / total, total


def strip_mass(im, side, frac=0.25):
    a = im.getchannel("A")
    w, h = a.size
    px = a.load()
    if side == "top":
        box = (0, 0, w, max(1, int(h * frac)))
    elif side == "bottom":
        box = (0, h - max(1, int(h * frac)), w, h)
    elif side == "left":
        box = (0, 0, max(1, int(w * frac)), h)
    else:
        box = (w - max(1, int(w * frac)), 0, w, h)
    s = 0
    for yy in range(box[1], box[3]):
        for xx in range(box[0], box[2]):
            s += px[xx, yy]
    return s / 255.0


def corner_base_rotation(im):
    """L bracket: which two border strips carry the bars -> quadrant it opens to."""
    m = {s: strip_mass(im, s) for s in ("top", "bottom", "left", "right")}
    vert = "bottom" if m["bottom"] >= m["top"] else "top"
    horiz = "left" if m["left"] >= m["right"] else "right"
    # canonical: bars along bottom + left (opens NE, quadrant 0)
    quadrant = {("bottom", "left"): 0, ("bottom", "right"): 1, ("top", "right"): 2, ("top", "left"): 3}[(vert, horiz)]
    return 90 * quadrant


def glow_edge_base_rotation(im):
    """Gradient strip: canonical bright side DOWN."""
    w, h, cx, cy, _ = alpha_stats(im)
    dx, dy = cx / w - 0.5, cy / h - 0.5  # image coordinates: +y = down
    if abs(dy) >= abs(dx):
        return 0 if dy > 0 else 180
    return 90 if dx > 0 else 270  # bright right -> rotate CW 90 ; bright left -> CW 270


def bar_thickness(im, canonical_rotation):
    """Thickness of the bottom bar of a canonical corner, in units."""
    # rotate native image into canonical orientation (PIL rotates CCW)
    can = im.rotate(-canonical_rotation, expand=True)
    a = can.getchannel("A")
    w, h = a.size
    px = a.load()
    rows = 0
    for yy in range(h - 1, -1, -1):
        row = sum(px[xx, yy] for xx in range(w)) / (255.0 * w)
        if row > 0.35:
            rows += 1
        else:
            break
    return rows / 4.0


def visible_bounds(frames, name):
    f = frames.get(name)
    if not f:
        return (-15.0, -15.0, 15.0, 15.0)
    sw, sh = f["spriteSize"] or f["sourceSize"]
    ox, oy = f["offset"]
    return ((ox - sw / 2) / 4.0, (oy - sh / 2) / 4.0, (ox + sw / 2) / 4.0, (oy + sh / 2) / 4.0)


def ui_category(frame):
    f = frame.lower()
    if f.startswith("pixel"):
        return "Pixel Art"
    if f.startswith("particle_"):
        return "Particles"
    if "gradient" in f or "glow" in f or "light" in f:
        return "Glow"
    if f.startswith(("blockoutline", "persp_outline", "gridline", "edge_")):
        return "Outlines"
    if f.startswith(("block", "square", "lightsquare", "d_square", "d_whiteblock", "d_largesquare")):
        return "Blocks"
    if "cloud" in f or "pillar" in f or "brick" in f or "bg" in f:
        return "Background"
    if "chain" in f or "rod_" in f:
        return "Background"
    return "Decorations"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def fnum(v):
    s = ("%.4f" % v).rstrip("0").rstrip(".")
    if s in ("", "-0"):
        s = "0"
    if "." not in s:
        s += ".0"
    return s + "f"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdata", required=True)
    ap.add_argument("--resources", required=True)
    ap.add_argument("--root", required=True)
    args = ap.parse_args()

    types = json.load(open(os.path.join(args.objdata, "object_types.json")))["objects"]
    frames = json.load(open(os.path.join(args.objdata, "frames_uhd.json")))
    get_sheet = load_sheet_cache(args.resources)

    gen_dir = os.path.join(args.root, "core", "src", "generated")
    data_dir = os.path.join(args.root, "tools", "data")
    os.makedirs(gen_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    # ---- object table ----------------------------------------------------
    rows = []
    for sid, v in sorted(types.items(), key=lambda kv: int(kv[0])):
        oid = int(sid)
        cat = v["category"]
        sub = v.get("subtype") or ""
        kind, portal, pval = "Special", "None", 0
        if cat == "solid":
            kind = "Solid"
        elif cat == "hazard":
            kind = "Hazard"
        elif cat == "decoration":
            kind = "Decoration"
        elif cat == "trigger":
            kind = "Trigger"
        elif cat == "portal/gamemode":
            kind, portal, pval = "Portal", "GameMode", GAMEMODES.get(sub, 0)
        elif cat == "gravity":
            kind, portal = "Portal", "Gravity"
            pval = 2 if "toggle" in sub else (1 if "up" in sub else 0)
        elif cat == "speed":
            kind, portal, pval = "Portal", "Speed", SPEEDS.get(sid, 0)
        elif cat == "size":
            kind, portal, pval = "Portal", "Size", 1 if "mini" in sub else 0
        elif cat == "orb/pad":
            kind = "Pad" if "pad" in sub else "Orb"
        elif cat == "collectible":
            kind = "Collectible"
        elif cat == "other":
            pk = v.get("portal_kind")
            if sub == "start_pos":
                kind = "StartPos"
            elif pk == "mirror":
                kind, portal, pval = "Portal", "Mirror", 1 if sid == "45" else 0
            elif pk == "dual":
                kind, portal, pval = "Portal", "Dual", 1 if sid == "286" else 0
            elif pk == "teleport":
                kind, portal = ("Portal", "Teleport") if v["gd_type_name"] == "TeleportPortal" else ("Special", "None")
            else:
                kind = "Special"
        hb = v.get("hitbox")
        shape, hw, hh = "None", 0.0, 0.0
        if hb:
            if hb["shape"] == "circle":
                shape, hw, hh = "Circle", hb["radius"] * 2, hb["radius"] * 2
            elif hb["shape"] == "slope":
                shape, hw, hh = "Slope", hb["w"], hb["h"]
            else:
                shape, hw, hh = "Box", hb["w"], hb["h"]
        if kind in ("Solid", "Hazard") and shape == "None":
            shape, hw, hh = "Box", 30.0, 30.0
        cw, ch = v.get("content_size") or [30.0, 30.0]
        zl = DEFAULT_Z.get(cat, 0)
        safe = cat == "decoration" and v["class"] in ("GameObject", "EnhancedGameObject") and not hb
        rows.append("{%d, K::%s, P::%s, %d, H::%s, %s, %s, 0, 0, %s, %s, %d, 0, %s, %s},"
                    % (oid, kind, portal, pval, shape, fnum(hw), fnum(hh), fnum(cw), fnum(ch), zl,
                       "true" if safe else "false", cstr(v["frame"])))
    with open(os.path.join(gen_dir, "ObjectTableData.inc"), "w", newline="\n") as fh:
        fh.write("// GENERATED by tools/catalog/gen_catalog.py from the installed game's data. Do not edit.\n")
        fh.write("// {id, kind, portal, value, shape, hitW, hitH, offX, offY, visW, visH, zLayer, zOrder, safeDeco, frame}\n")
        fh.write("\n".join(rows) + "\n")

    # ---- decoration catalog ---------------------------------------------
    deco_rows = []
    curated_ids = set()
    report = []
    for (oid, name, uicat, shape, tags, stretch, blend) in CURATED:
        v = types.get(str(oid))
        if not v or v["category"] != "decoration":
            print("skip (not decoration):", oid, file=sys.stderr)
            continue
        frame = v["frame"]
        x0, y0, x1, y1 = visible_bounds(frames, frame)
        base = 0
        thickness = 0.0
        im = frame_image(frames, get_sheet, frame)
        if shape == "corner" and im is not None:
            base = corner_base_rotation(im)
            thickness = bar_thickness(im, base)
        elif shape == "glow-edge" and im is not None:
            base = glow_edge_base_rotation(im)
        elif shape == "line":
            if (x1 - x0) < (y1 - y0):
                base = 90
            thickness = min(x1 - x0, y1 - y0)
        tag_list = [t for t in tags.split(",") if t]
        if v["class"] != "GameObject":
            tag_list.append("animated")
            tag_list = [t for t in tag_list if t != "default"]
        has_detail = (frame.replace("_001.png", "_color_001.png") in frames)
        curated_ids.add(oid)
        deco_rows.append("{%d, %s, %s, %s, S::%s, %s, %s, %s, %s, %s, %s, true, %s, %s, %s, %s},"
                         % (oid, cstr(name), cstr(frame), cstr(uicat), SHAPE_ENUM[shape], fnum(x0), fnum(y0),
                            fnum(x1), fnum(y1), fnum(base), fnum(thickness), "true" if has_detail else "false",
                            "true" if stretch else "false", "true" if blend else "false", cstr(",".join(tag_list))))
        report.append((oid, shape, base, round(thickness, 2), (round(x0, 2), round(y0, 2), round(x1, 2), round(y1, 2))))

    # Full native library (browsable / selectable, never used by default).
    library = 0
    for sid, v in sorted(types.items(), key=lambda kv: int(kv[0])):
        oid = int(sid)
        if oid in curated_ids or v["category"] != "decoration" or v.get("hitbox"):
            continue
        if v["class"] not in ("GameObject", "EnhancedGameObject"):
            continue
        frame = v["frame"]
        if frame not in frames:
            continue
        x0, y0, x1, y1 = visible_bounds(frames, frame)
        tags = ["library"] + (["animated"] if v["class"] != "GameObject" else [])
        name = frame.replace("_001.png", "").replace(".png", "")
        deco_rows.append("{%d, %s, %s, %s, S::Ornament, %s, %s, %s, %s, 0, 0, true, false, false, false, %s},"
                         % (oid, cstr(name), cstr(frame), cstr(ui_category(frame)), fnum(x0), fnum(y0), fnum(x1),
                            fnum(y1), cstr(",".join(tags))))
        library += 1

    with open(os.path.join(gen_dir, "DecoCatalogData.inc"), "w", newline="\n") as fh:
        fh.write("// GENERATED by tools/catalog/gen_catalog.py from the installed game's data. Do not edit.\n")
        fh.write("// {id, name, frame, category, shape, visX0, visY0, visX1, visY1, baseRotation, thickness,\n")
        fh.write("//  colorable, hasDetail, stretchable, blending, tags}\n")
        fh.write("\n".join(deco_rows) + "\n")

    # ---- renderer data -----------------------------------------------------
    objects = {}
    for sid, v in types.items():
        objects[sid] = {"frame": v["frame"], "cat": v["category"], "cls": v["class"],
                        "col": v.get("default_base_color"), "size": v.get("content_size")}
    json.dump(objects, open(os.path.join(data_dir, "objects.json"), "w"), separators=(",", ":"))
    wanted = set(v["frame"] for v in types.values())
    extra = set()
    for fr in wanted:
        for suffix in ("_color_001.png", "_glow_001.png", "_2_001.png", "_back_001.png"):
            alt = fr.replace("_001.png", suffix)
            if alt in frames:
                extra.add(alt)
    sub = {k: frames[k] for k in wanted | extra if k in frames}
    json.dump(sub, open(os.path.join(data_dir, "frames.json"), "w"), separators=(",", ":"))

    print("object table:", len(rows), "ids")
    print("deco catalog:", len(curated_ids), "curated +", library, "library")
    for r in report:
        print("  ", r)


if __name__ == "__main__":
    main()
