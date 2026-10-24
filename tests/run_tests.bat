@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0\.."

rem Activate MSVC environment if cl.exe is not in PATH
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "tools\msvc\setup_x64.bat" (
        call "tools\msvc\setup_x64.bat"
    ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
                call "%%i\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] cl.exe not found and neither portable MSVC nor Visual Studio was detected!
    exit /b 1
)

echo Compiling LightPDF Unified Test Suite with /W4...
if not exist "bin\obj_test" mkdir "bin\obj_test"
cl.exe /nologo /MP /O2 /MT /std:c++20 /EHsc /utf-8 /DNOMINMAX /W4 /Fo"bin\obj_test\\" /Fd"bin\obj_test\\" /I src /I tests ^
    tests\unified_tests.cpp src\pdf_document.cpp src\dictionary_engine.cpp ^
    src\tab_controller.cpp src\search_controller.cpp src\selection_controller.cpp ^
    src\pdf_parser.cpp src\pdf_searchable_writer.cpp src\pdf_search.cpp src\ui_views.cpp ^
    src\tts_engine.cpp src\read_aloud_controller.cpp src\recent_files_controller.cpp src\pdf_decryptor.cpp src\context_menu_controller.cpp ^
    /link /OUT:tests\unified_tests.exe ^
    d3d11.lib d2d1.lib dxgi.lib dwrite.lib windows.data.pdf.lib windowsapp.lib ^
    user32.lib gdi32.lib shell32.lib ole32.lib shcore.lib windowscodecs.lib advapi32.lib bcrypt.lib

if %errorlevel% equ 0 (
    echo.
    echo Running Unified Test Suite...
    tests\unified_tests.exe
    exit /b %errorlevel%
) else (
    echo [ERROR] Test compilation failed!
    exit /b 1
)
