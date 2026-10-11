@echo off
setlocal
cd /d "%~dp0\.."

echo ===================================================
echo   Signing LightPDF Release Binary
echo ===================================================

powershell -NoProfile -ExecutionPolicy Bypass -File tools\sign.ps1
exit /b %ERRORLEVEL%
