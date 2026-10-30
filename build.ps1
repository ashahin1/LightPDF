param(
    [switch]$EnableAVX2,
    [switch]$Rebuild,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot
[System.IO.Directory]::SetCurrentDirectory($PSScriptRoot)

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  Building LightPDF (Ultra-Fast Windows PDF Viewer)" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

# 1. Handle Clean
if ($Clean) {
    Write-Host "Cleaning build artifacts..." -ForegroundColor Yellow
    if (Test-Path "bin\obj") { Remove-Item -Path "bin\obj" -Recurse -Force }
    if (Test-Path "bin\obj_test") { Remove-Item -Path "bin\obj_test" -Recurse -Force }
    if (Test-Path "bin\LightPDF.exe") { Remove-Item -Path "bin\LightPDF.exe" -Force }
    if (Test-Path "resources\app.res") { Remove-Item -Path "resources\app.res" -Force }
    Write-Host "Clean completed successfully." -ForegroundColor Green
    exit 0
}

# 2. Handle Rebuild
if ($Rebuild) {
    Write-Host "Full rebuild requested. Purging bin\obj..." -ForegroundColor Yellow
    if (Test-Path "bin\obj") { Remove-Item -Path "bin\obj" -Recurse -Force }
    if (Test-Path "resources\app.res") { Remove-Item -Path "resources\app.res" -Force }
}

# 3. Ensure required directories
if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}
if (-not (Test-Path "bin\obj")) {
    New-Item -ItemType Directory -Path "bin\obj" -Force | Out-Null
}
if (-not (Test-Path "bin\dict")) {
    New-Item -ItemType Directory -Path "bin\dict" -Force | Out-Null
}
if (Test-Path "dict\en-ar.dat") {
    Copy-Item -Path "dict\*" -Destination "bin\dict\" -Recurse -Force
}

# 4. Activate MSVC if cl.exe is not in PATH
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (Test-Path "tools\msvc\activate.ps1") {
        Write-Host "Activating portable MSVC..." -ForegroundColor Yellow
        . "$PSScriptRoot\tools\msvc\activate.ps1"
    } elseif (Test-Path "tools\msvc\setup_x64.bat") {
        Write-Host "Activating portable MSVC via setup_x64.bat..." -ForegroundColor Yellow
        $tempEnv = [System.IO.Path]::GetTempFileName() + ".bat"
        @"
@echo off
call "$PSScriptRoot\tools\msvc\setup_x64.bat" >nul
set > "$tempEnv.txt"
"@ | Set-Content -Path $tempEnv -Encoding ASCII
        cmd.exe /c $tempEnv
        Get-Content "$tempEnv.txt" | ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
            }
        }
        Remove-Item -Path $tempEnv, "$tempEnv.txt" -Force -ErrorAction SilentlyContinue
    } else {
        # Check standard Visual Studio installation via vswhere.exe
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $activated = $false
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path $vcvars) {
                    Write-Host "Activating Visual Studio environment via $vcvars..." -ForegroundColor Yellow
                    & cmd.exe /c "call `"$vcvars`" && set" | ForEach-Object {
                        if ($_ -match "^(.*?)=(.*)$") {
                            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
                        }
                    }
                    $activated = $true
                }
            }
        }
        if (-not $activated -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
            Write-Error "cl.exe not found and neither portable MSVC nor Visual Studio installation was detected!"
        }
    }
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

# 5. Compile Windows Resource Script (app.rc) incrementally
$rcInputs = @(
    "resources\app.rc",
    "resources\app.ico",
    "resources\app.manifest"
)
$resFile = "resources\app.res"
$needRc = -not (Test-Path $resFile)

if (-not $needRc) {
    $resTime = (Get-Item $resFile).LastWriteTimeUtc
    foreach ($rcIn in $rcInputs) {
        if ((Test-Path $rcIn) -and (Get-Item $rcIn).LastWriteTimeUtc -gt $resTime) {
            $needRc = $true
            break
        }
    }
}

if ($needRc) {
    Write-Host "Compiling Windows resource script (app.rc)..." -ForegroundColor Green
    & rc.exe /nologo /i resources /fo "resources\app.res" "resources\app.rc"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Resource compilation failed with exit code $LASTEXITCODE"
    }
}

# 6. Precompiled Header (PCH) Management
$pchHpp = "src\pch.hpp"
$pchCpp = "src\pch.cpp"
$pchBin = "bin\obj\pch.pch"
$pchObj = "bin\obj\pch.obj"
$pchRebuilt = $false

$needPch = (-not (Test-Path $pchBin)) -or (-not (Test-Path $pchObj))
if (-not $needPch) {
    $pchTime = (Get-Item $pchBin).LastWriteTimeUtc
    if ((Get-Item $pchHpp).LastWriteTimeUtc -gt $pchTime -or (Get-Item $pchCpp).LastWriteTimeUtc -gt $pchTime) {
        $needPch = $true
    }
}

if ($needPch) {
    Write-Host "Generating Precompiled Header (pch.pch)..." -ForegroundColor Yellow
    $pchArgs = @(
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
        "/DNOMINMAX",
        "/W4",
        "/guard:cf",
        "/Yc`"pch.hpp`"",
        "/Fp`"$pchBin`"",
        "/c",
        "/Fo`"$pchObj`"",
        "/Fd`"bin\obj\pch.pdb`"",
        "/I", "src"
    )
    if ($EnableAVX2) { $pchArgs += "/arch:AVX2" }
    $pchArgs += $pchCpp

    & cl.exe $pchArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "PCH generation failed with exit code $LASTEXITCODE"
    }
    $pchRebuilt = $true
}

# 7. Translation Units & Dependency Resolution
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
    "src\selection_controller.cpp",
    "src\ui_views.cpp",
    "src\tts_engine.cpp",
    "src\read_aloud_controller.cpp",
    "src\recent_files_controller.cpp",
    "src\pdf_decryptor.cpp",
    "src\context_menu_controller.cpp",
    "src\settings_controller.cpp"
)

# Recursive header dependency scanner with memoization
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
            if ($hName -ne "pch.hpp") {
                $hCandidate = Join-Path "src" $hName
                if (Test-Path $hCandidate) {
                    [void]$deps.Add((Resolve-Path $hCandidate).Path)
                    foreach ($nested in (Get-HeaderDeps $hCandidate ($depth + 1))) {
                        [void]$deps.Add($nested)
                    }
                }
            }
        }
    }
    $arr = @($deps)
    $headerDepCache[$filePath] = $arr
    return $arr
}

$staleSources = [System.Collections.Generic.List[string]]::new()

foreach ($src in $sources) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $objPath = "bin\obj\$baseName.obj"

    if ($pchRebuilt -or (-not (Test-Path $objPath))) {
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

# 8. Compile stale source files concurrently using /MP and PCH
if ($staleSources.Count -gt 0) {
    Write-Host "Compiling $($staleSources.Count) source file(s) with /MP and PCH..." -ForegroundColor Green
    $clArgs = @(
        "/nologo",
        "/MP",
        "/O2",
        "/MT",
        "/std:c++20",
        "/GL",
        "/Gy",
        "/Gw",
        "/EHsc",
        "/utf-8",
        "/permissive-",
        "/DNOMINMAX",
        "/W4",
        "/guard:cf",
        "/Yu`"pch.hpp`"",
        "/FI`"pch.hpp`"",
        "/Fp`"$pchBin`"",
        "/c",
        "/Fo`"bin\obj/`"",
        "/Fd`"bin\obj/`"",
        "/I", "src"
    )
    if ($EnableAVX2) { $clArgs += "/arch:AVX2" }
    $clArgs += $staleSources

    & cl.exe $clArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "All translation units up to date." -ForegroundColor DarkGray
}

# 9. Link target binary incrementally
$targetExe = "bin\LightPDF.exe"
$needLink = -not (Test-Path $targetExe)

$allObjFiles = @($pchObj) + ($sources | ForEach-Object {
    "bin\obj\$([System.IO.Path]::GetFileNameWithoutExtension($_)).obj"
})
$allLinkInputs = $allObjFiles + @($resFile)

if (-not $needLink) {
    $exeTime = (Get-Item $targetExe).LastWriteTimeUtc
    foreach ($linkIn in $allLinkInputs) {
        if ((Test-Path $linkIn) -and (Get-Item $linkIn).LastWriteTimeUtc -gt $exeTime) {
            $needLink = $true
            break
        }
    }
}

if ($needLink) {
    Write-Host "Linking $targetExe with /LTCG..." -ForegroundColor Yellow
    $linkArgs = @(
        "/nologo",
        "/LTCG",
        "/OPT:REF",
        "/OPT:ICF",
        "/GUARD:CF",
        "/CETCOMPAT",
        "/SUBSYSTEM:WINDOWS",
        "/MANIFEST:EMBED",
        "/MANIFESTINPUT:resources\app.manifest"
    ) + $allLinkInputs + @(
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
        "bcrypt.lib",
        "/OUT:$targetExe"
    )

    & link.exe $linkArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Link failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "Executable $targetExe is up to date." -ForegroundColor Green
}

$sw.Stop()
$elapsedSec = [math]::Round($sw.Elapsed.TotalSeconds, 2)

if (Test-Path $targetExe) {
    $bin = Get-Item $targetExe
    $sizeKb = [math]::Round($bin.Length / 1KB, 1)
    Write-Host "`n===================================================" -ForegroundColor Cyan
    Write-Host "  BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "  Executable:  $($bin.FullName)" -ForegroundColor White
    Write-Host "  Binary Size: $sizeKb KB ($($bin.Length) bytes)" -ForegroundColor Yellow
    Write-Host "  Build Time:  ${elapsedSec}s" -ForegroundColor Magenta
    Write-Host "===================================================" -ForegroundColor Cyan
}
