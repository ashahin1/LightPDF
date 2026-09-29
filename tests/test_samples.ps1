$pdfDirs = @("tests\fixtures", "tests\PDF")
$files = Get-ChildItem -Path $pdfDirs -Filter "*.pdf" -ErrorAction SilentlyContinue

if ($files.Count -eq 0) {
    Write-Host "No sample PDF files found in tests\fixtures or tests\PDF." -ForegroundColor Yellow
    exit 0
}

foreach ($file in $files) {
    $path = $file.FullName
    $f = $file.Name
    if (Test-Path $path) {
        $p = Start-Process -FilePath "bin\LightPDF.exe" -ArgumentList "`"$path`"" -PassThru
        Start-Sleep -Milliseconds 400
        $p.Refresh()
        $ws = [math]::Round($p.WorkingSet64 / 1MB, 2)
        $priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
        Write-Host "File: $f" -ForegroundColor Cyan
        Write-Host "  Process ID:      $($p.Id)"
        Write-Host "  Responding:      $($p.Responding)" -ForegroundColor Green
        Write-Host "  Working Set RAM: $ws MB"
        Write-Host "  Private Commit:  $priv MB`n"
        $p.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 150
        if (-not $p.HasExited) { $p.Kill() }
    }
}
