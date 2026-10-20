<#
.SYNOPSIS
    Restores the Geode loader files and resources backed up by install_2091.ps1
    and removes the experimental GD Decoration Studio build from the mods folder.
    Studio projects (geode\mods\toyger.gd-decoration-studio\) are kept. Save
    files are NOT restored automatically (they are in <backup>\saves if needed).

    -RestoreGeodeFolder (only for a backup made with -FullGeodeBackup) instead puts
    the whole <GD>\geode folder back exactly as it was before the install: anything
    created there since (mod settings, logs, studio projects) is discarded.
#>
param(
    [Parameter(Mandatory = $true)] [string] $Backup,
    [string] $GdDir = "D:\SteamLibrary\steamapps\common\Geometry Dash",
    [switch] $RestoreGeodeFolder
)
$ErrorActionPreference = "Stop"
if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw "Close Geometry Dash first." }
if (-not (Test-Path (Join-Path $Backup "README.txt"))) { throw "Not a GD Decoration Studio backup folder: $Backup" }
$geodeCopy = Join-Path $Backup "geode-full"
if ($RestoreGeodeFolder -and -not (Test-Path $geodeCopy)) { throw "This backup has no full copy of the geode folder (install without -FullGeodeBackup)." }

foreach ($f in @("Geode.dll", "Geode.pdb", "XInput1_4.dll", "GeodeUpdater.exe")) {
    $src = Join-Path $Backup $f
    if (Test-Path $src) { Copy-Item $src $GdDir -Force }
}

if ($RestoreGeodeFolder) {
    robocopy $geodeCopy (Join-Path $GdDir "geode") /MIR /COPY:DAT /DCOPY:T /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Restoring the geode folder failed (robocopy exit $LASTEXITCODE)." }
    "Restored the loader and the whole geode folder from $Backup."
    exit 0
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
exit 0
