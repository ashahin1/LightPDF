$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  Building LightPDF (Ultra-Fast Windows PDF Viewer)" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

# Activate portable MSVC if needed
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (Test-Path "tools\msvc\activate.ps1") {
        Write-Host "Activating portable MSVC..." -ForegroundColor Yellow
        . "tools\msvc\activate.ps1"
    } else {
        Write-Error "cl.exe not found and tools\msvc\activate.ps1 missing!"
    }
}

Write-Host "Compiling optimized C++20 release binary with /O2 /MT /LTCG..." -ForegroundColor Green

$sources = @(
    "src\main.cpp",
    "src\app_window.cpp",
    "src\d2d_renderer.cpp",
    "src\pdf_document.cpp"
)

Write-Host "Compiling Windows resource script (app.rc)..." -ForegroundColor Green
& rc.exe /nologo /i resources /fo "resources\app.res" "resources\app.rc"

$clArgs = @(
    "/nologo",
    "/O2",
    "/MT",
    "/std:c++20",
    "/GL",
    "/Gy",
    "/Gw",
    "/EHsc",
    "/utf-8",
    "/permissive-",
    "/DNOMINMAX"
) + $sources + @(
    "resources\app.res",
    "/link",
    "/LTCG",
    "/OPT:REF",
    "/OPT:ICF",
    "/SUBSYSTEM:WINDOWS",
    "/MANIFEST:EMBED",
    "/MANIFESTINPUT:resources\app.manifest",
    "d3d11.lib",
    "d2d1.lib",
    "dxgi.lib",
    "dwrite.lib",
    "windows.data.pdf.lib",
    "windowsapp.lib",
    "user32.lib",
    "gdi32.lib",
    "shell32.lib",
    "ole32.lib",
    "shcore.lib",
    "comdlg32.lib",
    "/OUT:bin\LightPDF.exe"
)

& cl.exe $clArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n===================================================" -ForegroundColor Cyan
    Write-Host "  BUILD SUCCESSFUL!" -ForegroundColor Green
    $bin = Get-Item "bin\LightPDF.exe"
    $sizeKb = [math]::Round($bin.Length / 1KB, 1)
    Write-Host "  Executable: $($bin.FullName)" -ForegroundColor White
    Write-Host "  Binary Size: $sizeKb KB" -ForegroundColor Yellow
    Write-Host "===================================================" -ForegroundColor Cyan

    # Cleanup temporary obj files
    Remove-Item *.obj -ErrorAction SilentlyContinue
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}
