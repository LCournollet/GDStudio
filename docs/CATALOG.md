# Object catalog: where the data comes from

The engine's tables do not come from community lists: they were extracted from the installed game
(GD 2.2091, `GeometryDash.exe` of 2026-10-17), **read-only**, through static analysis.

| Data | Method |
|---|---|
| ID → sprite (4,093 objects) | decoding the 4,094 insertions of `ObjectToolbox::init` |
| C++ class of each object | emulation of `GameObject::createWithKey` + RTTI information |
| Type (solid, hazard, decoration, trigger, portal...) and hitbox | emulation of each class's `customSetup` (constants from the binary: spike 0.2×0.4 → 6×12, saw radii 32.3 / 21.6 / 11.925, orbs ×1.2) |
| Visible size and sprite offset | `-uhd.plist` sheets (4 px = 1 unit, checked: `square_01` = 120 px = 30 units) |
| Canonical sprite orientation (corners, glows, lines) | pixel measurements on the game's sprite sheets (`tools/catalog/gen_catalog.py`) |

Generated files: `core/src/generated/ObjectTableData.inc` (gameplay table),
`core/src/generated/DecoCatalogData.inc` (61 curated objects with shape and role + 3,360 static
native decorations for the library), `tools/data/*.json` (offline renderer).

Safety rules: only *Decoration*-type objects without a hitbox, of class `GameObject` (static) or
`EnhancedGameObject` (flagged "animated", never used by default) are in the catalog. Portal /
trigger / text objects that the game classifies as "decoration" (teleporter exits, texts) are
excluded. In the game, every chosen object is additionally created once at startup and disabled if
it does not behave like a decoration.

Extracted data: `tools/catalog/data/` (`object_types.json`, `frames_uhd.json`,
`deco_candidates.json`). Extraction scripts (disassembly / emulation, read-only access to the exe):
`tools/catalog/extract/`.

To regenerate the C++ tables (for example after re-extracting the data for a new GD version):

```powershell
python tools/catalog/gen_catalog.py --objdata tools/catalog/data --resources "<GD>\Resources" --root .
```
