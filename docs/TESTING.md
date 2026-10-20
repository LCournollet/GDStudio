# Tests

## Automated (without the game)

```powershell
./scripts/build.ps1 -Target tests      # builds and runs tests/gdds_tests.exe
```

`tests/test_analysis.cpp` (geometry), `tests/test_basics.cpp` (JSON, object format, palettes,
layers, masks), `tests/test_generation.cpp` (full pipeline on a "level string" backend).
The test levels are in `core/src/TestLevels.cpp` (`gdds-cli testlevel A..L file.txt`).

| Scenario | Automated test | What is checked |
|---|---|---|
| A – simple platform | `analysis A`, `generation A` | 30 blocks = 1 structure, 4 segments, 4 corners; 5 fill objects, continuous 12-piece outline inside the edge |
| B – connected platforms | `analysis B` | adjacent blocks merged, diagonal contacts kept apart |
| C – corners | `analysis C`, `generation C`, quadrants, ring | 5 convex + 1 concave corners; no "notch" at inner corners; cavity detected |
| D – spikes | `analysis D/E`, `generation D` | hazard clusters, support; continuous outline under the spikes |
| E – wave passage | `analysis D/E`, `generation E` | wave section detected as "tight"; no foreground, highlights or particles in the corridor |
| F – open area | `tools/render` renders | artistic composition (see the images) |
| G – layers and palettes | `generation: deterministic`, `gameplay untouched`, `every built-in preset` | determinism, identical gameplay, 7 distinct presets with no rejection |
| H – regeneration inside a mask | `generation H` | only the chosen layer's objects inside the mask change; everything else is byte-identical |
| I – locked layer | `generation I` | the locked layer does not change (neither regeneration nor removal); lock inherited from groups |
| J – save / reopen | `generation J` | full reconciliation; layers recovered from tag groups without a project file |
| K – manual edit | `generation K` | edited object detected; kept (Keep), confirmation (Ask), replaced (Replace) |
| L – full removal | `generation L` | the level goes back to exactly the original gameplay |
| Undo / redo / preview | `undo / redo ...` | exact states restored; a cancelled preview leaves nothing behind |
| Safety | `rolls the operation back` | a backend that alters the gameplay triggers a full rollback |
| Colors | `changing intensity` | a layer's intensity only changes the opacity of its channels |

Current result: **43 tests, 4,525 checks, 0 failures.** Since the first in-game session they also cover a game that does not really delete objects (the operation must stop without duplicating anything) and GD 2.2091 renumbering key 155 between creation and reload.

Offline visual checks: `tools/render/render.py` (rendering with the game's sprites).
Performance measured on a real 115,969-object level (read-only export): analysis 0.22 s, Dark Tech
generation of 19,283 objects in 3 s, gameplay intact.

## In the game (manual) – once Geode supports the installed GD version

Preparation: back up `%LOCALAPPDATA%\GeometryDash\CCLocalLevels.dat`, create a **new** test level
(do not use existing levels), build with `./scripts/build.ps1 -Target mod -Install` (for the right
GD version), start the game.

1. **Button**: open the editor; the "Studio" button appears at the top left; Alt+D opens/closes it.
2. **Test level**: build a 30-block platform, a U shape, a few spikes (or import `G.gmd` produced by
   `python tools/levels/gdsave.py gmd G.txt "Test G" G.gmd` with GMD API).
3. **Preview**: Generate tab → Preview. Expected: proposed objects outlined in cyan, Apply / Cancel
   toolbar. Cancel → no object added. Preview then Apply → the decoration stays.
4. **Colors**: check that the decoration takes the preset's colors immediately. If it stays white
   until the level is reopened → note it (the "color refresh" item).
5. **Glow**: Layers tab → Structural Glow → Intensity 0.3. Expected: the glow fades live.
6. **Regenerate one layer**: Background Structures → "skyline" style → Regenerate. Only that layer changes.
7. **Mask**: Masks tab → Rectangle → draw around a zone → Done. Select the Background Details layer,
   change its density, "Regenerate in mask". Only the zone changes.
8. **Lock**: lock Base Block Design, Regenerate in Generate. The fill does not move.
9. **Manual edit**: move an outline object by hand, regenerate Outlines → the moved object stays.
10. **Hidden layer**: uncheck "show" on a layer → its objects disappear from the editor;
    save → reopen: they are still in the level.
11. **Save / reopen**: save, quit GD, restart, reopen the level → Stats tab:
    "linked" = number of generated objects, 0 missing.
12. **Full removal**: Generate → Remove all → the level only contains the gameplay.
13. **Gameplay**: play the level before/after (same path, same collisions).

For each step, write down what differs and attach the Geode log file
(`<GD>/geode/logs`, `[GD Decoration Studio]` lines).
