$ErrorActionPreference = "Stop"
$exe = "$PSScriptRoot\..\bin\LightPDF.exe"

if (-not (Test-Path $exe)) {
    Write-Error "Executable not found at $exe. Please build the project first."
}

Write-Host "Testing Shell Associations (/register and /unregister)..." -ForegroundColor Cyan

# 1. Register
$pReg = Start-Process -FilePath $exe -ArgumentList "/register /silent" -Wait -PassThru
$pdfProp = Get-ItemProperty -Path "HKCU:\Software\Classes\.pdf\OpenWithProgids" -ErrorAction SilentlyContinue
$progCmd = Test-Path "HKCU:\Software\Classes\LightPDF.Document\shell\open\command"
$appKey = Test-Path "HKCU:\Software\Classes\Applications\LightPDF.exe\shell\open\command"
$appProp = Get-ItemProperty -Path "HKCU:\Software\Classes\Applications\LightPDF.exe" -ErrorAction SilentlyContinue
$capsExist = Test-Path "HKCU:\Software\LightPDF\Capabilities\FileAssociations"
$regApp = Get-ItemProperty -Path "HKCU:\Software\RegisteredApplications" -ErrorAction SilentlyContinue

$isReg = ($null -ne $pdfProp) -and `
         ($pdfProp.PSObject.Properties.Name -contains "LightPDF.Document") -and `
         $progCmd -and `
         $appKey -and `
         ($null -ne $appProp) -and ($appProp.FriendlyAppName -eq "LightPDF") -and `
         $capsExist -and `
         ($null -ne $regApp) -and ($null -ne $regApp.LightPDF)

$c1 = if ($isReg) { "Green" } else { "Red" }
Write-Host "  Registered .pdf shell associations: $isReg" -ForegroundColor $c1

# 2. Unregister
$pUnreg = Start-Process -FilePath $exe -ArgumentList "/unregister /silent" -Wait -PassThru
$unregProp = Get-ItemProperty -Path "HKCU:\Software\Classes\.pdf\OpenWithProgids" -ErrorAction SilentlyContinue
$regApp2 = Get-ItemProperty -Path "HKCU:\Software\RegisteredApplications" -ErrorAction SilentlyContinue

$isUnreg = (($null -eq $unregProp) -or (-not ($unregProp.PSObject.Properties.Name -contains "LightPDF.Document"))) -and `
           -not (Test-Path "HKCU:\Software\Classes\LightPDF.Document") -and `
           -not (Test-Path "HKCU:\Software\Classes\Applications\LightPDF.exe") -and `
           -not (Test-Path "HKCU:\Software\LightPDF\Capabilities") -and `
           (($null -eq $regApp2) -or ($null -eq $regApp2.LightPDF))

$c2 = if ($isUnreg) { "Green" } else { "Red" }
Write-Host "  Unregistered .pdf shell associations cleanly: $isUnreg" -ForegroundColor $c2

if ($isReg -and $isUnreg) {
    Write-Host "Shell Association test PASSED!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Shell Association test FAILED!" -ForegroundColor Red
    exit 1
}
