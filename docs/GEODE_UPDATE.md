# When Geode supports GD 2.2091

As of 2026-10-20, Geode 5.11.2 runs GD 2.2091 in *forward compatibility mode* and refuses every mod
targeting 2.2081 (including this one). Steps to follow when the update ships. (Until then, see
`docs/EXPERIMENTAL_2091.md` for the experimental build.)

## 1. Check that the update is really there

- Geode's release notes (https://github.com/geode-sdk/geode/releases) announce support for
  **2.2091** (not just forward-compat "fixes").
- After updating, the latest log (`<GD>\geode\logs`) no longer contains
  `(forward compatibility mode)` in the `Running Geode vX in Geometry Dash ...` line.

## 2. Update the game

1. Back up `%LOCALAPPDATA%\GeometryDash\CCLocalLevels.dat` and `CCGameManager.dat`.
2. Start GD: the loader updates itself (or use Geode's official installer).
3. Update the other mods from Geode's mod list.

## 3. Rebuild the mod

```powershell
geode sdk update                      # updates %USERPROFILE%\geode-sdk to the new version
geode sdk install-binaries --platform win
./scripts/build.ps1 -Target mod -GdVersion 2.2091 -Clean
```

If the SDK is a shallow clone pinned to the v5.11.2 tag and `geode sdk update` fails, delete
`%USERPROFILE%\geode-sdk`, then run `geode sdk install "%USERPROFILE%\geode-sdk"` (fresh clone).

If the build fails (a new major Geode version means API changes), the fixes belong in the adapter
(`mod/src`), which is deliberately thin; the engine (`core/`) does not depend on Geode. The
experimental build already went through the v6 API changes (`asp::fs::Path`, see `mod/src/Files.hpp`).

Once it builds: `./scripts/build.ps1 -Target mod -GdVersion 2.2091 -Install` copies the `.geode`
into the game's mods folder (Geode CLI `steam` profile).

## 4. First in-game test

On a **new test level**, never on an existing one: follow the checklist in `docs/TESTING.md`
("In the game" section). For each problem, note the step and attach the Geode log
(`<GD>\geode\logs`, `[GD Decoration Studio]` lines).

Watch these first: color refresh, button / Alt+D shortcut, mask drawing, preview (cyan outlines),
save / reopen.
