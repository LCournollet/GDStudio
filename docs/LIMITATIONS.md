# Known limitations and decisions

This document lists, honestly, what is not guaranteed or not verified yet.

## Game and Geode versions (blocking for in-game tests)

- Installed GD: **2.2091** (Steam, PE timestamp 2026-10-17). Installed Geode: **5.11.2**, which
  targets **2.2081** and runs in *forward compatibility mode*: in that mode the loader **refuses**
  every mod declaring `gd: 2.2081` (this mod, and also `geode.node-ids`).
- The stable build is compiled against SDK 5.11.2 / GD 2.2081, so it cannot load in GD 2.2091.
  **No in-game behavior has been observed.** Once Geode officially supports 2.2091: update the SDK
  and the loader, build with `-GdVersion 2.2091`, test (`docs/GEODE_UPDATE.md`).
- The published 2.2091 bindings (`geode-sdk/bindings`, `main`) **do not compile with any released
  Geode SDK**: neither 5.11.2 nor the SDK's `main` branch (the 2.2091 cocos headers
  `CCTouchClipper.h` / `CCKeyboardActionManager.h` are missing and signatures changed). The failure
  happens in Geode's own bindings library, before this mod's code.
- They do compile with Geode's **`v6` development branch**, which natively targets 2.2091. An
  experimental build (loader built from source + this mod) passes every offline check and was run
  once in the game; the problems that session revealed are fixed: see `docs/EXPERIMENTAL_2091.md`.
- Every game function used by the mod has a Windows address in the 2.2081 **and** 2.2091 bindings
  (checked in the broma files), and the 2.2091 addresses were checked against the installed
  executable, including that each one lies among its class's other methods. That last check exists
  because the upstream 2.2091 address of `LevelEditorLayer::removeObject` turned out to be another
  function; the experimental build fixes it, and the engine no longer trusts a backend's removal
  count: it re-reads the level and stops an operation before creating anything if objects were not
  really deleted.
- GD 2.2091 writes a key `155` on every object whose value changes between creation and reload; it is
  excluded from object fingerprints. The adapter deliberately avoids `getLevelString`, `EditButtonBar::create`,
  `LevelSettingsObject::create`, the UI's `undoLastAction`, `EditorPauseLayer::saveLevel` and the
  `node-ids` dependency, which are missing or broken in 2.2091.

## Not verified in the game (test these first)

- **Color refresh** after writing the channels (`ColorAction::setupFromString`,
  `transferDefaultColors`, `updateObjectColors`): the exact sequence that refreshes the editor is
  not documented; if colors only show up after reopening the level, this is the culprit.
- **Vertical offset** between level coordinates and editor nodes: measured automatically on an
  object (not hard-coded); only used to draw masks and the overlay.
- **Touch priorities** of the toolbar and the mask tool in the editor. The toolbar's position is
  chosen at runtime clear of the editor's buttons (logged as `Toolbar placed at (x, y)`).
- **Visibility of hidden layers**: reapplied after every `updateVisibility` of the editor.
- **Object type checks**: at startup every catalog object is created and set up as the game does
  for its toolbox icons (`createWithKey` + `customSetup`) and disabled if it gets a gameplay type.
  Type 0 (Solid) is also the constructor default, and some decorations only get their type once
  placed in a level (`setupCustomSprites`, e.g. object 507), so those are checked again on every
  object actually created: anything the game does not treat as a decoration is removed at once and
  disabled. In both checks, if more than a third fail together (unreliable field read), the check
  is skipped.

## Gameplay safety: what is guaranteed, what is not

Guaranteed (verified by the tests):
- no non-generated object is ever modified, moved or removed: full before/after comparison of
  every operation, automatic rollback on any difference;
- only catalog objects verified as decorations (no hitbox, not a trigger/portal/orb/pad) are
  created, with no property other than position/rotation/scale/color/Z/tag group;
- nothing drawn above the hazards covers a hazard.

Not guaranteed:
- **trajectories are not simulated**: readability relies on conservative rules (margins around
  hazards, reduction in tight corridors, draw order, no foreground near the gameplay);
- readability also depends on the chosen colors: the harmonizer warns when fill and outlines, or
  fill and background, are too close, but does not fix the colors.

## Analysis

- Half-block analysis grid (15 units): thinner objects are approximated.
- Slopes: rasterized as stairs for the analysis; the outline follows the hypotenuse, but the fill
  does not cover slope cells (the slope sprite stays visible).
- Objects moved / toggled by triggers (groups targeted by a trigger), hidden or *no-touch*: never
  decorated (a static decoration would stay in place while the platform moves, or would reveal a
  hidden path). An "inherit movement groups" option is still to do.
- Objects' default Z layers are not in the extracted data: in the game they are read from the
  objects; offline (CLI, renderer), B2 is assumed for blocks and hazards.

## UI

- One tabbed (modal) window + a permanent toolbar: mask drawing and preview happen with the window
  closed. The UI style uses the game's sprites.
- Preset import: copy the `.json` into the presets folder ("Open presets folder" button).

## Offline renderer

`tools/render/render.py` approximates the game's rendering (no shaders, pulses, animations,
automatic block glow sprites, camera effects). It is meant to judge composition and alignment, not
to reproduce the game pixel for pixel.
