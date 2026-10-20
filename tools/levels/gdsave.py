"""Read-only access to Geometry Dash local levels, and .gmd export.

    python tools/levels/gdsave.py list   [--save PATH]
    python tools/levels/gdsave.py export "<level name>" OUT.txt [--save PATH]
    python tools/levels/gdsave.py gmd LEVEL.txt "<name>" OUT.gmd

The save file is NEVER written: it is copied to memory and decoded
(XOR 11 -> base64 -> gzip -> plist). .gmd files can be imported into the
editor with the "GMD API" mod (hjfod.gmd-api) or similar tools.
"""
import argparse
import base64
import gzip
import os
import re
import sys
import xml.etree.ElementTree as ET
import zlib

DEFAULT_SAVE = os.path.join(os.environ.get("LOCALAPPDATA", ""), "GeometryDash", "CCLocalLevels.dat")


def decode_save(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"<?xml"):
        return data.decode("utf-8", "replace")
    x = bytes(b ^ 11 for b in data)
    x = x.replace(b"-", b"+").replace(b"_", b"/").rstrip(b"\x00")
    x += b"=" * (-len(x) % 4)
    raw = base64.b64decode(x)
    return zlib.decompress(raw, 15 | 32).decode("utf-8", "replace")


def decode_level_string(s):
    if not s:
        return ""
    if s.startswith("kS") or s.startswith("1,"):
        return s
    b = s.replace("-", "+").replace("_", "/")
    b += "=" * (-len(b) % 4)
    return zlib.decompress(base64.b64decode(b), 15 | 32).decode("utf-8", "replace")


def encode_level_string(s):
    comp = gzip.compress(s.encode("utf-8"))
    return base64.urlsafe_b64encode(comp).decode("ascii")


def iter_levels(xml_text):
    """Yields dicts of key -> value for each local level (plist <d> with k_ keys)."""
    # GD's plist uses short tags: <d> dict, <k> key, <s> string, <i> int, <t/> true...
    root = ET.fromstring(xml_text)
    top = root.find("dict")
    if top is None:
        return
    items = list(top)
    for i in range(0, len(items) - 1, 2):
        if items[i].text == "LLM_01":
            levels = items[i + 1]
            children = list(levels)
            for j in range(0, len(children) - 1, 2):
                d = children[j + 1]
                if d.tag != "d":
                    continue
                entry = {}
                kids = list(d)
                for k in range(0, len(kids) - 1, 2):
                    entry[kids[k].text] = kids[k + 1].text
                if "k2" in entry:
                    yield entry


def cmd_list(save):
    for e in iter_levels(decode_save(save)):
        print(f"{e.get('k2')!r:40} objects={e.get('k48', '?'):>7}  rev={e.get('k46', '0')}")


def cmd_export(save, name, out):
    for e in iter_levels(decode_save(save)):
        if e.get("k2") == name:
            s = decode_level_string(e.get("k4", ""))
            with open(out, "w", encoding="utf-8") as f:
                f.write(s)
            print(out, len(s.split(";")) - 2, "objects")
            return 0
    print("level not found:", name, file=sys.stderr)
    return 1


def cmd_gmd(level_txt, name, out):
    s = open(level_txt, encoding="utf-8").read().strip()
    esc = lambda t: t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    gmd = ('<?xml version="1.0"?><plist version="1.0" gjver="2.0"><dict>'
           f"<k>kCEK</k><i>4</i><k>k2</k><s>{esc(name)}</s><k>k4</k><s>{encode_level_string(s)}</s>"
           "<k>k13</k><t /><k>k21</k><i>2</i><k>k50</k><i>45</i></dict></plist>")
    with open(out, "w", encoding="utf-8") as f:
        f.write(gmd)
    print(out)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "export", "gmd"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--save", default=DEFAULT_SAVE)
    a = ap.parse_args()
    if a.cmd == "list":
        return cmd_list(a.save)
    if a.cmd == "export" and len(a.args) == 2:
        return cmd_export(a.save, a.args[0], a.args[1])
    if a.cmd == "gmd" and len(a.args) == 3:
        return cmd_gmd(*a.args)
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main() or 0)
