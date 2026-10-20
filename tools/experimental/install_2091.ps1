<#
.SYNOPSIS
    EXPERIMENTAL: installs a locally built Geode loader (v6 development branch,
    native GD 2.2091 target) and GD Decoration Studio built for 2.2091.

    Everything replaced is backed up first; tools/experimental/rollback_2091.ps1
    restores the previous state. Geometry Dash must be closed.

    Note: with this loader your other mods (made for 2.2081) are refused, exactly
    as they already are with Geode 5.11.2 in forward-compatibility mode.

    -FullGeodeBackup also copies the whole <GD>\geode folder (mods, settings,
    mod data, logs) into the backup, so rollback_2091.ps1 -RestoreGeodeFolder can
    put it back exactly as it was.
#>
param(
    [string] $GdDir = "D:\SteamLibrary\steamapps\common\Geometry Dash",
    # Output of tools/experimental/build_2091.ps1
    [string] $LoaderBuild = "$env:USERPROFILE\geode-sdk-v6\bin\nightly",
    [string] $ModPackage = "$env:LOCALAPPDATA\gdds-build\exp2091\mod\mod\toyger.gd-decoration-studio.geode",
    [switch] $FullGeodeBackup
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

# 0. Optional full copy of the geode folder, checked before anything is changed.
if ($FullGeodeBackup) {
    $geodeDir = Join-Path $GdDir "geode"
    $geodeCopy = Join-Path $backup "geode-full"
    robocopy $geodeDir $geodeCopy /E /COPY:DAT /DCOPY:T /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Full backup of $geodeDir failed (robocopy exit $LASTEXITCODE). Nothing was changed." }
    $src = Get-ChildItem $geodeDir -Recurse -File -Force
    $dst = Get-ChildItem $geodeCopy -Recurse -File -Force
    $srcBytes = ($src | Measure-Object Length -Sum).Sum
    $dstBytes = ($dst | Measure-Object Length -Sum).Sum
    if ($src.Count -ne $dst.Count -or $srcBytes -ne $dstBytes) {
        throw "Full backup incomplete ($($dst.Count)/$($src.Count) files). Nothing was changed."
    }
    "Full geode backup: $($dst.Count) files, $([math]::Round($dstBytes / 1MB, 1)) MB"
}

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
    "Save files (not restored automatically) are in the saves folder.",
    "geode-full (if present) is a full copy of the geode folder: restore it with -RestoreGeodeFolder."
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
exit 0
