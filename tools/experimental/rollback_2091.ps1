<#
.SYNOPSIS
    Restores the Geode loader files and resources backed up by install_2091.ps1
    and removes the experimental GD Decoration Studio build from the mods folder.
    Studio projects (geode\mods\toyger.gd-decoration-studio\) are kept. Save
    files are NOT restored automatically (they are in <backup>\saves if needed).
#>
param(
    [Parameter(Mandatory = $true)] [string] $Backup,
    [string] $GdDir = "D:\SteamLibrary\steamapps\common\Geometry Dash"
)
$ErrorActionPreference = "Stop"
if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw "Close Geometry Dash first." }
if (-not (Test-Path (Join-Path $Backup "README.txt"))) { throw "Not a GD Decoration Studio backup folder: $Backup" }

foreach ($f in @("Geode.dll", "Geode.pdb", "XInput1_4.dll", "GeodeUpdater.exe")) {
    $src = Join-Path $Backup $f
    if (Test-Path $src) { Copy-Item $src $GdDir -Force }
}
$savedResources = Join-Path $Backup "geode.loader-resources"
if (Test-Path $savedResources) {
    $resources = Join-Path $GdDir "geode\resources\geode.loader"
    if (Test-Path $resources) { Remove-Item $resources -Recurse -Force }
    Copy-Item $savedResources $resources -Recurse
}

$mod = Join-Path $GdDir "geode\mods\toyger.gd-decoration-studio.geode"
if (Test-Path $mod) { Remove-Item $mod }
$previousMod = Join-Path $Backup "toyger.gd-decoration-studio.geode"
if (Test-Path $previousMod) { Copy-Item $previousMod (Join-Path $GdDir "geode\mods") }
$unzipped = Join-Path $GdDir "geode\unzipped\toyger.gd-decoration-studio"
if (Test-Path $unzipped) { Remove-Item $unzipped -Recurse -Force }
"Restored the loader from $Backup and removed the experimental mod."
