# GD Decoration Studio

A Geode mod (C++23) that automatically generates the decoration of a Geometry Dash level whose
gameplay already exists: geometry analysis, independent layers, masks, palettes, preview,
non-destructive editing. Everything it generates is made of **native game objects**, editable in
the regular editor, and the level displays normally without the mod.

## Current status (read this first)

| Component | Status |
|---|---|
| Engine (analysis, structural / lighting / artistic engines, layers, masks, palettes, presets, persistence, history, validation) | Implemented, **40 automated tests** (4,500 checks) passing |
| Offline renderer using the game's real sprites | Implemented (`tools/render`), used to judge visual quality |
| Geode mod (editor adapter + UI) | **Builds and packages** (`.geode`) for Geode 5.11.2 / GD 2.2081 |
| Experimental GD 2.2091 build | **Builds** against Geode's `v6` development branch with one bindings fix, verified offline (addresses, imports, field offsets). See `docs/EXPERIMENTAL_2091.md` |
| Running in the game | **First session done with the experimental 2.2091 build**: the studio opens and generates; the bugs it revealed (deletion, layer list layout, button position, object recognition after reload) are fixed but the fixed build is not tested in the game yet. No released Geode supports GD 2.2091 (Steam build of 2026-10-17). See `docs/EXPERIMENTAL_2091.md` and `docs/LIMITATIONS.md` |

## Layout

```
core/            pure C++ library (no Geode dependency): the whole engine
  include/gdds/  public API (Analysis, Engine, Generator, Studio, Layer, Mask, Palette, Presets, Project...)
  src/           implementation; src/generated/ = tables generated from the game's data
mod/             Geode mod: editor adapter (EditorBackend), session, mask tool, UI
tests/           unit and integration tests (scenarios A to L)
tools/cli/       gdds-cli: analyze / decorate level strings without the game
tools/render/    PNG rendering of a level with the game's sprites (reads Resources/ only)
tools/catalog/   generation of the object tables from data extracted from the installed game
tools/levels/    read-only export of local levels and .gmd creation
tools/experimental/  experimental GD 2.2091 build (Geode v6 loader + mod), install / rollback scripts
docs/            architecture, persistence, limitations, tests, catalog
scripts/build.ps1  build (tests or mod)
```

## Building

Prerequisites: Visual Studio Build Tools 2022 (MSVC 14.44), CMake + Ninja (pip `--user` works),
Geode CLI 3.9.0 (`%LOCALAPPDATA%\Programs\geode-cli`), Geode SDK 5.11.2 (`%USERPROFILE%\geode-sdk`,
`GEODE_SDK` environment variable).

```powershell
./scripts/build.ps1 -Target tests          # core + tests + CLI, runs the tests
./scripts/build.ps1 -Target mod            # Geode mod -> %LOCALAPPDATA%\gdds-build\mod\mod\toyger.gd-decoration-studio.geode
./scripts/build.ps1 -Target mod -Install   # same + copies it into GD's mods folder (only on a supported GD version)
```

Build folders live in `%LOCALAPPDATA%\gdds-build` (short paths: MSVC limits object file paths to
250 characters).

## Trying it without the game

```powershell
$cli = "$env:LOCALAPPDATA\gdds-build\tests\tools\cli\gdds-cli.exe"
& $cli testlevel G G.txt                                   # test level (A..L)
& $cli decorate G.txt G_deco.txt --preset "Neon" --level-colors --save-project G.json
python tools/render/render.py G_deco.txt G_deco.png --x0 0 --x1 2500
# One of your own levels (read-only access to the save file):
python tools/levels/gdsave.py list
python tools/levels/gdsave.py export "Level name" my_level.txt
& $cli decorate my_level.txt my_level_deco.txt --preset "Dark Tech"
# Regenerate a single layer inside a zone:
& $cli decorate G_deco.txt G_deco2.txt --project G.json --mode regenerate --layers background-details --mask 5000,-1000,8000,3000
```

## Using it in the editor (once the GD version is supported)

1. Open a level in the editor: a **Studio** button appears at the top left (or press **Alt+D**).
2. **Presets**: pick a style (Modern Glow, Minimal, Dark Tech, Neon, Industrial, Cyberpunk, Abstract).
3. **Objects / Colors**: choose objects and colors, globally or per layer.
4. **Layers**: tune each layer (density, complexity, intensity, scale, opacity, seed, style, Z, mask edge policy); lock / hide / rename / duplicate / create sub-layers.
5. **Masks**: draw rectangles, polygons, brush strokes, X sections, or build a mask from the selected structures; include or exclude.
6. **Generate**: *Preview* (proposed objects outlined in cyan, Apply / Cancel in the toolbar), then *Generate* / *Regenerate* / *Remove*.
7. Adjust a layer (e.g. glow intensity: applied live, no regeneration), regenerate a single layer or a single zone.
8. Edit by hand: modified objects are detected and kept (*Keep* / *Replace* / *Ask* policy).
9. Save the level: the studio project is saved with it and found again when the level is reopened.

Documentation: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PERSISTENCE.md](docs/PERSISTENCE.md),
[docs/LIMITATIONS.md](docs/LIMITATIONS.md), [docs/TESTING.md](docs/TESTING.md), [docs/CATALOG.md](docs/CATALOG.md),
[docs/GEODE_UPDATE.md](docs/GEODE_UPDATE.md), [docs/EXPERIMENTAL_2091.md](docs/EXPERIMENTAL_2091.md).
