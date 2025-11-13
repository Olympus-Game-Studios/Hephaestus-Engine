param(
    [string]$SourceDir = "editor/Icons/logo",
    [string]$OutputIcon = "engine_logo.ico"
)

$pngCandidates = @(
    "256px.png",
    "128px.png",
    "64px.png",
    "48px.png",
    "32px.png",
    "16px.png"
)

function Write-IconFromPngs {
    param(
        [System.Collections.ArrayList]$Entries,
        [string]$Destination
    )

    if ($Entries.Count -eq 0) {
        throw "No PNG sources available to build icon."
    }

    $fs = [System.IO.File]::Open($Destination, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $writer = New-Object System.IO.BinaryWriter($fs)
        $writer.Write([UInt16]0)   # reserved
        $writer.Write([UInt16]1)   # icon type
        $writer.Write([UInt16]$Entries.Count)

        $offset = 6 + 16 * $Entries.Count
        foreach ($entry in $Entries) {
            $writer.Write([byte]$entry.Width)
            $writer.Write([byte]$entry.Height)
            $writer.Write([byte]0) # color count
            $writer.Write([byte]0) # reserved
            $writer.Write([UInt16]0) # planes
            $writer.Write([UInt16]32) # bit depth
            $writer.Write([UInt32]$entry.Data.Length)
            $writer.Write([UInt32]$offset)
            $offset += $entry.Data.Length
        }

        foreach ($entry in $Entries) {
            $writer.Write($entry.Data)
        }
        $writer.Flush()
    }
    finally {
        $fs.Dispose()
    }
}

if (-not (Test-Path $SourceDir)) {
    throw "Source directory '$SourceDir' does not exist."
}

try {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
}
catch {
    throw "System.Drawing assembly is required but could not be loaded: $_"
}

$iconEntries = New-Object System.Collections.ArrayList

foreach ($candidate in $pngCandidates) {
    $path = Join-Path $SourceDir $candidate
    if (-not (Test-Path $path)) {
        Write-Verbose "Skipping missing icon size: $candidate"
        continue
    }

    try {
        $image = [System.Drawing.Image]::FromFile($path)
    }
    catch {
        Write-Warning "Failed to read $candidate as an image: $_"
        continue
    }

    try {
        $data = [System.IO.File]::ReadAllBytes($path)
        $entry = [PSCustomObject]@{
            Width  = if ($image.Width -eq 256) { 0 } else { [byte][Math]::Min($image.Width, 255) }
            Height = if ($image.Height -eq 256) { 0 } else { [byte][Math]::Min($image.Height, 255) }
            Data   = $data
        }
        [void]$iconEntries.Add($entry)
        Write-Host ("Included {0} ({1}x{2})" -f $candidate, $image.Width, $image.Height) -ForegroundColor Green
    }
    finally {
        $image.Dispose()
    }
}

if ($iconEntries.Count -eq 0) {
    throw "No valid PNG files were found in '$SourceDir'."
}

$destinationPath = [System.IO.Path]::GetFullPath($OutputIcon)
$destinationDir = [System.IO.Path]::GetDirectoryName($destinationPath)
if (-not (Test-Path $destinationDir)) {
    New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
}

Write-IconFromPngs -Entries $iconEntries -Destination $destinationPath
Write-Host "Generated icon: $destinationPath" -ForegroundColor Cyan
