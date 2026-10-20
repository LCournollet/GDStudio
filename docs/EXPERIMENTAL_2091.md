# Experimental build for GD 2.2091 (before Geode's official release)

Status as of 2026-10-20: **installed and run once in the game; the bugs that first session revealed
are fixed (see "First in-game session"), the fixed build has not been run in the game yet.**

## Why not with Geode 5.11.2

- In *forward compatibility* mode, the 5.11.2 loader accepts a mod that declares 2.2091 as its
  target (`ModMetadata::checkGameVersion`), **but** in 2.2091 the `CCNode` base class has two new
  virtual functions (`offsetPosition`, `applyScale`): every derived class's vtable is shifted. A mod
  (or Geode itself) built with the 5.11.2 headers that creates its own UI nodes would crash. That
  is why Geode 5.11.2 disables its UI in this mode.
- On the test machine, all 25 installed mods are refused by Geode 5.11.2 (24 "doesn't support this
  version", 1 DLL load failure): replacing the loader does not cost any working mod.

## Method

1. **Loader**: Geode's `v6` branch (active development, native 2.2091 target, recognizes the
   executable through "add 2.2091 timestamp"), built with clang-cl (LLVM 21, like Geode's CI).
   Geode's prebuilt libcurl needs `volatileaccessu.lib` (Windows SDK 10.0.26100); with an older SDK,
   `tools/experimental/sdkshim/volatileaccess.c` provides the only function it uses
   (`RtlSetVolatileMemory`, a memory fill made of volatile writes).
2. **Mod**: built against that `v6` SDK and **the same revision** of the 2.2091 bindings as the
   loader, with one fix (below). Geode v6 replaces `std::filesystem::path` with `asp::fs::Path`: the
   mod's file access goes through `mod/src/Files.hpp`, which compiles with both versions (the stable
   2.2081 build is unchanged).
3. Command: `./tools/experimental/build_2091.ps1 -Part all` (prerequisites at the top of the script).

### Bindings fix: `LevelEditorLayer::removeObject`

The upstream 2.2091 bindings bind it to `0x223320`. That address is a real function start, but it
is a level-statistics routine called only by `PlayLayer::onQuit` (it fills a dictionary with
`id`, `uid`, `cs`, `sq`...). The real `removeObject(GameObject*, bool noUndo)` is `0x3f21b0`: null
check, undo entry unless `noUndo` (`GameObjectCopy`, "Delete" command), `removeObjectFromSection`,
`removeSpecial` — the same sequence as `EditorUI::onDeleteSelected`; it lies between the other
`LevelEditorLayer` methods and is called by `breakApartTextObject`.
`build_2091.ps1` patches a copy of the bindings, and `mod/CMakeLists.txt` refuses to build a 2.2091
mod whose bindings still have the wrong address.

## Checks performed (without starting the game)

| Check | Result |
|---|---|
| v6 loader built (`Geode.dll`, `Geode.lib`, `XInput1_4.dll`, `GeodeUpdater.exe`) | OK |
| Mod built for `gd.win = 2.2091` | OK |
| 3 `GameObject` offsets measured in the exe = the bindings' offsets (`static_assert`, `mod/src/LayoutChecks.cpp`) | OK |
| 31 game functions used: exact function starts (`.pdata`), virtuals present in vtables, address inside the range of the class's other methods (`tools/experimental/verify_addresses.py --bro ...`) | 31/31 (the upstream `removeObject` address is reported as an outlier) |
| In the compiled mod: `removeObject` → `0x3f21b0` (no trace of `0x223320`), `customSetup` called through vtable slot `0x458` (the slot the game calls after `createWithKey`) | OK |
| Mod imports from `Geode.dll` present in the built loader | 72/72 |
| Mod imports from the installed game's `libcocos2d.dll` / `libExtensions.dll` / glew32 / fmod | 1092 + 171 + 2 + 9, 0 missing |
| Loader delay-load imports from the game's `libcocos2d.dll` / `libExtensions.dll` | 1174 + 172, 0 missing |
| Install then rollback on a **copy** of the game folder | identical to the start (SHA-256, 0 differences) |
| Stable 2.2081 build + tests | 43/43 tests, mod packaged |

## First in-game session (2026-10-20)

Worked: the v6 loader starts GD 2.2091, the mod loads, the studio window opens from the editor,
generation creates objects, presets apply, the project is saved with the level.

Found, from the screenshots and the Geode log, and fixed:

| Problem | Cause | Fix |
|---|---|---|
| Regenerating left the old decoration in place (duplicates), cancelled previews stayed in the level | wrong upstream address of `removeObject` (above); it returned early, nothing was deleted. Only GD's normal save files changed during the session | correct address; the engine now re-reads the level after every removal and stops before creating anything if objects were not really deleted (`Studio::commit`, undo, redo, move) |
| After reopening, every generated object counted as "hand-modified" | GD 2.2091 writes a key `155` whose value changes between creation and save/reload | key 155 is excluded from fingerprints; project files recompute fingerprints when loaded |
| "Catalog verification looks unreliable (61 of 61 failed)" | the object type is set by `customSetup`, which a bare `createWithKey` does not run | call `customSetup` after `createWithKey`, as the game does for its toolbox icons |
| Layer list: labels and toggles drawn on the row separators | row menus had their origin at the row's bottom edge instead of its middle | origin moved to the middle line |
| "Studio" button drawn over the playtest button | fixed position | placed at runtime in the first spot no editor button covers |

A level generated with the first build may contain duplicated decoration objects; they carry the
studio's tag groups, so the fixed build recovers them into their layers and *Remove* / *Regenerate*
deletes them.

**Not verified**: actual behavior in the game (loader startup, editor, generation). Only a real
launch will prove it.

## Installing (only on purpose)

`tools/experimental/install_2091.ps1` (GD closed):
- backs up `Geode.dll`, `Geode.pdb`, `XInput1_4.dll`, `GeodeUpdater.exe`, the loader's resources
  (`geode\resources\geode.loader`) and the save files (`CCLocalLevels*.dat`, `CCGameManager*.dat`)
  into `<GD>\gdds-backup-<date>\`;
- installs the built loader **with its resources** (the loader checks them by SHA-256 and would
  otherwise download the 5.11.2 set) and the mod; other mods are left untouched.

Rollback: `tools/experimental/rollback_2091.ps1 -Backup "<backup folder>"` (studio projects are kept).

## When Geode officially ships 2.2091 support

At startup the experimental loader asks Geode's server for "the latest Geode for gd=2.2091"
(*auto-check-updates* setting). As long as there is none, nothing happens; as soon as a newer
official version exists, it downloads it and switches to it on the next launch. Otherwise:
reinstall Geode with the official installer. The mod is then rebuilt normally
(`docs/GEODE_UPDATE.md`).

## Risks

- Development loader: it may crash, including outside the editor.
- 2.2091 bindings are a work in progress: the functions used are verified, the memory layout of the
  classes only partially (3 `GameObject` fields).
- Test on a **new level** first, after a backup.
