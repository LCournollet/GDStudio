<#
.SYNOPSIS
    EXPERIMENTAL: builds the Geode v6 loader (development branch, native GD 2.2091
    target) from source, then GD Decoration Studio against it. Nothing is installed
    into the game (see install_2091.ps1 for that).

    Prerequisites (see docs/EXPERIMENTAL_2091.md):
      - git clone -b v6 https://github.com/geode-sdk/geode $env:USERPROFILE\geode-sdk-v6
      - LLVM 21 (clang-cl, lld-link) extracted in %LOCALAPPDATA%\Programs\clang+llvm-21*
        (the loader only compiles with Clang, like Geode's CI)
      - Visual Studio Build Tools (MSVC x64, Windows SDK), CMake, Ninja, Geode CLI

.EXAMPLE
    ./tools/experimental/build_2091.ps1 -Part all -Clean
#>
param(
    [ValidateSet("loader", "mod", "all")] [string] $Part = "all",
    [switch] $Clean,
    [string] $Sdk = "$env:USERPROFILE\geode-sdk-v6",
    [string] $BuildRoot = "$env:LOCALAPPDATA\gdds-build\exp2091"
)
$ErrorActionPreference = "Continue"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not (Test-Path "$Sdk\VERSION")) { throw "Geode v6 SDK not found in $Sdk" }
New-Item -ItemType Directory -Force $BuildRoot | Out-Null

# --- Toolchain ----------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio / Build Tools installation with the MSVC x64 toolset was found." }
$env:PATH = "$(Split-Path -Parent $vswhere);$env:APPDATA\Python\Python312\Scripts;$env:LOCALAPPDATA\Programs\geode-cli;$env:PATH"
if (-not $env:VSCMD_VER) {
    cmd /c "`"$(Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat')`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
    }
}
$llvm = (Get-ChildItem "$env:LOCALAPPDATA\Programs" -Directory -Filter "clang+llvm-21*" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
$env:GEODE_SDK = $Sdk
$loaderBuild = Join-Path $BuildRoot "loader"
$failed = $false

function Show-Log($log, $pattern) {
    Get-Content $log | Select-String -Pattern $pattern | Select-Object -First 40 | ForEach-Object {
        $l = $_.Line -replace [regex]::Escape($root), ""
        $l.Substring(0, [Math]::Min(320, $l.Length))
    }
}

# --- Loader ---------------------------------------------------------------------
if ($Part -in @("loader", "all")) {
    if (-not $llvm) { throw "LLVM 21 not found in %LOCALAPPDATA%\Programs (clang+llvm-21*)" }
    $env:PATH = "$llvm\bin;$env:PATH"
    # Geode's prebuilt libcurl was built with Windows SDK 10.0.26100 and needs
    # volatileaccessu.lib (RtlSetVolatileMemory). Older SDKs do not ship it, so a
    # one-function stand-in (sdkshim/volatileaccess.c) is linked instead.
    $shim = Join-Path $BuildRoot "sdkshim"
    New-Item -ItemType Directory -Force $shim | Out-Null
    & "$llvm\bin\clang-cl.exe" /nologo /c /O2 /GS- /Zl "/Fo$shim\volatileaccess.obj" "$PSScriptRoot\sdkshim\volatileaccess.c"
    & "$llvm\bin\llvm-lib.exe" /nologo "/out:$shim\volatileaccessu.lib" "$shim\volatileaccess.obj"
    if ($LASTEXITCODE -ne 0) { throw "could not build the volatileaccessu shim" }
    if ($Clean -and (Test-Path $loaderBuild)) { Remove-Item -Recurse -Force $loaderBuild }
    $linkFlags = "/machine:x64 /LIBPATH:$shim"
    $log = Join-Path $BuildRoot "loader-build.log"
    cmake -S $Sdk -B $loaderBuild -G Ninja "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DCMAKE_C_COMPILER=clang-cl" `
        "-DCMAKE_CXX_COMPILER=clang-cl" "-DCMAKE_LINKER=lld-link" "-DCMAKE_SHARED_LINKER_FLAGS=$linkFlags" `
        "-DCMAKE_EXE_LINKER_FLAGS=$linkFlags" *> $log
    if ($LASTEXITCODE -eq 0) { cmake --build $loaderBuild *>> $log }
    "loader: exit $LASTEXITCODE (log: $log)"
    if ($LASTEXITCODE -ne 0) { $failed = $true; Show-Log $log "error:|fatal error|FAILED|undefined" }
    # The loader build writes to <sdk>\bin\nightly.
    Get-ChildItem "$Sdk\bin\nightly" -Include Geode.dll, Geode.lib, XInput1_4.dll, GeodeUpdater.exe -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { "  $($_.Name) $($_.Length)" }
}

# --- Mod --------------------------------------------------------------------------
if (-not $failed -and $Part -in @("mod", "all")) {
    # The SDK links <sdk>\bin\nightly when <sdk>\bin\<version> does not exist.
    $ver = (Get-Content "$Sdk\VERSION").Trim()
    if (Test-Path "$Sdk\bin\$ver") { throw "$Sdk\bin\$ver exists and would shadow the locally built loader in bin\nightly" }
    if (-not (Test-Path "$Sdk\bin\nightly\Geode.lib")) { throw "build the loader first (-Part loader)" }
    # Same bindings revision as the loader (GD 2.2091 addresses), plus the
    # fixes this mod verified against the executable (verify_addresses.py).
    $bindingsSrc = Join-Path $loaderBuild "_deps\bindings-src"
    $bindings = Join-Path $BuildRoot "bindings-gdds"
    robocopy $bindingsSrc $bindings /MIR /XD .git /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "could not copy the bindings from $bindingsSrc" }
    $fixes = @(
        # LevelEditorLayer::removeObject: 0x223320 is a level-statistics function
        # called only by PlayLayer::onQuit; the real one is 0x3f21b0.
        @{ File = "bindings\2.2091\GeometryDash.bro";
           Old = "void removeObject(GameObject* object, bool noUndo) = win 0x223320,";
           New = "void removeObject(GameObject* object, bool noUndo) = win 0x3f21b0," }
    )
    foreach ($fix in $fixes) {
        $path = Join-Path $bindings $fix.File
        $text = [IO.File]::ReadAllText($path)
        if ($text.Contains($fix.Old)) { $text = $text.Replace($fix.Old, $fix.New); [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false))); "  bindings fix applied: $($fix.New)" }
        elseif ($text.Contains($fix.New)) { "  bindings already fixed upstream: $($fix.New)" }
        else { throw "bindings changed upstream, check by hand: $($fix.Old)" }
    }
    $env:GEODE_BINDINGS_REPO_PATH = $bindings
    $modBuild = Join-Path $BuildRoot "mod"
    if ($Clean -and (Test-Path $modBuild)) { Remove-Item -Recurse -Force $modBuild }
    $log = Join-Path $BuildRoot "mod-build.log"
    cmake -S $root -B $modBuild -G Ninja "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DGDDS_BUILD_MOD=ON" `
        "-DGDDS_GD_VERSION=2.2091" "-DGEODE_DONT_INSTALL_MODS=ON" *> $log
    if ($LASTEXITCODE -eq 0) { cmake --build $modBuild *>> $log }
    "mod: exit $LASTEXITCODE (log: $log)"
    if ($LASTEXITCODE -ne 0) { $failed = $true; Show-Log $log "error C|error:|fatal error|FAILED|LNK" }
    else { "  $modBuild\mod\toyger.gd-decoration-studio.geode" }
}
if ($failed) { exit 1 }
