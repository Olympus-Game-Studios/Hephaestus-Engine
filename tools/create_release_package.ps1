# Hephaestus Engine - Create Release Package
# This script creates a portable release package that can run outside the repository

param(
    [Parameter(Mandatory=$false)]
    [string]$OutputPath = ".\dist",
    
    [Parameter(Mandatory=$false)]
    [string]$Configuration = "Release",
    
    [Parameter(Mandatory=$false)]
    [switch]$RuntimeMode = $false,
    
    [Parameter(Mandatory=$false)]
    [switch]$CreateZip = $false
)

$ErrorActionPreference = "Stop"

# Color output helpers
function Write-Success { param([string]$Message) Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Warn { param([string]$Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

# Get script and project directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build\$Configuration"

Write-Info "Hephaestus Engine Release Packager"
Write-Info "==================================="
Write-Host ""
Write-Info "Project Root: $ProjectRoot"
Write-Info "Build Directory: $BuildDir"
Write-Info "Output: $OutputPath"
Write-Host ""

# Verify build exists
if (-not (Test-Path (Join-Path $BuildDir "HephaestusEngine.exe"))) {
    Write-Err "Build not found at $BuildDir"
    Write-Info "Run 'cmake --build build --config $Configuration' first"
    exit 1
}

# Create output directory
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
if (Test-Path $OutputPath) {
    Write-Warn "Output directory exists, cleaning: $OutputPath"
    Remove-Item -Recurse -Force $OutputPath
}
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null

Write-Info "Creating release package..."

# Copy executable
Write-Info "Copying executable..."
Copy-Item (Join-Path $BuildDir "HephaestusEngine.exe") $OutputPath
Write-Success "HephaestusEngine.exe"

# Copy Lua DLL if present
$luaDll = Join-Path $BuildDir "lua.dll"
if (Test-Path $luaDll) {
    Copy-Item $luaDll $OutputPath
    Write-Success "lua.dll"
}

# Copy shaders
Write-Info "Copying shaders..."
$shadersSource = Join-Path $BuildDir "shaders"
$shadersDest = Join-Path $OutputPath "shaders"
if (Test-Path $shadersSource) {
    Copy-Item -Recurse $shadersSource $shadersDest
    $shaderCount = (Get-ChildItem $shadersDest -Filter "*.spv").Count
    Write-Success "shaders/ ($shaderCount SPV files)"
} else {
    # Fall back to source shaders
    $shadersSource = Join-Path $ProjectRoot "shaders"
    if (Test-Path $shadersSource) {
        New-Item -ItemType Directory -Path $shadersDest -Force | Out-Null
        Get-ChildItem $shadersSource -Filter "*.spv" | Copy-Item -Destination $shadersDest
        $shaderCount = (Get-ChildItem $shadersDest -Filter "*.spv").Count
        Write-Success "shaders/ ($shaderCount SPV files from source)"
    } else {
        Write-Err "No shaders found!"
        exit 1
    }
}

# Copy assets
Write-Info "Copying assets..."
$assetsSource = Join-Path $BuildDir "assets"
$assetsDest = Join-Path $OutputPath "assets"
if (-not (Test-Path $assetsSource)) {
    $assetsSource = Join-Path $ProjectRoot "assets"
}
if (Test-Path $assetsSource) {
    Copy-Item -Recurse $assetsSource $assetsDest
    $assetCount = (Get-ChildItem $assetsDest -Recurse -File).Count
    Write-Success "assets/ ($assetCount files)"
} else {
    Write-Warn "No assets directory found"
    New-Item -ItemType Directory -Path $assetsDest -Force | Out-Null
}

# Copy editor resources (icons, fonts)
Write-Info "Copying editor resources..."
$editorSource = Join-Path $BuildDir "editor"
$editorDest = Join-Path $OutputPath "editor"
if (-not (Test-Path $editorSource)) {
    $editorSource = Join-Path $ProjectRoot "editor"
}
if (Test-Path $editorSource) {
    Copy-Item -Recurse $editorSource $editorDest
    $editorCount = (Get-ChildItem $editorDest -Recurse -File).Count
    Write-Success "editor/ ($editorCount files)"
} else {
    Write-Warn "No editor directory found"
}

# Create README file (doesn't trigger runtime mode)
$readmePath = Join-Path $OutputPath "README.txt"
$packageType = if ($RuntimeMode) { "Runtime" } else { "Editor" }
@"
# Hephaestus Engine - Portable $packageType Package
# =============================================
# Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
# Configuration: $Configuration
# Package Type: $packageType
#
# To run: Double-click HephaestusEngine.exe
#
# Required folder structure:
#   HephaestusEngine.exe  - Main executable
#   shaders/              - Compiled SPIR-V shaders
#   assets/               - Game assets (textures, meshes, scenes)
#   editor/               - Editor resources (icons, fonts)
#   lua.dll               - Lua runtime (optional)
#
# System Requirements:
#   - Windows 10/11 (64-bit)
#   - Vulkan-capable GPU with up-to-date drivers
#   - Visual C++ Redistributable 2019 or later
"@ | Out-File -FilePath $readmePath -Encoding UTF8
Write-Success "README.txt"

# Only create runtime markers if -RuntimeMode is specified
if ($RuntimeMode) {
    # Create runtime marker file (triggers runtime mode detection)
    $manifestPath = Join-Path $OutputPath "PACKAGE_MANIFEST.txt"
    "Hephaestus Runtime Package - $(Get-Date)" | Out-File -FilePath $manifestPath -Encoding UTF8
    Write-Success "PACKAGE_MANIFEST.txt (runtime marker)"
    
    # Create batch file launcher for runtime
    $batchPath = Join-Path $OutputPath "RunGame.bat"
@"
@echo off
cd /d "%~dp0"
start HephaestusEngine.exe --runtime %*
"@ | Out-File -FilePath $batchPath -Encoding ASCII
    Write-Success "RunGame.bat"
}

# Summary
Write-Host ""
Write-Info "==================================="
Write-Success "$packageType package created successfully!"
Write-Host ""
Write-Info "Package location: $OutputPath"
Write-Info "Package size: $([math]::Round((Get-ChildItem $OutputPath -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB, 2)) MB"
Write-Host ""

# Create ZIP if requested
if ($CreateZip) {
    $zipName = "HephaestusEngine_${packageType}_$(Get-Date -Format 'yyyyMMdd').zip"
    $zipPath = Join-Path (Split-Path $OutputPath -Parent) $zipName
    Write-Info "Creating ZIP archive: $zipName"
    Compress-Archive -Path "$OutputPath\*" -DestinationPath $zipPath -Force
    Write-Success "Created: $zipPath"
    Write-Info "ZIP size: $([math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB"
}

Write-Host ""
Write-Info "To test the package:"
Write-Host "  1. Copy the '$OutputPath' folder to any location" -ForegroundColor White
Write-Host "  2. Run HephaestusEngine.exe or RunGame.bat" -ForegroundColor White
