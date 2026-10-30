param(
    [string]$BinaryPath = "bin\LightPDF.exe",
    [string]$DistDir = "dist",
    [string]$Subject = "CN=Ahmed Amer Shahin, O=EtherHome"
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot\..

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  LightPDF Code Signing & Trust Package Generator" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

# 1. Locate or create Code-Signing Certificate
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue | Where-Object { $_.Subject -like "*$Subject*" } | Select-Object -First 1

if (-not $cert) {
    Write-Host "Creating new Code Signing certificate for '$Subject'..." -ForegroundColor Yellow
    $cert = New-SelfSignedCertificate `
        -Type CodeSigning `
        -Subject $Subject `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -NotAfter (Get-Date).AddYears(5)
    Write-Host "Certificate created with Thumbprint: $($cert.Thumbprint)" -ForegroundColor Green
} else {
    Write-Host "Found Code Signing certificate: $($cert.Thumbprint)" -ForegroundColor Green
}

# 2. Add to CurrentUser\TrustedPublisher store (silent, non-interactive)
try {
    $pubStore = New-Object System.Security.Cryptography.X509Certificates.X509Store "TrustedPublisher", "CurrentUser"
    $pubStore.Open("ReadWrite")
    $pubStore.Add($cert)
    $pubStore.Close()
} catch {
    Write-Warning "Could not add to TrustedPublisher store: $_"
}

# 3. Sign bin\LightPDF.exe
if (Test-Path $BinaryPath) {
    Write-Host "Signing '$BinaryPath' with Authenticode (SHA256)..." -ForegroundColor Yellow
    $sig = Set-AuthenticodeSignature -FilePath $BinaryPath -Certificate $cert -HashAlgorithm "SHA256"
    Write-Host "Signature Status: $($sig.Status)" -ForegroundColor Green
    if ($sig.StatusMessage) {
        Write-Host "Status Message: $($sig.StatusMessage)" -ForegroundColor Gray
    }
} else {
    Write-Error "Binary not found at '$BinaryPath'!"
}

# 4. Prepare Colleague Distribution Bundle in dist/
if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

$certFile = Join-Path $DistDir "LightPDF_Publisher.cer"
Export-Certificate -Cert $cert -FilePath $certFile -Force | Out-Null
Write-Host "Exported public certificate to: $certFile" -ForegroundColor Green

# Copy binary and dictionary database to dist/
Copy-Item -Path $BinaryPath -Destination (Join-Path $DistDir "LightPDF.exe") -Force
if (Test-Path "bin\dict") {
    Copy-Item -Path "bin\dict" -Destination $DistDir -Recurse -Force
}

# Create colleague one-click installer batch script
$installBat = @"
@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================
echo   LightPDF - Colleague Trust Setup
echo   Publisher: Ahmed Amer Shahin (EtherHome)
echo ===================================================
echo.
echo Installing publisher certificate...
echo (If Windows shows a Security Warning dialog, click 'Yes' to trust)
echo.

certutil.exe -user -addstore TrustedPublisher "%~dp0LightPDF_Publisher.cer" >nul 2>&1
certutil.exe -user -addstore Root "%~dp0LightPDF_Publisher.cer"

echo.
echo Removing download flags from LightPDF.exe...
powershell -NoProfile -Command "Unblock-File -Path '%~dp0LightPDF.exe' -ErrorAction SilentlyContinue"

echo.
echo ===================================================
echo [SUCCESS] Setup complete!
echo You can now launch LightPDF.exe without warnings.
echo ===================================================
echo.
pause
"@

Set-Content -Path (Join-Path $DistDir "install_certificate.bat") -Value $installBat -Encoding Ascii

# Create README for colleagues
$colleagueReadme = @"
LightPDF - Instructions for Colleagues
======================================
Publisher: Ahmed Amer Shahin (EtherHome)
Version:   2.4.0.0

FIRST-TIME SETUP (Takes 5 seconds):
1. Right-click 'install_certificate.bat' and run it (or simply double-click).
   - This registers Ahmed Amer Shahin (EtherHome) into your Windows Trusted Publishers.
   - It also removes any Internet download flag ('Mark of the Web') on LightPDF.exe.

2. Double-click 'LightPDF.exe' to launch!
   - Windows SmartScreen and Defender will recognize the digital signature and start immediately without blocking.
"@

Set-Content -Path (Join-Path $DistDir "README_COLLEAGUES.txt") -Value $colleagueReadme -Encoding Utf8

Write-Host "Distribution bundle ready in '$DistDir\'!" -ForegroundColor Cyan
Write-Host "  -> $DistDir\LightPDF.exe (Signed)"
Write-Host "  -> $DistDir\LightPDF_Publisher.cer"
Write-Host "  -> $DistDir\install_certificate.bat"
Write-Host "  -> $DistDir\README_COLLEAGUES.txt"
