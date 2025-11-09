<#
Create GitHub issues from markdown drafts in `.github/ISSUE_DRAFTS/`.

Requirements:
- GitHub CLI (`gh`) installed and authenticated (`gh auth login`).

Usage:
From repository root (PowerShell):

    .\tools\create_github_issues.ps1

The script will create one issue per draft file. It will skip README.md and any non-markdown files.
#>

Set-StrictMode -Version Latest

$draftDir = Join-Path $PSScriptRoot "..\.github\ISSUE_DRAFTS"
$draftDir = Resolve-Path $draftDir

Write-Host "Reading issue drafts from $draftDir"

$files = Get-ChildItem -Path $draftDir -Filter "*.md" | Where-Object { $_.Name -ne 'README.md' } | Sort-Object Name

foreach ($f in $files) {
    $content = Get-Content -Raw -Path $f.FullName
    # Extract first heading as title
    $titleLine = ($content -split "\r?\n" | Where-Object { $_ -match '^#+' } | Select-Object -First 1)
    if (-not $titleLine) {
        Write-Warning "Skipping $($f.Name): no title heading found"
        continue
    }
    # Remove leading '#' and trim
    $title = $titleLine -replace '^#+', '' -replace '^\s+', '' -replace '\s+$',''

    Write-Host "Creating issue: $title"
    try {
        gh issue create --title "$title" --body "$content" | Out-Null
        Write-Host "Created issue from $($f.Name)"
    } catch {
        Write-Error "Failed to create issue from $($f.Name): $_"
    }
}

Write-Host "Done. Review created issues on GitHub." -ForegroundColor Green
