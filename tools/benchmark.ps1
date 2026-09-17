# =====================================================================
# Comprehensive PDF Viewer Benchmark Suite
# Compares: LightPDF vs. SumatraPDF vs. PDF-XChange Editor vs. MS Edge
# =====================================================================

$ErrorActionPreference = "SilentlyContinue"

$LightPDFPath = "C:\AntiGravity_Projects\LightPDF\bin\LightPDF.exe"
$SumatraPath  = "C:\AntiGravity_Projects\LightPDF\scratch\Sumatra\SumatraPDF-3.5.2-64.exe"
$PXCPath      = "C:\Program Files\PDF-XChange\PDF Editor\PXCEditor.exe"
$EdgePath     = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"

$BookPdf = "C:\Users\eng_a\Desktop\Book.pdf"
$MainPdf = "C:\AntiGravity_Projects\AI_IoT_Emergency_Survey\main.pdf"
$ArabicItem = Get-ChildItem -Path "D:\ZU\Teaching\BPC - Spring 2026" -Recurse -Filter "1-4-2026.pdf" -ErrorAction SilentlyContinue | Select-Object -First 1
$ArabicPdf = if ($ArabicItem) { $ArabicItem.FullName } else { "" }

$Scenarios = @(
    @{ Label = "Empty UI (Clean Launch)"; Path = ""; Pages = 0 },
    @{ Label = "Book.pdf (926 Pages, 18.3 MB)"; Path = $BookPdf; Pages = 926 },
    @{ Label = "main.pdf (26 Pages, Survey)"; Path = $MainPdf; Pages = 26 }
)

if (-not [string]::IsNullOrEmpty($ArabicPdf)) {
    $Scenarios += @{ Label = "1-4-2026.pdf (5 Pages, Arabic)"; Path = $ArabicPdf; Pages = 5 }
}

function Measure-ViewerOnce {
    param(
        [string]$ViewerName,
        [string]$DocPath
    )

    $readyMs = 0
    $wsMB = 0
    $privMB = 0

    if ($ViewerName -eq "LightPDF") {
        $p = if ([string]::IsNullOrEmpty($DocPath)) {
            [System.Diagnostics.Process]::Start($LightPDFPath)
        } else {
            [System.Diagnostics.Process]::Start($LightPDFPath, "`"$DocPath`"")
        }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($p.MainWindowHandle -eq 0 -and $sw.ElapsedMilliseconds -lt 6000) {
            Start-Sleep -Milliseconds 15
            $p.Refresh()
        }
        $sw.Stop()
        $readyMs = $sw.ElapsedMilliseconds
        Start-Sleep -Milliseconds 1200
        $p.Refresh()
        $wsMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $privMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        $p.Kill()
        Start-Sleep -Milliseconds 300
    }
    elseif ($ViewerName -eq "SumatraPDF") {
        $p = if ([string]::IsNullOrEmpty($DocPath)) {
            [System.Diagnostics.Process]::Start($SumatraPath)
        } else {
            [System.Diagnostics.Process]::Start($SumatraPath, "`"$DocPath`"")
        }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($p.MainWindowHandle -eq 0 -and $sw.ElapsedMilliseconds -lt 6000) {
            Start-Sleep -Milliseconds 15
            $p.Refresh()
        }
        $sw.Stop()
        $readyMs = $sw.ElapsedMilliseconds
        Start-Sleep -Milliseconds 1200
        $p.Refresh()
        $wsMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $privMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        $p.Kill()
        Start-Sleep -Milliseconds 300
    }
    elseif ($ViewerName -eq "PDF-XChange") {
        $p = if ([string]::IsNullOrEmpty($DocPath)) {
            [System.Diagnostics.Process]::Start($PXCPath)
        } else {
            [System.Diagnostics.Process]::Start($PXCPath, "`"$DocPath`"")
        }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($p.MainWindowHandle -eq 0 -and $sw.ElapsedMilliseconds -lt 6000) {
            Start-Sleep -Milliseconds 15
            $p.Refresh()
        }
        $sw.Stop()
        $readyMs = $sw.ElapsedMilliseconds
        Start-Sleep -Milliseconds 1200
        $p.Refresh()
        $wsMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $privMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        $p.Kill()
        Start-Sleep -Milliseconds 300
    }
    elseif ($ViewerName -eq "MS Edge") {
        Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
        $arg = if ([string]::IsNullOrEmpty($DocPath)) {
            "--new-window about:blank"
        } else {
            "--new-window `"file:///$($DocPath -replace '\\', '/')`""
        }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $p = [System.Diagnostics.Process]::Start($EdgePath, $arg)
        while ($p.MainWindowHandle -eq 0 -and $sw.ElapsedMilliseconds -lt 6000) {
            Start-Sleep -Milliseconds 15
            $p.Refresh()
        }
        $sw.Stop()
        $readyMs = $sw.ElapsedMilliseconds
        Start-Sleep -Milliseconds 1500
        $edgeProcs = Get-Process msedge -ErrorAction SilentlyContinue
        if ($edgeProcs) {
            $wsMB = [math]::Round(($edgeProcs | Measure-Object -Property WorkingSet64 -Sum).Sum / 1MB, 1)
            $privMB = [math]::Round(($edgeProcs | Measure-Object -Property PrivateMemorySize64 -Sum).Sum / 1MB, 1)
        }
        $edgeProcs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
    }

    return @{
        ReadyMs = $readyMs
        WorkingSetMB = $wsMB
        PrivateMB = $privMB
    }
}

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "  RUNNING MULTI-VIEWER BENCHMARK SUITE (3 TRIALS PER SCENARIO)" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$Viewers = @("LightPDF", "SumatraPDF", "PDF-XChange", "MS Edge")
$Summary = @()

foreach ($sc in $Scenarios) {
    Write-Host "`n>>> Scenario: $($sc.Label)" -ForegroundColor Yellow

    foreach ($v in $Viewers) {
        Write-Host "  Testing $v... " -NoNewline

        # Warmup
        $null = Measure-ViewerOnce -ViewerName $v -DocPath $sc.Path

        # 3 Measured runs
        $r1 = Measure-ViewerOnce -ViewerName $v -DocPath $sc.Path
        $r2 = Measure-ViewerOnce -ViewerName $v -DocPath $sc.Path
        $r3 = Measure-ViewerOnce -ViewerName $v -DocPath $sc.Path

        $avgReady = [math]::Round(($r1.ReadyMs + $r2.ReadyMs + $r3.ReadyMs) / 3.0, 1)
        $avgWs = [math]::Round(($r1.WorkingSetMB + $r2.WorkingSetMB + $r3.WorkingSetMB) / 3.0, 1)
        $avgPriv = [math]::Round(($r1.PrivateMB + $r2.PrivateMB + $r3.PrivateMB) / 3.0, 1)

        Write-Host "Ready: $avgReady ms | RAM (WS): $avgWs MB | Private: $avgPriv MB" -ForegroundColor Green

        $Summary += [PSCustomObject]@{
            Scenario     = $sc.Label
            Viewer       = $v
            ReadyTimeMs  = $avgReady
            WorkingSetMB = $avgWs
            PrivateMB    = $avgPriv
        }
    }
}

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "  BENCHMARK RESULTS MATRIX" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$Summary | Format-Table -AutoSize

# Export JSON
$Summary | ConvertTo-Json -Depth 3 | Out-File -FilePath "C:\AntiGravity_Projects\LightPDF\scratch\benchmark_results.json" -Encoding utf8
Write-Host "`nResults exported to scratch\benchmark_results.json" -ForegroundColor Cyan
