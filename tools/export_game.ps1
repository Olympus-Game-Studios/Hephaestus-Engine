# Hephaestus Engine - Game Export Pipeline
# =========================================
# This script exports a game project to a self-contained distributable package.
# It copies the game runtime executable, all required DLLs, assets, scripts, and shaders.
#
# Usage:
#   .\tools\export_game.ps1 -ProjectPath "path/to/project" -OutputPath "path/to/output"
#   .\tools\export_game.ps1 -ProjectPath "." -ProjectName "MyGame" -Configuration "Release"
#
# The output folder will contain everything needed to run the game:
#   MyGame/
#     MyGame.exe (renamed HephaestusGame.exe)
#     lua.dll
#     game.ini
#     assets/
#     scripts/
#     shaders/
#     logs/ (created at runtime)
#     RunGame.bat

param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectPath = ".",
    
    [Parameter(Mandatory=$false)]
    [string]$ProjectName = "",
    
    [Parameter(Mandatory=$false)]
    [string]$OutputPath = "",
    
    [Parameter(Mandatory=$false)]
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    
    [Parameter(Mandatory=$false)]
    [string]$EngineRoot = "",
    
    [Parameter(Mandatory=$false)]
    [switch]$CreateZip = $false,
    
    [Parameter(Mandatory=$false)]
    [switch]$Verbose = $false
)

# ─────────────────────────────────────────────────────────────────────────────
# Helper Functions
# ─────────────────────────────────────────────────────────────────────────────

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "HH:mm:ss"
    $color = switch ($Level) {
        "INFO"    { "Cyan" }
        "SUCCESS" { "Green" }
        "WARNING" { "Yellow" }
        "ERROR"   { "Red" }
        default   { "White" }
    }
    Write-Host "[$timestamp] [$Level] $Message" -ForegroundColor $color
}

function Test-PathExists {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path $Path)) {
        Write-Log "$Description not found: $Path" "ERROR"
        return $false
    }
    return $true
}

function Copy-ItemSafe {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$Description = "",
        [switch]$Recurse
    )
    
    if (-not (Test-Path $Source)) {
        Write-Log "Source not found: $Source" "WARNING"
        return $false
    }
    
    try {
        $destDir = if ($Recurse) { $Destination } else { Split-Path $Destination -Parent }
        if ($destDir -and -not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        
        if ($Recurse) {
            Copy-Item -Path $Source -Destination $Destination -Recurse -Force
        } else {
            Copy-Item -Path $Source -Destination $Destination -Force
        }
        
        $desc = if ($Description) { $Description } else { Split-Path $Source -Leaf }
        if ($Verbose) {
            Write-Log "Copied: $desc" "INFO"
        }
        return $true
    } catch {
        Write-Log "Failed to copy $Source : $_" "ERROR"
        return $false
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Resolve Paths
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "═══════════════════════════════════════════════════════════════" "INFO"
Write-Log "  Hephaestus Engine - Game Export Pipeline" "INFO"
Write-Log "═══════════════════════════════════════════════════════════════" "INFO"
Write-Log "" "INFO"

# Resolve project path
$ProjectPath = Resolve-Path $ProjectPath -ErrorAction SilentlyContinue
if (-not $ProjectPath) {
    Write-Log "Invalid project path" "ERROR"
    exit 1
}

# Auto-detect engine root
if (-not $EngineRoot) {
    # Look for engine root markers
    $candidates = @(
        (Join-Path $ProjectPath ".."),
        $ProjectPath,
        (Join-Path $PSScriptRoot ".."),
        "."
    )
    
    foreach ($candidate in $candidates) {
        $resolved = Resolve-Path $candidate -ErrorAction SilentlyContinue
        if ($resolved -and (Test-Path (Join-Path $resolved "engine")) -and (Test-Path (Join-Path $resolved "extern"))) {
            $EngineRoot = $resolved.Path
            break
        }
    }
    
    if (-not $EngineRoot) {
        # Fallback: assume script is in engine/tools
        $EngineRoot = Split-Path $PSScriptRoot -Parent
    }
}

Write-Log "Engine root: $EngineRoot" "INFO"
Write-Log "Project path: $ProjectPath" "INFO"
Write-Log "Configuration: $Configuration" "INFO"

# Auto-detect project name from folder
if (-not $ProjectName) {
    $ProjectName = Split-Path $ProjectPath -Leaf
    if ($ProjectName -eq "." -or $ProjectName -eq "") {
        $ProjectName = "HephaestusGame"
    }
}
Write-Log "Project name: $ProjectName" "INFO"

# Set output path
if (-not $OutputPath) {
    $OutputPath = Join-Path $ProjectPath "dist" $ProjectName
}
Write-Log "Output path: $OutputPath" "INFO"
Write-Log "" "INFO"

# ─────────────────────────────────────────────────────────────────────────────
# Locate Build Outputs
# ─────────────────────────────────────────────────────────────────────────────

$buildDir = Join-Path $EngineRoot "build" $Configuration

# Find game runtime executable
$gameExePaths = @(
    (Join-Path $buildDir "HephaestusGame.exe"),
    (Join-Path $EngineRoot "build" $Configuration "HephaestusGame.exe"),
    (Join-Path $EngineRoot "HephaestusGame.exe")
)

$gameExePath = $null
foreach ($path in $gameExePaths) {
    if (Test-Path $path) {
        $gameExePath = $path
        break
    }
}

if (-not $gameExePath) {
    Write-Log "Game runtime executable (HephaestusGame.exe) not found!" "ERROR"
    Write-Log "Build the game runtime first: cmake --build build --config $Configuration --target HephaestusGame" "ERROR"
    exit 1
}

Write-Log "Found game runtime: $gameExePath" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Create Output Directory
# ─────────────────────────────────────────────────────────────────────────────

if (Test-Path $OutputPath) {
    Write-Log "Cleaning existing output directory..." "INFO"
    Remove-Item -Path $OutputPath -Recurse -Force
}

New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
Write-Log "Created output directory: $OutputPath" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Copy Game Executable
# ─────────────────────────────────────────────────────────────────────────────

$destExeName = "$ProjectName.exe"
$destExePath = Join-Path $OutputPath $destExeName
Copy-ItemSafe -Source $gameExePath -Destination $destExePath -Description "Game executable"
Write-Log "Copied game executable as: $destExeName" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Copy Required DLLs
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Copying runtime dependencies..." "INFO"

# Lua DLL
$luaDllPaths = @(
    (Join-Path $buildDir "lua.dll"),
    "$env:USERPROFILE\vcpkg\installed\x64-windows\bin\lua.dll",
    (Join-Path $EngineRoot "build" $Configuration "lua.dll")
)

$luaCopied = $false
foreach ($path in $luaDllPaths) {
    if (Test-Path $path) {
        Copy-ItemSafe -Source $path -Destination (Join-Path $OutputPath "lua.dll") -Description "lua.dll"
        Write-Log "Copied lua.dll" "SUCCESS"
        $luaCopied = $true
        break
    }
}

if (-not $luaCopied) {
    Write-Log "lua.dll not found - scripting may not work!" "WARNING"
}

# ─────────────────────────────────────────────────────────────────────────────
# Copy Shaders
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Copying shaders..." "INFO"

$shaderSources = @(
    (Join-Path $buildDir "shaders"),
    (Join-Path $EngineRoot "build" $Configuration "shaders"),
    (Join-Path $EngineRoot "shaders")
)

$shadersCopied = $false
foreach ($shaderDir in $shaderSources) {
    if (Test-Path $shaderDir) {
        $destShaders = Join-Path $OutputPath "shaders"
        Copy-ItemSafe -Source $shaderDir -Destination $destShaders -Recurse -Description "Shaders"
        
        # Count .spv files
        $spvCount = (Get-ChildItem -Path $destShaders -Filter "*.spv" -Recurse).Count
        Write-Log "Copied $spvCount shader files" "SUCCESS"
        $shadersCopied = $true
        break
    }
}

if (-not $shadersCopied) {
    Write-Log "No shader directory found!" "ERROR"
    exit 1
}

# ─────────────────────────────────────────────────────────────────────────────
# Copy Assets
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Copying assets..." "INFO"

$assetSources = @(
    (Join-Path $ProjectPath "assets"),
    (Join-Path $EngineRoot "assets")
)

$assetsCopied = $false
foreach ($assetDir in $assetSources) {
    if (Test-Path $assetDir) {
        $destAssets = Join-Path $OutputPath "assets"
        Copy-ItemSafe -Source $assetDir -Destination $destAssets -Recurse -Description "Assets"
        
        $assetCount = (Get-ChildItem -Path $destAssets -Recurse -File).Count
        Write-Log "Copied $assetCount asset files" "SUCCESS"
        $assetsCopied = $true
        break
    }
}

if (-not $assetsCopied) {
    Write-Log "No assets directory found!" "WARNING"
}

# ─────────────────────────────────────────────────────────────────────────────
# Copy Scripts
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Copying scripts..." "INFO"

$scriptSources = @(
    (Join-Path $ProjectPath "scripts"),
    (Join-Path $EngineRoot "scripts")
)

$scriptsCopied = $false
foreach ($scriptDir in $scriptSources) {
    if (Test-Path $scriptDir) {
        $destScripts = Join-Path $OutputPath "scripts"
        Copy-ItemSafe -Source $scriptDir -Destination $destScripts -Recurse -Description "Scripts"
        
        $scriptCount = (Get-ChildItem -Path $destScripts -Filter "*.lua" -Recurse).Count
        Write-Log "Copied $scriptCount Lua script files" "SUCCESS"
        $scriptsCopied = $true
        break
    }
}

if (-not $scriptsCopied) {
    Write-Log "No scripts directory found" "INFO"
}

# ─────────────────────────────────────────────────────────────────────────────
# Create Configuration File
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Creating game configuration..." "INFO"

$gameIniPath = Join-Path $OutputPath "game.ini"
$gameIniContent = @"
# Hephaestus Game Configuration
# Generated by export_game.ps1 on $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

[Game]
project_name = $ProjectName
window_title = $ProjectName

[Display]
window_width = 1280
window_height = 720
fullscreen = false
vsync = true

[Rendering]
# max_fps = 60

[Audio]
# master_volume = 1.0
"@

$gameIniContent | Out-File -FilePath $gameIniPath -Encoding UTF8
Write-Log "Created game.ini" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Create Launcher Script
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Creating launcher scripts..." "INFO"

# Windows batch launcher
$runGameBatPath = Join-Path $OutputPath "RunGame.bat"
$runGameBatContent = @"
@echo off
title $ProjectName
cd /d "%~dp0"

echo Starting $ProjectName...
echo.

"$destExeName"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ════════════════════════════════════════════════════════════════
    echo  Game exited with error code: %ERRORLEVEL%
    echo  Check logs/runtime.log for details.
    echo ════════════════════════════════════════════════════════════════
    echo.
    pause
)
"@

$runGameBatContent | Out-File -FilePath $runGameBatPath -Encoding ASCII
Write-Log "Created RunGame.bat" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Create Export Manifest
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "Creating export manifest..." "INFO"

$manifestPath = Join-Path $OutputPath "export_manifest.txt"
$allFiles = Get-ChildItem -Path $OutputPath -Recurse -File

$manifestContent = @"
═══════════════════════════════════════════════════════════════════════════════
  Hephaestus Engine - Game Export Manifest
═══════════════════════════════════════════════════════════════════════════════

Project:        $ProjectName
Export Date:    $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Configuration:  $Configuration
Engine Root:    $EngineRoot

═══════════════════════════════════════════════════════════════════════════════
  PACKAGE CONTENTS
═══════════════════════════════════════════════════════════════════════════════

Total Files: $($allFiles.Count)
Total Size:  $([math]::Round(($allFiles | Measure-Object -Property Length -Sum).Sum / 1MB, 2)) MB

──────────────────────────────────────────────────────────────────────────────
  Executables
──────────────────────────────────────────────────────────────────────────────
$destExeName

──────────────────────────────────────────────────────────────────────────────
  Runtime Dependencies (DLLs)
──────────────────────────────────────────────────────────────────────────────
$((Get-ChildItem -Path $OutputPath -Filter "*.dll" | ForEach-Object { "  $($_.Name)" }) -join "`n")

──────────────────────────────────────────────────────────────────────────────
  Shaders
──────────────────────────────────────────────────────────────────────────────
$((Get-ChildItem -Path (Join-Path $OutputPath "shaders") -Filter "*.spv" -Recurse -ErrorAction SilentlyContinue | ForEach-Object { "  $($_.Name)" }) -join "`n")

──────────────────────────────────────────────────────────────────────────────
  Scripts
──────────────────────────────────────────────────────────────────────────────
$((Get-ChildItem -Path (Join-Path $OutputPath "scripts") -Filter "*.lua" -Recurse -ErrorAction SilentlyContinue | ForEach-Object { "  $($_.Name)" }) -join "`n")

═══════════════════════════════════════════════════════════════════════════════
  SYSTEM REQUIREMENTS
═══════════════════════════════════════════════════════════════════════════════

- Windows 10/11 (64-bit)
- Vulkan-compatible GPU with up-to-date drivers
- Visual C++ Redistributable 2019 or later

═══════════════════════════════════════════════════════════════════════════════
  HOW TO RUN
═══════════════════════════════════════════════════════════════════════════════

1. Double-click 'RunGame.bat' or '$destExeName'
2. Logs will be written to 'logs/runtime.log'
3. Crash reports (if any) will be saved to 'crashes/'

"@

$manifestContent | Out-File -FilePath $manifestPath -Encoding UTF8
Write-Log "Created export_manifest.txt" "SUCCESS"

# ─────────────────────────────────────────────────────────────────────────────
# Create ZIP Archive (Optional)
# ─────────────────────────────────────────────────────────────────────────────

if ($CreateZip) {
    Write-Log "Creating ZIP archive..." "INFO"
    
    $zipName = "$ProjectName`_$(Get-Date -Format 'yyyyMMdd_HHmmss').zip"
    $zipPath = Join-Path (Split-Path $OutputPath -Parent) $zipName
    
    try {
        Compress-Archive -Path "$OutputPath\*" -DestinationPath $zipPath -Force
        $zipSize = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
        Write-Log "Created archive: $zipName ($zipSize MB)" "SUCCESS"
    } catch {
        Write-Log "Failed to create ZIP archive: $_" "ERROR"
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────

Write-Log "" "INFO"
Write-Log "═══════════════════════════════════════════════════════════════" "SUCCESS"
Write-Log "  EXPORT COMPLETED SUCCESSFULLY!" "SUCCESS"
Write-Log "═══════════════════════════════════════════════════════════════" "SUCCESS"
Write-Log "" "INFO"
Write-Log "Output: $OutputPath" "INFO"
Write-Log "Files:  $($allFiles.Count)" "INFO"
Write-Log "Size:   $([math]::Round(($allFiles | Measure-Object -Property Length -Sum).Sum / 1MB, 2)) MB" "INFO"
Write-Log "" "INFO"
Write-Log "To run the game:" "INFO"
Write-Log "  1. Navigate to: $OutputPath" "INFO"
Write-Log "  2. Double-click: RunGame.bat" "INFO"
Write-Log "" "INFO"

exit 0
