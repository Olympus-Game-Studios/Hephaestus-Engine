param(
    [string]$ProjectPath = ".",
    [string]$ProjectName = "",
    [string]$OutputPath = "",
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$EngineRoot = "",
    [string]$StartScene = "",
    [string]$WindowTitle = "",
    [int]$WindowWidth = 1280,
    [int]$WindowHeight = 720,
    [switch]$Fullscreen = $false,
    [switch]$NoVsync = $false,
    [switch]$CreateZip = $false,
    [switch]$DetailedLog = $false
)

$ErrorActionPreference = "Stop"
Write-Warning "export_game.ps1 is deprecated and now delegates to HephaestusBuild.exe."

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = $repoRoot
}
$buildConfiguration = if ($Configuration -eq "Debug") { "Development" } else { "Release" }
$toolCandidates = @(
    (Join-Path $EngineRoot "build\$Configuration\HephaestusBuild.exe"),
    (Join-Path $PSScriptRoot "HephaestusBuild.exe")
)
$buildTool = $toolCandidates | Where-Object { Test-Path $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $buildTool) {
    throw "HephaestusBuild.exe was not found. Build it or use Build > Package in the Editor."
}

$arguments = @(
    "--project", ([System.IO.Path]::GetFullPath($ProjectPath)),
    "--config", $buildConfiguration,
    "--engine-root", ([System.IO.Path]::GetFullPath($EngineRoot))
)
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $arguments += @("--output", ([System.IO.Path]::GetFullPath($OutputPath)))
}
if ($CreateZip) {
    $arguments += "--zip"
}

if (-not [string]::IsNullOrWhiteSpace($ProjectName) -or
    -not [string]::IsNullOrWhiteSpace($StartScene) -or
    -not [string]::IsNullOrWhiteSpace($WindowTitle) -or
    $WindowWidth -ne 1280 -or $WindowHeight -ne 720 -or $Fullscreen -or $NoVsync) {
    Write-Warning "Legacy metadata overrides are ignored. Configure product, startup scene, and window defaults in Project Settings."
}

& $buildTool @arguments
exit $LASTEXITCODE
