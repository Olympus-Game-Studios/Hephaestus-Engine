# Download and extract EnTT header-only library
# EnTT is a header-only C++ library for Entity-Component-System

$enttVersion = "v3.13.2"
$enttUrl = "https://github.com/skypjack/entt/archive/refs/tags/$enttVersion.zip"
$downloadPath = "extern/entt.zip"
$extractPath = "extern/"

Write-Host "Downloading EnTT $enttVersion..." -ForegroundColor Cyan

try {
    # Download EnTT
    Invoke-WebRequest -Uri $enttUrl -OutFile $downloadPath
    Write-Host "Download complete!" -ForegroundColor Green

    # Extract the archive
    Write-Host "Extracting EnTT..." -ForegroundColor Cyan
    Expand-Archive -Path $downloadPath -DestinationPath $extractPath -Force

    # Move the extracted folder to the correct location
    $extractedFolder = "extern/entt-$($enttVersion.Substring(1))"
    if (Test-Path $extractedFolder) {
        # Remove old entt folder if it exists
        if (Test-Path "extern/entt") {
            Remove-Item -Path "extern/entt" -Recurse -Force
        }
        Move-Item -Path $extractedFolder -Destination "extern/entt"
        Write-Host "EnTT installed successfully!" -ForegroundColor Green
    }

    # Clean up
    Remove-Item -Path $downloadPath -Force
    Write-Host "Setup complete! EnTT is ready to use." -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
}
