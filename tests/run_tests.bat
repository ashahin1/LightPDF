@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0\.."

rem Activate portable MSVC environment if cl.exe is not in PATH
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "tools\msvc\setup_x64.bat" (
        call "tools\msvc\setup_x64.bat"
    ) else (
        echo [ERROR] cl.exe not found and tools\msvc\setup_x64.bat missing!
        exit /b 1
    )
)

echo Compiling LightPDF Unified Test Suite with /W4...
if not exist "bin\obj" mkdir "bin\obj"
cl.exe /nologo /O2 /MT /std:c++20 /EHsc /utf-8 /DNOMINMAX /W4 /Fo"bin\obj\\" /Fd"bin\obj\\" /I src /I tests ^
    tests\unified_tests.cpp src\pdf_document.cpp src\dictionary_engine.cpp ^
    src\tab_controller.cpp src\search_controller.cpp src\selection_controller.cpp ^
    src\pdf_parser.cpp src\pdf_searchable_writer.cpp src\pdf_search.cpp src\ui_views.cpp ^
    /link /OUT:tests\unified_tests.exe ^
    d3d11.lib d2d1.lib dxgi.lib dwrite.lib windows.data.pdf.lib windowsapp.lib ^
    user32.lib gdi32.lib shell32.lib ole32.lib shcore.lib windowscodecs.lib

if %errorlevel% equ 0 (
    echo.
    echo Running Unified Test Suite...
    tests\unified_tests.exe
    exit /b %errorlevel%
) else (
    echo [ERROR] Test compilation failed!
    exit /b 1
)
