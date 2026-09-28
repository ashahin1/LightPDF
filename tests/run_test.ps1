$ErrorActionPreference = "Stop"
Set-Location -Path (Split-Path -Parent $PSScriptRoot)

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (Test-Path "tools\msvc\activate.ps1") {
        . "tools\msvc\activate.ps1"
    }
}

$srcs = @(
    "tests\unified_tests.cpp",
    "src\pdf_document.cpp",
    "src\dictionary_engine.cpp",
    "src\tab_controller.cpp",
    "src\search_controller.cpp",
    "src\selection_controller.cpp",
    "src\pdf_parser.cpp",
    "src\pdf_searchable_writer.cpp",
    "src\pdf_search.cpp",
    "src\ui_views.cpp"
)

$libs = @(
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
    "windowscodecs.lib"
)

$compileArgs = @(
    "/nologo",
    "/O2",
    "/MT",
    "/std:c++20",
    "/EHsc",
    "/utf-8",
    "/DNOMINMAX",
    "/I", "src",
    "/I", "tests"
) + $srcs + @(
    "/link",
    "/OUT:tests\unified_tests.exe"
) + $libs

Write-Host "Compiling LightPDF Unified Test Suite..." -ForegroundColor Cyan
& cl.exe $compileArgs
if ($LASTEXITCODE -eq 0) {
    Write-Host "Running Unified Test Suite..." -ForegroundColor Green
    & ".\tests\unified_tests.exe"
    exit $LASTEXITCODE
} else {
    Write-Error "Compilation of tests failed!"
    exit 1
}
