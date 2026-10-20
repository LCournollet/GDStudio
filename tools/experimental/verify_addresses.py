"""Checks the 2.2091 binding addresses used by the mod against the installed GeometryDash.exe.

For every address: it must be the exact start of a function in the exe's .pdata
(exception directory). Virtual functions must also appear as a pointer in a
vtable (.rdata). Read-only: the exe is only read.
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
    "LevelEditorLayer::removeObject": (0x223320, False),
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


def main():
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
        print(line)
    print("RESULT:", "all addresses plausible" if bad == 0 else f"{bad} suspicious")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
