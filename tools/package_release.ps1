Set-Location -Path $PSScriptRoot\..

$tag = "v2.4.1"
$zipName = "dist\LightPDF-$tag-windows-x64.zip"
$tempDir = "bin\release_pkg"

if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }
New-Item -ItemType Directory -Path $tempDir | Out-Null

Copy-Item "dist\LightPDF.exe" $tempDir
Copy-Item "dist\install_certificate.bat" $tempDir
Copy-Item "dist\LightPDF_Publisher.cer" $tempDir
Copy-Item "dist\README_COLLEAGUES.txt" $tempDir
if (Test-Path "dict") { Copy-Item "dict" $tempDir -Recurse }
Copy-Item "README.md" $tempDir
Copy-Item "LICENSE" $tempDir

Compress-Archive -Path "$tempDir\*" -DestinationPath $zipName -Force
Remove-Item $tempDir -Recurse -Force

$hashExe = (Get-FileHash "dist\LightPDF.exe" -Algorithm SHA256).Hash
$hashZip = (Get-FileHash $zipName -Algorithm SHA256).Hash

$checksums = @(
    "LightPDF $tag SHA-256 Checksums:",
    "$hashExe  LightPDF.exe",
    "$hashZip  LightPDF-$tag-windows-x64.zip"
)
$checksums | Set-Content -Path "dist\checksums.txt" -Encoding utf8
$checksums | ForEach-Object { Write-Host $_ }
