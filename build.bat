@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ===================================================
echo   Building LightPDF (Ultra-Fast Windows PDF Viewer)
echo ===================================================

if not exist "bin" mkdir bin

rem Activate portable MSVC environment if cl.exe is not in PATH
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "tools\msvc\setup_x64.bat" (
        echo Activating portable MSVC...
        call "tools\msvc\setup_x64.bat"
    ) else (
        echo [ERROR] cl.exe not found and tools\msvc\setup_x64.bat does not exist!
        exit /b 1
    )
)

echo Compiling Windows resource script (app.rc)...
rc.exe /nologo /i resources /fo resources\app.res resources\app.rc

echo Compiling optimized C++20 release binary...
cl.exe /nologo /O2 /MT /std:c++20 /GL /Gy /Gw /EHsc /utf-8 /permissive- /DNOMINMAX ^
    src\main.cpp src\app_window.cpp src\d2d_renderer.cpp src\pdf_document.cpp ^
    resources\app.res ^
    /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
    /MANIFEST:EMBED /MANIFESTINPUT:resources\app.manifest ^
    d3d11.lib d2d1.lib dxgi.lib dwrite.lib windows.data.pdf.lib windowsapp.lib ^
    user32.lib gdi32.lib shell32.lib ole32.lib shcore.lib comdlg32.lib ^
    /OUT:bin\LightPDF.exe

if %errorlevel% equ 0 (
    echo.
    echo ===================================================
    echo   BUILD SUCCESSFUL!
    echo   Executable: bin\LightPDF.exe
    echo ===================================================
    rem Clean up intermediate object files
    del *.obj >nul 2>nul
) else (
    echo.
    echo [ERROR] Build failed!
    exit /b %errorlevel%
)
