@echo off
setlocal

echo [LightPDF] Checking for Doxygen...
where doxygen >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [LightPDF] Running Doxygen...
    doxygen Doxyfile
    if %ERRORLEVEL% equ 0 (
        echo [LightPDF] Documentation generated successfully in docs\html\index.html
    ) else (
        echo [LightPDF] Doxygen encountered an error.
    )
    exit /b %ERRORLEVEL%
)

if exist "%ProgramFiles%\doxygen\bin\doxygen.exe" (
    echo [LightPDF] Running Doxygen from Program Files...
    "%ProgramFiles%\doxygen\bin\doxygen.exe" Doxyfile
    if %ERRORLEVEL% equ 0 (
        echo [LightPDF] Documentation generated successfully in docs\html\index.html
    ) else (
        echo [LightPDF] Doxygen encountered an error.
    )
    exit /b %ERRORLEVEL%
)

echo [LightPDF] ERROR: doxygen executable not found in PATH or standard install paths.
echo Please install Doxygen using:
echo    winget install DimitrivanHeesch.Doxygen
echo or download from: https://www.doxygen.nl/download.html
exit /b 1
