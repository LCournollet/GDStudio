# Experimental build for GD 2.2091 (before Geode's official release)

Status as of 2026-10-20: **built and verified outside the game, not installed, not tested in the game.**

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
   loader. Geode v6 replaces `std::filesystem::path` with `asp::fs::Path`: the mod's file access goes
   through `mod/src/Files.hpp`, which compiles with both versions (the stable 2.2081 build is unchanged).
3. Command: `./tools/experimental/build_2091.ps1 -Part all` (prerequisites at the top of the script).

## Checks performed (without starting the game)

| Check | Result |
|---|---|
| v6 loader built (`Geode.dll`, `Geode.lib`, `XInput1_4.dll`, `GeodeUpdater.exe`) | OK |
| Mod built for `gd.win = 2.2091` | OK |
| 3 `GameObject` offsets measured in the exe = the bindings' offsets (`static_assert`, `mod/src/LayoutChecks.cpp`) | OK |
| 30 game functions used: exact function starts (`.pdata`), virtuals present in vtables (`tools/experimental/verify_addresses.py`) | 30/30 |
| Compiled bindings = verified bindings (diff of the `.bro` files) | identical |
| Mod imports from `Geode.dll` present in the built loader | 72/72 |
| Mod imports from the installed game's `libcocos2d.dll` / `libExtensions.dll` / glew32 / fmod | 1092 + 171 + 2 + 9, 0 missing |
| Loader delay-load imports from the game's `libcocos2d.dll` / `libExtensions.dll` | 1174 + 172, 0 missing |
| Install then rollback on a **copy** of the game folder | identical to the start (SHA-256, 0 differences) |
| Stable 2.2081 build + tests | 40/40 tests, mod packaged |

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
