<#
.SYNOPSIS
    Generates HTML API documentation for LightPDF using Doxygen.
.DESCRIPTION
    Discovers doxygen in PATH or standard installation directories,
    invokes Doxyfile from the repository root, and reports generation results.
#>

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

$cmd = Get-Command doxygen -ErrorAction SilentlyContinue
$doxygenExe = if ($cmd) { $cmd.Source } else { $null }

if (-not $doxygenExe) {
    $standardPaths = @(
        "$env:ProgramFiles\doxygen\bin\doxygen.exe",
        "${env:ProgramFiles(x86)}\doxygen\bin\doxygen.exe"
    )
    foreach ($p in $standardPaths) {
        if (Test-Path $p) {
            $doxygenExe = $p
            break
        }
    }
}

if (-not $doxygenExe) {
    Write-Host "[LightPDF] ERROR: Doxygen executable not found." -ForegroundColor Red
    Write-Host "Please install Doxygen using:"
    Write-Host "   winget install DimitrivanHeesch.Doxygen"
    Write-Host "or download from: https://www.doxygen.nl/download.html"
    exit 1
}

Write-Host "[LightPDF] Generating documentation with Doxygen ($doxygenExe)..." -ForegroundColor Cyan
& $doxygenExe "Doxyfile"

if ($LASTEXITCODE -eq 0) {
    $indexPath = Join-Path $PSScriptRoot "docs\html\index.html"
    Write-Host "===================================================" -ForegroundColor Green
    Write-Host "  DOCUMENTATION GENERATED SUCCESSFULLY!" -ForegroundColor Green
    Write-Host "  Index: $indexPath" -ForegroundColor Green
    Write-Host "===================================================" -ForegroundColor Green
} else {
    Write-Host "[LightPDF] ERROR: Doxygen encountered an error (exit code $LASTEXITCODE)." -ForegroundColor Red
    exit $LASTEXITCODE
}
