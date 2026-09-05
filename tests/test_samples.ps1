$files = @(
    "sample_document.pdf",
    "second_sample.pdf",
    "forensic_test.pdf"
)

foreach ($f in $files) {
    $path = "C:\AntiGravity_Projects\NovaPDF\$f"
    if (Test-Path $path) {
        $p = Start-Process -FilePath "bin\LightPDF.exe" -ArgumentList "`"$path`"" -PassThru
        Start-Sleep -Milliseconds 300
        $p.Refresh()
        $ws = [math]::Round($p.WorkingSet64 / 1MB, 2)
        $priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
        Write-Host "File: $f" -ForegroundColor Cyan
        Write-Host "  Process ID:      $($p.Id)"
        Write-Host "  Responding:      $($p.Responding)" -ForegroundColor Green
        Write-Host "  Working Set RAM: $ws MB"
        Write-Host "  Private Commit:  $priv MB`n"
        $p.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 100
        if (-not $p.HasExited) { $p.Kill() }
    }
}
