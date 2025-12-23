# Hephaestus Engine - Package Validation & Manifest Generator
# This script validates that all required runtime files are present in an export directory
# and generates a detailed manifest for distribution.

param(
    [Parameter(Mandatory=$false)]
    [string]$ExportPath = ".\dist",
    
    [Parameter(Mandatory=$false)]
    [string]$ProjectName = "HephaestusProject",
    
    [Parameter(Mandatory=$false)]
    [switch]$CreateZip = $false,
    
    [Parameter(Mandatory=$false)]
    [switch]$ShowDetails = $false
)

# Color output helpers
function Write-Success { param([string]$Message) Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Warning { param([string]$Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Error { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }
function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }

# Check if export directory exists
if (-not (Test-Path $ExportPath)) {
    Write-Error "Export directory not found: $ExportPath"
    Write-Info "Please run the editor Export command first, or specify a valid export path with -ExportPath"
    exit 1
}

Write-Info "Validating Hephaestus Engine export package..."
Write-Info "Export path: $ExportPath"
Write-Info "Project: $ProjectName"
Write-Host ""

# Initialize validation results
$validationPassed = $true
$warnings = @()
$requiredFiles = @()
$optionalFiles = @()

# 1. Check for runtime executable
Write-Info "Checking for runtime executable..."
$exePatterns = @("HephaestusEngine.exe", "*.exe", "HephaestusRuntime.exe")
$foundExe = $null

foreach ($pattern in $exePatterns) {
    $exe = Get-ChildItem -Path $ExportPath -Filter $pattern -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) {
        $foundExe = $exe
        break
    }
}

if ($foundExe) {
    Write-Success "Found runtime executable: $($foundExe.Name)"
    $requiredFiles += $foundExe.FullName
} else {
    Write-Warning "Runtime executable not found. You'll need to copy the built executable to this directory."
    $warnings += "Missing runtime executable"
    $validationPassed = $false
}

# 2. Check for shaders directory
Write-Info "Checking for shaders..."
$shadersDir = Join-Path $ExportPath "shaders"
if (Test-Path $shadersDir) {
    $shaderFiles = Get-ChildItem -Path $shadersDir -File -Recurse
    if ($shaderFiles.Count -gt 0) {
        Write-Success "Found $($shaderFiles.Count) shader files"
        $requiredFiles += $shadersDir
    } else {
        Write-Warning "Shaders directory is empty"
        $warnings += "No shader files found"
    }
} else {
    Write-Warning "Shaders directory not found. Copy shaders/ from your build output."
    $warnings += "Missing shaders directory"
    $validationPassed = $false
}

# 3. Check for scene files
Write-Info "Checking for scene files..."
$sceneFiles = Get-ChildItem -Path $ExportPath -Filter "*.hpscene" -Recurse
if ($sceneFiles.Count -gt 0) {
    Write-Success "Found $($sceneFiles.Count) scene file(s)"
    foreach ($scene in $sceneFiles) {
        $requiredFiles += $scene.FullName
    }
} else {
    Write-Warning "No scene files found (.hpscene)"
    $warnings += "No scene files"
}

# 4. Check for assets
Write-Info "Checking for assets..."
$assetsDir = Join-Path $ExportPath "assets"
if (Test-Path $assetsDir) {
    $assetCount = (Get-ChildItem -Path $assetsDir -File -Recurse).Count
    Write-Success "Found assets directory with $assetCount files"
    $requiredFiles += $assetsDir
} else {
    Write-Warning "Assets directory not found"
    $warnings += "No assets directory"
}

# 5. Check for manifest
Write-Info "Checking for export manifest..."
$manifestPath = Join-Path $ExportPath "export_manifest.txt"
if (Test-Path $manifestPath) {
    Write-Success "Found export manifest"
    $optionalFiles += $manifestPath
} else {
    Write-Info "No manifest found (optional)"
}

# 6. Check for required DLLs (Vulkan, etc.)
Write-Info "Checking for runtime dependencies..."
$dllPatterns = @("vulkan-1.dll", "*.dll")
$foundDlls = Get-ChildItem -Path $ExportPath -Filter "*.dll" -Recurse -ErrorAction SilentlyContinue
if ($foundDlls.Count -gt 0) {
    Write-Success "Found $($foundDlls.Count) DLL(s)"
    foreach ($dll in $foundDlls) {
        $optionalFiles += $dll.FullName
    }
} else {
    Write-Info "No DLLs found. May need to copy Vulkan runtime if not system-installed."
}

# Display summary
Write-Host ""
Write-Info "=== Validation Summary ==="
Write-Host ""

if ($validationPassed) {
    Write-Success "Package validation PASSED"
    Write-Info "The export package contains all essential files for distribution."
} else {
    Write-Warning "Package validation completed with WARNINGS"
    Write-Info "The following issues were found:"
    foreach ($warning in $warnings) {
        Write-Host "  • $warning" -ForegroundColor Yellow
    }
}

# Generate enhanced manifest
Write-Host ""
Write-Info "Generating package manifest..."

$manifestContent = @"
# ==========================================
# Hephaestus Engine - Package Manifest
# ==========================================
# Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
# Project: $ProjectName
# Engine Version: 1.0.0-dev
# Export Path: $ExportPath
# ==========================================

## Validation Status
Status: $(if ($validationPassed) { "PASSED" } else { "WARNINGS" })
Warnings: $($warnings.Count)

## Runtime Executable
$(if ($foundExe) { "[OK] " + $foundExe.Name } else { "[MISSING] - copy runtime executable here" })

## Required Components

### Shaders
$(if (Test-Path $shadersDir) { 
    "[OK] shaders/ directory present`n" + 
    ((Get-ChildItem -Path $shadersDir -File -Recurse | ForEach-Object { "  - " + $_.Name }) -join "`n")
} else { 
    "[MISSING] shaders/ directory" 
})

### Scenes
$(if ($sceneFiles.Count -gt 0) {
    ($sceneFiles | ForEach-Object { "[OK] " + $_.Name }) -join "`n"
} else {
    "[MISSING] No scene files found"
})

### Assets
$(if (Test-Path $assetsDir) {
    $assetFiles = Get-ChildItem -Path $assetsDir -File -Recurse
    "[OK] assets/ directory ($($assetFiles.Count) files)`n" +
    ($assetFiles | Select-Object -First 20 | ForEach-Object { 
        $rel = $_.FullName.Substring($ExportPath.Length + 1)
        "  - $rel"
    }) -join "`n"
    if ($assetFiles.Count -gt 20) { "`n  ... and $($assetFiles.Count - 20) more files" }
} else {
    "[MISSING] assets/ directory"
})

## Optional Components

### Runtime Dependencies (DLLs)
$(if ($foundDlls.Count -gt 0) {
    ($foundDlls | ForEach-Object { "  - " + $_.Name }) -join "`n"
} else {
    "  None packaged (may require system installation)"
})

## Distribution Notes
- Ensure Vulkan SDK is installed on target systems (or bundle vulkan-1.dll)
- Test the packaged application on a clean system before distribution
- Consider creating an installer or zip archive for end users

## How to Run
1. Ensure Vulkan runtime is available
2. Run $($foundExe.Name) from this directory
3. The engine will load assets from the assets/ subdirectory

"@

$enhancedManifestPath = Join-Path $ExportPath "PACKAGE_MANIFEST.txt"
$manifestContent | Out-File -FilePath $enhancedManifestPath -Encoding UTF8
Write-Success "Created detailed manifest: PACKAGE_MANIFEST.txt"

# Create ZIP archive if requested
if ($CreateZip) {
    Write-Host ""
    Write-Info "Creating ZIP archive..."
    
    $zipName = "${ProjectName}_Package_$(Get-Date -Format 'yyyyMMdd_HHmmss').zip"
    $zipPath = Join-Path (Split-Path $ExportPath -Parent) $zipName
    
    try {
        Compress-Archive -Path "$ExportPath\*" -DestinationPath $zipPath -Force
        Write-Success "Created archive: $zipName"
        Write-Info "Archive location: $zipPath"
    } catch {
        Write-Error "Failed to create ZIP archive: $_"
    }
}

# Display next steps
Write-Host ""
Write-Info "=== Next Steps ==="
Write-Host ""
Write-Host "1. Copy your runtime executable (e.g., HephaestusEngine.exe) to the export directory" -ForegroundColor Cyan
Write-Host "2. Test the packaged application on your development machine" -ForegroundColor Cyan
Write-Host "3. Test on a clean system without development tools installed" -ForegroundColor Cyan
Write-Host "4. Create an installer or distribute as a ZIP archive" -ForegroundColor Cyan
Write-Host ""

if ($ShowDetails) {
    Write-Info "=== Detailed File Listing ==="
    Get-ChildItem -Path $ExportPath -Recurse | ForEach-Object {
        $indent = "  " * (($_.FullName.Substring($ExportPath.Length) -split '\\').Count - 1)
        Write-Host "$indent$($_.Name)" -ForegroundColor Gray
    }
}

exit $(if ($validationPassed) { 0 } else { 1 })
