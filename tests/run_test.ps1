param(
    [switch]$Rebuild,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location -Path $root
[System.IO.Directory]::SetCurrentDirectory($root)

# 1. Handle Clean
if ($Clean) {
    Write-Host "Cleaning test artifacts..." -ForegroundColor Yellow
    if (Test-Path "bin\obj_test") { Remove-Item -Path "bin\obj_test" -Recurse -Force }
    if (Test-Path "tests\unified_tests.exe") { Remove-Item -Path "tests\unified_tests.exe" -Force }
    Write-Host "Test clean completed." -ForegroundColor Green
    exit 0
}

# 2. Handle Rebuild
if ($Rebuild) {
    Write-Host "Full test rebuild requested. Purging bin\obj_test..." -ForegroundColor Yellow
    if (Test-Path "bin\obj_test") { Remove-Item -Path "bin\obj_test" -Recurse -Force }
    if (Test-Path "tests\unified_tests.exe") { Remove-Item -Path "tests\unified_tests.exe" -Force }
}

# 3. Ensure obj_test directory
if (-not (Test-Path "bin\obj_test")) {
    New-Item -ItemType Directory -Path "bin\obj_test" -Force | Out-Null
}

# 4. Activate MSVC
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (Test-Path "tools\msvc\activate.ps1") {
        . "tools\msvc\activate.ps1"
    } elseif (Test-Path "tools\msvc\setup_x64.bat") {
        $tempEnv = [System.IO.Path]::GetTempFileName() + ".bat"
        @"
@echo off
call "$PSScriptRoot\..\tools\msvc\setup_x64.bat" >nul
set > "$tempEnv.txt"
"@ | Set-Content -Path $tempEnv -Encoding ASCII
        cmd.exe /c $tempEnv
        Get-Content "$tempEnv.txt" | ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
            }
        }
        Remove-Item -Path $tempEnv, "$tempEnv.txt" -Force -ErrorAction SilentlyContinue
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
    "src\ui_views.cpp",
    "src\tts_engine.cpp",
    "src\read_aloud_controller.cpp",
    "src\recent_files_controller.cpp",
    "src\pdf_decryptor.cpp",
    "src\context_menu_controller.cpp",
    "src\settings_controller.cpp"
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
    "windowscodecs.lib",
    "advapi32.lib",
    "bcrypt.lib"
)

# 5. Header dependency scanner with memoization
$headerDepCache = @{}
function Get-HeaderDeps($filePath, $depth = 0) {
    if ($depth -gt 3) { return @() }
    if ($headerDepCache.ContainsKey($filePath)) { return $headerDepCache[$filePath] }

    $deps = [System.Collections.Generic.HashSet[string]]::new()
    if (Test-Path $filePath) {
        $content = [System.IO.File]::ReadAllText($filePath)
        $matches = [regex]::Matches($content, '(?m)^\s*#include\s+"([^"]+)"')
        foreach ($m in $matches) {
            $hName = $m.Groups[1].Value
            $cand1 = Join-Path "src" $hName
            $cand2 = Join-Path "tests" $hName
            $target = $null
            if (Test-Path $cand1) { $target = $cand1 }
            elseif (Test-Path $cand2) { $target = $cand2 }
            if ($target) {
                [void]$deps.Add((Resolve-Path $target).Path)
                foreach ($nested in (Get-HeaderDeps $target ($depth + 1))) {
                    [void]$deps.Add($nested)
                }
            }
        }
    }
    $arr = @($deps)
    $headerDepCache[$filePath] = $arr
    return $arr
}

# 6. Check for stale source files
$staleSources = [System.Collections.Generic.List[string]]::new()
$objFiles = [System.Collections.Generic.List[string]]::new()

foreach ($src in $srcs) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $objPath = "bin\obj_test\$baseName.obj"
    $objFiles.Add($objPath)

    if (-not (Test-Path $objPath)) {
        $staleSources.Add($src)
        continue
    }

    $objTime = (Get-Item $objPath).LastWriteTimeUtc
    if ((Get-Item $src).LastWriteTimeUtc -gt $objTime) {
        $staleSources.Add($src)
        continue
    }

    $isHeaderStale = $false
    foreach ($depHeader in (Get-HeaderDeps $src)) {
        if ((Test-Path $depHeader) -and (Get-Item $depHeader).LastWriteTimeUtc -gt $objTime) {
            $isHeaderStale = $true
            break
        }
    }

    if ($isHeaderStale) {
        $staleSources.Add($src)
    }
}

# 7. Compile stale files with /MP
$recompiled = $false
if ($staleSources.Count -gt 0) {
    Write-Host "Compiling $($staleSources.Count) test source file(s) with /MP across all cores..." -ForegroundColor Cyan
    $clArgs = @(
        "/nologo",
        "/MP",
        "/O2",
        "/MT",
        "/std:c++20",
        "/EHsc",
        "/utf-8",
        "/DNOMINMAX",
        "/I", "src",
        "/I", "tests",
        "/c",
        "/Fo`"bin\obj_test/`"",
        "/Fd`"bin\obj_test/`""
    ) + $staleSources

    & cl.exe $clArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation of test sources failed!"
        exit $LASTEXITCODE
    }
    $recompiled = $true
}

# 8. Link if necessary
$testExe = "tests\unified_tests.exe"
$needLink = $recompiled -or (-not (Test-Path $testExe))

if (-not $needLink) {
    $exeTime = (Get-Item $testExe).LastWriteTimeUtc
    foreach ($obj in $objFiles) {
        if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTimeUtc -gt $exeTime) {
            $needLink = $true
            break
        }
    }
}

if ($needLink) {
    Write-Host "Linking $testExe..." -ForegroundColor Cyan
    $linkArgs = @(
        "/nologo",
        "/OUT:$testExe"
    ) + $objFiles + $libs

    & link.exe $linkArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Linking of test executable failed!"
        exit $LASTEXITCODE
    }
} else {
    Write-Host "Test executable is up to date." -ForegroundColor Green
}

# 9. Run the test suite
Write-Host "Running Unified Test Suite..." -ForegroundColor Green
& ".\$testExe"
exit $LASTEXITCODE
