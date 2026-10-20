<#
.SYNOPSIS
    Builds GD Decoration Studio.

.EXAMPLE
    ./scripts/build.ps1 -Target tests      # core library + unit tests + CLI, then runs the tests
    ./scripts/build.ps1 -Target mod        # Geode mod (.geode package in build-mod/)
    ./scripts/build.ps1 -Target mod -Install   # also copies the .geode into the GD mods folder
#>
param(
    [ValidateSet("tests", "mod")] [string] $Target = "tests",
    [string] $Config = "RelWithDebInfo",
    [switch] $Clean,
    [switch] $Install,
    # 2.2081 = stable Geode target; 2.2091 = experimental bindings (in progress upstream)
    [ValidateSet("2.2081", "2.2091")] [string] $GdVersion = "2.2081",
    # Build trees live in a short path: MSVC object paths are limited to ~250
    # characters and the Geode/CPM dependency tree is deep.
    [string] $BuildRoot = "$env:LOCALAPPDATA\gdds-build"
)
# Native tools (git, cmake) write progress to stderr; with "Stop", Windows
# PowerShell 5.1 would turn that into a terminating error. Exit codes are
# checked explicitly instead.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force $BuildRoot | Out-Null

# --- Locate the MSVC toolchain and import its environment (vcvars64) ---------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio / Build Tools installation with the MSVC x64 toolset was found." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
# vcvars itself calls vswhere, which is not on PATH by default.
$env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
if (-not $env:VSCMD_VER) {
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
    }
}

# --- CMake / Ninja (pip --user installs them here if they are not on PATH) ----
$pyScripts = "$env:APPDATA\Python\Python312\Scripts"
if ((-not (Get-Command cmake -ErrorAction SilentlyContinue)) -and (Test-Path $pyScripts)) { $env:PATH = "$pyScripts;$env:PATH" }
$geodeCli = "$env:LOCALAPPDATA\Programs\geode-cli"
if (Test-Path $geodeCli) { $env:PATH = "$geodeCli;$env:PATH" }
if (-not $env:GEODE_SDK) { $env:GEODE_SDK = [Environment]::GetEnvironmentVariable("GEODE_SDK", "User") }

if ($Target -eq "tests") {
    $build = Join-Path $BuildRoot "tests"
    if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
    cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DGDDS_BUILD_MOD=OFF -DGDDS_BUILD_TESTS=ON -DGDDS_BUILD_TOOLS=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build $build
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    & (Join-Path $build "tests\gdds_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Unit tests failed" }
}
else {
    $build = Join-Path $BuildRoot "mod"
    if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
    $installFlag = if ($Install) { "OFF" } else { "ON" }
    if ($GdVersion -eq "2.2091") { $build = Join-Path $BuildRoot "mod-2.2091" }
    cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DGDDS_BUILD_MOD=ON "-DGEODE_DONT_INSTALL_MODS=$installFlag" "-DGDDS_GD_VERSION=$GdVersion"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build $build
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    Get-ChildItem $build -Filter *.geode | ForEach-Object { "Built: $($_.FullName)" }
}
