@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

rem Normalize command line switches for PowerShell (e.g. /rebuild -> -Rebuild)
set "ARGS="
for %%A in (%*) do (
    set "ARG=%%A"
    if /i "!ARG!"=="/rebuild" set "ARG=-Rebuild"
    if /i "!ARG!"=="/clean"   set "ARG=-Clean"
    if /i "!ARG!"=="/avx2"    set "ARG=-EnableAVX2"
    set "ARGS=!ARGS! !ARG!"
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" !ARGS!
exit /b %errorlevel%
