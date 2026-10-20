<#
.SYNOPSIS
    EXPERIMENTAL: installs a locally built Geode loader (v6 development branch,
    native GD 2.2091 target) and GD Decoration Studio built for 2.2091.

    Everything replaced is backed up first; tools/experimental/rollback_2091.ps1
    restores the previous state. Geometry Dash must be closed.

    Note: with this loader your other mods (made for 2.2081) are refused, exactly
    as they already are with Geode 5.11.2 in forward-compatibility mode.
#>
param(
    [string] $GdDir = "D:\SteamLibrary\steamapps\common\Geometry Dash",
    # Output of %LOCALAPPDATA%\gdds-build\exp2091\build_loader_v6.ps1
    [string] $LoaderBuild = "$env:USERPROFILE\geode-sdk-v6\bin\nightly",
    [string] $ModPackage = "$env:LOCALAPPDATA\gdds-build\exp2091\mod\mod\toyger.gd-decoration-studio.geode"
)
$ErrorActionPreference = "Stop"

if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw "Close Geometry Dash first." }
$required = @($GdDir, $ModPackage, (Join-Path $GdDir "GeometryDash.exe"), (Join-Path $GdDir "geode\mods"),
              (Join-Path $LoaderBuild "Geode.dll"), (Join-Path $LoaderBuild "XInput1_4.dll"),
              (Join-Path $LoaderBuild "GeodeUpdater.exe"), (Join-Path $LoaderBuild "resources\mod.json"))
foreach ($p in $required) { if (-not (Test-Path $p)) { throw "Missing: $p" } }

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path $GdDir "gdds-backup-$stamp"
New-Item -ItemType Directory -Force $backup | Out-Null

# 1. Back up the current loader files, the loader's resources and the player's save files.
foreach ($f in @("Geode.dll", "Geode.pdb", "XInput1_4.dll", "GeodeUpdater.exe")) {
    $src = Join-Path $GdDir $f
    if (Test-Path $src) { Copy-Item $src $backup }
}
$resources = Join-Path $GdDir "geode\resources\geode.loader"
if (Test-Path $resources) { Copy-Item $resources (Join-Path $backup "geode.loader-resources") -Recurse }
$existingMod = Join-Path $GdDir "geode\mods\toyger.gd-decoration-studio.geode"
if (Test-Path $existingMod) { Copy-Item $existingMod $backup }
$saves = Join-Path $env:LOCALAPPDATA "GeometryDash"
$saveBackup = Join-Path $backup "saves"
New-Item -ItemType Directory -Force $saveBackup | Out-Null
foreach ($f in @("CCLocalLevels.dat", "CCLocalLevels2.dat", "CCGameManager.dat", "CCGameManager2.dat")) {
    $src = Join-Path $saves $f
    if (Test-Path $src) { Copy-Item $src $saveBackup }
}
Set-Content -Path (Join-Path $backup "README.txt") -Encoding ascii -Value @(
    "Backup made by GD Decoration Studio install_2091.ps1 on $stamp.",
    "Restore with: tools/experimental/rollback_2091.ps1 -Backup `"$backup`"",
    "Save files (not restored automatically) are in the saves folder."
)

# 2. Install the experimental loader. Its resources must be the ones it was built
#    with: the loader checks them by hash and would otherwise download the 5.11.2 set.
foreach ($f in @("Geode.dll", "Geode.pdb", "XInput1_4.dll", "GeodeUpdater.exe")) {
    $src = Join-Path $LoaderBuild $f
    if (Test-Path $src) { Copy-Item $src $GdDir -Force }
}
if (Test-Path $resources) { Remove-Item $resources -Recurse -Force }
New-Item -ItemType Directory -Force $resources | Out-Null
Copy-Item (Join-Path $LoaderBuild "resources\*") $resources -Recurse -Force

# 3. Install the mod (other mods are left untouched).
Copy-Item $ModPackage (Join-Path $GdDir "geode\mods") -Force

"Installed. Backup: $backup"
"To undo: ./tools/experimental/rollback_2091.ps1 -Backup `"$backup`""
