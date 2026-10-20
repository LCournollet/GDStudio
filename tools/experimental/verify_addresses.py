"""Checks the 2.2091 binding addresses used by the mod against the installed GeometryDash.exe.

For every address: it must be the exact start of a function in the exe's .pdata
(exception directory). Virtual functions must also appear as a pointer in a
vtable (.rdata). Read-only: the exe is only read.

Being a function start is not enough: upstream bound LevelEditorLayer::removeObject
to 0x223320, a real function start that is in fact a level-statistics routine
called only by PlayLayer::onQuit. With `--bro <GeometryDash.bro>`, every address
is also compared with the other functions bound for the same class: a class's
methods are laid out together in the executable, so an address far from all of
them is reported as an OUTLIER (that check flags 0x223320, not 0x3f21b0).

Usage: python verify_addresses.py [--bro <bindings>/bindings/2.2091/GeometryDash.bro]
"""
import struct
import sys

EXE = r"D:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"

USED = {
    # hooks
    "EditorUI::init": (0x1a9630, False),
    "EditorUI::ccTouchBegan": (0x1bff50, True),
    "EditorUI::ccTouchMoved": (0x1c0500, True),
    "EditorUI::ccTouchEnded": (0x1c0cf0, True),
    "EditorUI::keyDown": (0x1c1a90, True),
    "LevelEditorLayer::updateVisibility": (0x3f63b0, True),
    "EditorPauseLayer::onSave": (0x182800, False),
    "EditorPauseLayer::onSaveAndPlay": (0x182590, False),
    "EditorPauseLayer::onSaveAndExit": (0x182b30, False),
    "EditorPauseLayer::onExitNoSave": (0x182c80, False),
    # calls
    "EditorUI::deselectAll": (0x1c82b0, False),
    "EditorUI::getSelectedObjects": (0x1c9630, False),
    "EditorUI::selectObjects": (0x1c8500, False),
    "LevelEditorLayer::createObjectsFromString": (0x3f0150, False),
    # 0x3f21b0, not the upstream 0x223320: null check, undo entry unless noUndo,
    # removeObjectFromSection, removeSpecial (what EditorUI::onDeleteSelected does).
    "LevelEditorLayer::removeObject": (0x3f21b0, False),
    "GameObject::customSetup": (0x28e020, True),
    "LevelEditorLayer::transferDefaultColors": (0x3fa630, False),
    "LevelEditorLayer::updateObjectColors": (0x3f7460, False),
    "GameObject::getSaveString": (0x29c200, True),
    "GameObject::createWithKey": (0x288930, False),
    "LevelSettingsObject::getSaveString": (0x40ed20, False),
    "GJEffectManager::getColorAction": (0x365ef0, False),
    "GJEffectManager::setColorAction": (0x366100, False),
    "ColorAction::create": (0x363310, False),
    "ColorAction::setupFromString": (0x363ce0, False),
    "ButtonSprite::create(caption...)": (0x42be0, False),
    "ButtonSprite::updateBGImage": (0x42fc0, False),
    "CCMenuItemSpriteExtra::create": (0x488a0, False),
    "CCMenuItemToggler::create": (0x48f70, False),
    "CCMenuItemToggler::toggle": (0x49320, False),
    "FLAlertLayer::show": (0x5b8a0, True),
}


def class_addresses(bro_path):
    """win addresses bound in a .bro file, grouped by class."""
    import re
    out = {}
    cls = None
    for line in open(bro_path, encoding="utf-8"):
        m = re.match(r"\s*class\s+([\w:]+)", line)
        if m:
            cls = m.group(1)
            continue
        w = re.search(r"\)[^=;{]*=\s*[^;]*?win\s+(0x[0-9a-f]+)", line)
        if w and cls:
            a = int(w.group(1), 16)
            if a != 0x9999999:
                out.setdefault(cls, []).append(a)
    return {c: sorted(v) for c, v in out.items()}


def is_outlier(rva, addrs):
    """True when `rva` lies outside its class's bulk and far from every other method."""
    others = [a for a in addrs if a != rva]
    if len(others) < 5:
        return False
    p10, p90 = others[len(others) // 10], others[(len(others) * 9) // 10]
    nearest = min(abs(a - rva) for a in others)
    return not (p10 - 0x8000 <= rva <= p90 + 0x8000) and nearest >= 0x2000


def main():
    bro = sys.argv[sys.argv.index("--bro") + 1] if "--bro" in sys.argv else None
    classes = class_addresses(bro) if bro else {}
    data = open(EXE, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    dd = opt + 112
    exc_rva, exc_size = struct.unpack_from("<II", data, dd + 3 * 8)
    sections = []
    for i in range(nsec):
        s = pe + 24 + optsize + i * 40
        name = data[s:s + 8].rstrip(b"\0").decode()
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((name, va, vsize, raw, rsize))

    def rva_to_off(rva):
        for name, va, vsize, raw, rsize in sections:
            if va <= rva < va + max(vsize, rsize):
                return raw + rva - va
        return None

    starts = set()
    off = rva_to_off(exc_rva)
    for k in range(exc_size // 12):
        begin, end, unwind = struct.unpack_from("<III", data, off + k * 12)
        starts.add(begin)
    rdata = [s for s in sections if s[0] == ".rdata"][0]
    rdata_bytes = data[rdata[3]:rdata[3] + rdata[4]]
    print(f"image base {image_base:#x}, {len(starts)} functions in .pdata")
    bad = 0
    for name, (rva, virtual) in USED.items():
        ok_start = rva in starts
        line = f"{name:45} {rva:#08x}  function start: {'yes' if ok_start else 'NO'}"
        if virtual:
            ptr = struct.pack("<Q", image_base + rva)
            refs = rdata_bytes.count(ptr)
            line += f"   vtable refs: {refs}"
            if refs == 0:
                bad += 1
        if not ok_start:
            bad += 1
        cls = name.split("::")[0]
        if cls in classes:
            outlier = is_outlier(rva, classes[cls])
            line += "   class range: " + ("OUTLIER" if outlier else "ok")
            if outlier:
                bad += 1
        print(line)
    print("RESULT:", "all addresses plausible" if bad == 0 else f"{bad} suspicious")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
