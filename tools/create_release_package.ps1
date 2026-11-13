# Hephaestus Engine - Create Windows Release Package
# Builds Release from this checkout and writes the folder uploaded to Steam: the launcher,
# editor, game player, build tool, Qt (via windeployqt), Lua, plugins and content, without
# debug symbols, tests or development-only content. The VC++ runtime is not bundled; Steam
# installs it.
#
#   tools\create_release_package.ps1 [-OutputPath <dir>] [-BuildDirectory <dir>] [-Qt6Dir <dir>]

param(
    [string]$OutputPath = "",
    [string]$BuildDirectory = "",
    [string]$Qt6Dir = "C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $ProjectRoot "dist\windows" }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $ProjectRoot "build\steam-windows" }
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$BuildOutput = Join-Path $BuildDirectory "Release"

$Qt6Dir = [System.IO.Path]::GetFullPath($Qt6Dir)
$WinDeployQt = Join-Path $Qt6Dir "..\..\..\bin\windeployqt.exe"
if (-not (Test-Path (Join-Path $Qt6Dir "Qt6Config.cmake") -PathType Leaf) -or
    -not (Test-Path $WinDeployQt -PathType Leaf)) {
    throw "Qt not found at $Qt6Dir (expected Qt6Config.cmake and ..\..\..\bin\windeployqt.exe). Pass -Qt6Dir."
}

# Plugins that only exist for development, not for users.
$ExcludedPlugins = @("HephaestusFPS")

Write-Host "Building Release in $BuildDirectory with Qt from $Qt6Dir"
if (-not (Test-Path (Join-Path $ProjectRoot "extern\JoltPhysics\Build\CMakeLists.txt")) -or
    -not (Test-Path (Join-Path $ProjectRoot "extern\glfw\CMakeLists.txt")) -or
    -not (Test-Path (Join-Path $ProjectRoot "extern\ufbx\ufbx.h"))) {
    git -C $ProjectRoot submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }
}
cmake -S $ProjectRoot -B $BuildDirectory -G "Visual Studio 17 2022" -A x64 `
    "-DQt6_DIR=$Qt6Dir" `
    -DHEPHAESTUS_BUILD_EDITOR=ON -DHEPHAESTUS_BUILD_GAME=ON -DHEPHAESTUS_USE_QT=ON `
    -DHEPHAESTUS_BUILD_COMPAT_TESTS=OFF -DHEPHAESTUS_BUILD_QT_TESTS=OFF `
    -DHEPHAESTUS_RUNTIME_AUTOMATION=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
$configuredQt = (Select-String -Path (Join-Path $BuildDirectory "CMakeCache.txt") -Pattern "^Qt6_DIR:PATH=(.*)$").Matches[0].Groups[1].Value
if ([System.IO.Path]::GetFullPath($configuredQt) -ne $Qt6Dir) {
    throw "The build is configured with Qt from $configuredQt, not $Qt6Dir"
}
cmake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

foreach ($required in @("HephaestusLauncher.exe", "HephaestusEngine.exe", "HephaestusGame.exe",
                        "tools\HephaestusBuild.exe")) {
    if (-not (Test-Path (Join-Path $BuildOutput $required) -PathType Leaf)) {
        throw "The build did not produce $required"
    }
}
$luaDll = Get-ChildItem $BuildOutput -Filter "lua*.dll" -File | Select-Object -First 1
if (-not $luaDll) { throw "The build did not copy a Lua DLL into $BuildOutput" }

if (Test-Path $OutputPath) {
    $existing = @(Get-ChildItem -Force $OutputPath)
    if ($existing.Count -gt 0 -and -not (Test-Path (Join-Path $OutputPath "HephaestusEngine.exe"))) {
        throw "$OutputPath is not empty and is not a previous release package; choose another -OutputPath"
    }
    Remove-Item -Recurse -Force $OutputPath
}
New-Item -ItemType Directory -Path $OutputPath | Out-Null
Write-Host "Staging $OutputPath"

foreach ($file in @("HephaestusLauncher.exe", "HephaestusEngine.exe", "HephaestusGame.exe", $luaDll.Name)) {
    Copy-Item (Join-Path $BuildOutput $file) $OutputPath
}
New-Item -ItemType Directory -Path (Join-Path $OutputPath "tools") | Out-Null
Copy-Item (Join-Path $BuildOutput "tools\HephaestusBuild.exe") (Join-Path $OutputPath "tools")
foreach ($notice in @("LICENSE.md", "THIRD_PARTY_NOTICES.md")) {
    Copy-Item (Join-Path $ProjectRoot $notice) $OutputPath
}

New-Item -ItemType Directory -Path (Join-Path $OutputPath "shaders") | Out-Null
Copy-Item (Join-Path $BuildOutput "shaders\*.spv") (Join-Path $OutputPath "shaders")
Copy-Item -Recurse (Join-Path $ProjectRoot "assets") (Join-Path $OutputPath "assets")
Remove-Item -Recurse -Force (Join-Path $OutputPath "assets\tests")
Copy-Item -Recurse (Join-Path $ProjectRoot "editor") (Join-Path $OutputPath "editor")
New-Item -ItemType Directory -Path (Join-Path $OutputPath "scripts") | Out-Null
Copy-Item (Join-Path $ProjectRoot "scripts\*.lua") (Join-Path $OutputPath "scripts")
New-Item -ItemType Directory -Path (Join-Path $OutputPath ".engine") | Out-Null
Copy-Item -Recurse (Join-Path $ProjectRoot ".engine\meshes") (Join-Path $OutputPath ".engine\meshes")
Copy-Item -Recurse (Join-Path $ProjectRoot "assets\textures\hdris") (Join-Path $OutputPath ".engine\hdris")

New-Item -ItemType Directory -Path (Join-Path $OutputPath "Plugins") | Out-Null
Get-ChildItem (Join-Path $BuildOutput "Plugins") -Directory |
    Where-Object { $ExcludedPlugins -notcontains $_.Name } |
    ForEach-Object { Copy-Item -Recurse $_.FullName (Join-Path $OutputPath "Plugins\$($_.Name)") }
Get-ChildItem $OutputPath -Recurse -File -Include *.pdb, *.lib, *.exp, *.ilk | Remove-Item -Force

Write-Host "Deploying Qt"
$qtTargets = @((Join-Path $OutputPath "HephaestusEngine.exe"), (Join-Path $OutputPath "HephaestusLauncher.exe")) +
    @(Get-ChildItem (Join-Path $OutputPath "Plugins") -Recurse -Filter "*.dll" | ForEach-Object FullName)
& $WinDeployQt --release --dir $OutputPath --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
    --skip-plugin-types generic,networkinformation $qtTargets
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
# The Sketchfab plugin's HTTPS uses Windows' own TLS (Schannel); the OpenSSL backend would need OpenSSL DLLs.
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $OutputPath "tls\qopensslbackend.dll"), (Join-Path $OutputPath "tls\qcertonlybackend.dll")
# windeployqt copies Windows' own ICU from System32; Windows 10 and later already have it.
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $OutputPath "icuuc.dll")

$problems = @()
foreach ($required in @("HephaestusLauncher.exe", "HephaestusEngine.exe", "HephaestusGame.exe",
                        "tools\HephaestusBuild.exe", "Qt6Core.dll", "Qt6Widgets.dll", "Qt6Svg.dll",
                        "Qt6Network.dll", "tls\qschannelbackend.dll",
                        "platforms\qwindows.dll", "LICENSE.md", "THIRD_PARTY_NOTICES.md",
                        "editor", "assets", "shaders", ".engine\meshes", ".engine\hdris", "Plugins")) {
    if (-not (Test-Path (Join-Path $OutputPath $required))) { $problems += "missing $required" }
}
$problems += @(Get-ChildItem $OutputPath -Recurse -File -Include *.pdb, vc_redist*, msvcp*.dll, vcruntime*.dll, icu*.dll, dxcompiler.dll, qopensslbackend.dll |
    ForEach-Object { "unexpected $($_.FullName)" })
foreach ($plugin in $ExcludedPlugins) {
    if (Test-Path (Join-Path $OutputPath "Plugins\$plugin")) { $problems += "excluded plugin $plugin is present" }
}
if ($problems.Count -gt 0) { throw "Release package check failed:`n  " + ($problems -join "`n  ") }

$size = (Get-ChildItem $OutputPath -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("Release package: {0} ({1:N0} MB)" -f $OutputPath, ($size / 1MB))
