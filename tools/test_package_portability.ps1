param(
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot "build\$Configuration"
}
$buildTool = Join-Path $BuildDirectory "tools\HephaestusBuild.exe"
$runtimeRoot = Split-Path -Parent (Split-Path -Parent $buildTool)
$project = Join-Path $repoRoot "engine\Tests\Fixtures\PackagePortabilityProject"

if (-not (Test-Path $buildTool -PathType Leaf)) {
    throw "HephaestusBuild.exe was not found at $buildTool"
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "hephaestus_portability_" + [Guid]::NewGuid().ToString("N"))
$packageRoot = Join-Path $testRoot "package-output"
$relocatedRoot = Join-Path $testRoot "relocated"
$stdoutPath = Join-Path $testRoot "game.stdout.log"
$stderrPath = Join-Path $testRoot "game.stderr.log"

try {
    New-Item -ItemType Directory -Path $packageRoot, $relocatedRoot -Force | Out-Null

    & $buildTool `
        --project $project `
        --config Release `
        --output $packageRoot `
        --engine-root $runtimeRoot
    if ($LASTEXITCODE -ne 0) {
        throw "HephaestusBuild failed with exit code $LASTEXITCODE"
    }

    $package = Join-Path $packageRoot "Stage_6_Portability"
    $required = @(
        "Stage_6_Portability.exe",
        "lua54.dll",
        "game.ini",
        "build_manifest.json",
        "assets\scenes\Portability.hpscene",
        "assets\materials\Portability.hematerial",
        "assets\textures\portability.ppm",
        "assets\prefabs\PortabilityLamp.hpprefab",
        "assets\ui\Portability.hui",
        "assets\audio\portability.wav",
        "scripts\portability.lua",
        "shaders",
        ".engine\meshes",
        ".engine\hdris\portability.hdr"
    )
    foreach ($relative in $required) {
        if (-not (Test-Path (Join-Path $package $relative))) {
            throw "Package is missing required path: $relative"
        }
    }
    if (Test-Path (Join-Path $package ".hephaestus")) {
        throw "Editor-only .hephaestus data leaked into the package"
    }

    $relocatedPackage = Join-Path $relocatedRoot "Stage_6_Portability"
    Copy-Item -Recurse $package $relocatedPackage
    $executable = Join-Path $relocatedPackage "Stage_6_Portability.exe"
    $process = Start-Process -FilePath $executable `
        -WorkingDirectory $relocatedPackage `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Relocated game did not exit within $TimeoutSeconds seconds"
    }
    # Complete redirected stream draining before reading ExitCode or log files.
    $process.WaitForExit()
    $process.Refresh()

    $runtimeLog = Join-Path $relocatedPackage "logs\runtime.log"
    if (-not (Test-Path $runtimeLog -PathType Leaf)) {
        throw "Relocated game did not create logs/runtime.log"
    }
    $combinedLog = (
        (Get-Content $runtimeLog -Raw) + "`n" +
        (Get-Content $stdoutPath -Raw -ErrorAction SilentlyContinue) + "`n" +
        (Get-Content $stderrPath -Raw -ErrorAction SilentlyContinue)
    )
    foreach ($marker in @(
        "Scene loaded successfully",
        "STAGE6_LUA_AWAKE",
        "STAGE6_HUI_CONTROLLER_LOADED",
        "STAGE6_PORTABILITY_COMPLETE",
        "Shutting down..."
    )) {
        if (-not $combinedLog.Contains($marker)) {
            throw "Relocated game log is missing marker: $marker"
        }
    }
    if (-not $combinedLog.Contains("Runtime root: $relocatedPackage")) {
        throw "Runtime root was not the relocated package"
    }
    if ($combinedLog.Contains($repoRoot)) {
        throw "Relocated game accessed or logged the engine repository: $repoRoot"
    }

    Write-Host "[PASS] Gate 2: relocated standalone package launched successfully."
    Write-Host "[PASS] Runtime root: $relocatedPackage"
    Write-Host "[PASS] No engine repository path appeared in runtime output."
}
finally {
    if (Test-Path $testRoot) {
        Remove-Item -Recurse -Force $testRoot
    }
}
