$lightpdf = "C:\AntiGravity_Projects\LightPDF\bin\LightPDF.exe"
$sumatra = "C:\AntiGravity_Projects\LightPDF\scratch\Sumatra\SumatraPDF-3.5.2-64.exe"
$pxc_exe = "C:\Program Files\PDF-XChange\PDF Editor\PXCEditor.exe"
$pxc_dir = "C:\Program Files\PDF-XChange\PDF Editor"
$edge_exe = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
$edge_dir = "C:\Program Files (x86)\Microsoft\Edge\Application"

$lightpdf_size = [math]::Round((Get-Item $lightpdf).Length / 1KB, 1)
$sumatra_size = [math]::Round((Get-Item $sumatra).Length / 1MB, 2)
$pxc_exe_size = [math]::Round((Get-Item $pxc_exe).Length / 1MB, 2)
$pxc_dir_size = [math]::Round(((Get-ChildItem $pxc_dir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum) / 1MB, 1)
$edge_exe_size = [math]::Round((Get-Item $edge_exe).Length / 1MB, 2)
$edge_dir_size = [math]::Round(((Get-ChildItem $edge_dir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum) / 1MB, 1)

$results = @(
    [PSCustomObject]@{
        Application = "LightPDF"
        "Exe Size" = "$lightpdf_size KB"
        "Install Footprint" = "$lightpdf_size KB"
        "Architecture" = "Native C++20 / Direct2D"
        "Dependencies" = "Zero external DLLs / OS Native"
    },
    [PSCustomObject]@{
        Application = "SumatraPDF"
        "Exe Size" = "$sumatra_size MB"
        "Install Footprint" = "$sumatra_size MB"
        "Architecture" = "Native C++ / MuPDF"
        "Dependencies" = "Single portable binary"
    },
    [PSCustomObject]@{
        Application = "PDF-XChange Editor"
        "Exe Size" = "$pxc_exe_size MB"
        "Install Footprint" = "$pxc_dir_size MB"
        "Architecture" = "Native C++ (Full Editor)"
        "Dependencies" = "Dozens of DLLs, plugins, OCR"
    },
    [PSCustomObject]@{
        Application = "Microsoft Edge"
        "Exe Size" = "$edge_exe_size MB"
        "Install Footprint" = "$edge_dir_size MB"
        "Architecture" = "Chromium / PDFium / Blink"
        "Dependencies" = "Web browser engine, multi-process sandbox"
    }
)

$results | Format-Table -AutoSize
