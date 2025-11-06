$engineRoot = "f:\Engines\Hephaestus-Engine"
$engineMeshesPath = "$engineRoot\assets\meshes"
$engineTexturesPath = "$engineRoot\assets\textures"
$projectPath = "f:\Engines\Hephaestus-Engine\projects\TestProject"
$projectModelsPath = "$projectPath\assets\models"
$projectTexturesPath = "$projectPath\assets\textures"

Write-Host "Engine meshes path exists: $(Test-Path $engineMeshesPath)"
Write-Host "Engine textures path exists: $(Test-Path $engineTexturesPath)"
Write-Host "Project models path exists: $(Test-Path $projectModelsPath)"
Write-Host "Project textures path exists: $(Test-Path $projectTexturesPath)"

if (Test-Path $engineMeshesPath) {
    Get-ChildItem $engineMeshesPath -File | ForEach-Object {
        Write-Host "Copying mesh: $($_.Name)"
        Copy-Item -Path $_.FullName -Destination (Join-Path $projectModelsPath $_.Name) -Force
    }
}

if (Test-Path $engineTexturesPath) {
    Get-ChildItem $engineTexturesPath -File | ForEach-Object {
        Write-Host "Copying texture: $($_.Name)"
        Copy-Item -Path $_.FullName -Destination (Join-Path $projectTexturesPath $_.Name) -Force
    }
}

Write-Host "Done. Checking results:"
Get-ChildItem -Recurse $projectPath
