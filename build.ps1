$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  Building LightPDF (Ultra-Fast Windows PDF Viewer)" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}
if (-not (Test-Path "bin\dict")) {
    New-Item -ItemType Directory -Path "bin\dict" -Force | Out-Null
}
if (Test-Path "dict\en-ar.dat") {
    Copy-Item -Path "dict\*" -Destination "bin\dict\" -Recurse -Force
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

Write-Host "Compiling optimized C++20 release binary with /O2 /MT /LTCG /AVX2..." -ForegroundColor Green

$sources = @(
    "src\main.cpp",
    "src\app_window.cpp",
    "src\d2d_renderer.cpp",
    "src\pdf_document.cpp",
    "src\pdf_parser.cpp",
    "src\pdf_search.cpp",
    "src\dictionary_engine.cpp",
    "src\pdf_searchable_writer.cpp",
    "src\tab_controller.cpp",
    "src\search_controller.cpp",
    "src\selection_controller.cpp"
)

Write-Host "Compiling Windows resource script (app.rc)..." -ForegroundColor Green
& rc.exe /nologo /i resources /fo "resources\app.res" "resources\app.rc"

$clArgs = @(
    "/nologo",
    "/O2",
    "/Ob3",
    "/fp:fast",
    "/arch:AVX2",
    "/GA",
    "/Oi",
    "/GF",
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
    "windowscodecs.lib",
    "comdlg32.lib",
    "advapi32.lib",
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
