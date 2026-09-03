@echo off
REM ---------------------------------------------------------------------
REM Builds Kakuge.exe on Windows with the Microsoft C++ compiler (MSVC).
REM
REM Prerequisite (free): "Visual Studio Build Tools" with the
REM "Desktop development with C++" workload -
REM   https://visualstudio.microsoft.com/downloads/  ->  Build Tools
REM
REM How to run: open "x64 Native Tools Command Prompt for VS 20xx" from the
REM Start menu (that shell is what puts cl.exe on PATH), cd to this folder,
REM and run:   build_windows_msvc.bat
REM
REM The alternative toolchain is MinGW-w64 via MSYS2 - see build_windows.sh,
REM which runs unchanged in an "MSYS2 MINGW64" shell.
REM ---------------------------------------------------------------------
setlocal
cd /d "%~dp0"
if not exist build mkdir build

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo.
    echo ERROR: cl.exe not found on PATH.
    echo Open the "x64 Native Tools Command Prompt for VS" and run this script from there.
    echo Install it from https://visualstudio.microsoft.com/downloads/ ^(Build Tools -^> Desktop development with C++^).
    echo.
    exit /b 1
)

set SRC=platform\WinMain.cpp platform\App.cpp platform\Screens.cpp platform\Editor.cpp platform\Draw.cpp platform\Sprites.cpp platform\HudSkin.cpp

REM /utf-8    : the sources contain Japanese string literals; without this
REM             MSVC reads them in the system codepage and mangles them.
REM /DNOMINMAX: windows.h would otherwise define min/max as macros and break
REM             every std::min / std::max / std::clamp call.
REM /MT       : static CRT, so the .exe needs no VC++ redistributable -
REM             matching what the MinGW build produces.
set FLAGS=/nologo /std:c++17 /EHsc /O2 /MT /utf-8 /permissive- /DNOMINMAX /DUNICODE /D_UNICODE /I third_party /Fobuild\
set LIBS=gdiplus.lib gdi32.lib user32.lib shell32.lib ole32.lib comctl32.lib comdlg32.lib winmm.lib

echo Compiling with MSVC: %SRC%
cl %FLAGS% %SRC% /Febuild\Kakuge.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup %LIBS%
if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo Built build\Kakuge.exe
echo Copy the Data\ folder next to it ^(it is already there in this folder^) and run it.
endlocal
