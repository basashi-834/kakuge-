#!/usr/bin/env bash
# Builds the Windows .exe with MinGW-w64, statically linked so it has zero
# runtime DLL dependencies beyond what every Windows 10/11 install already
# ships (no separate "install the C++ runtime" step for the user - this was
# the whole point of avoiding Godot/Python/Node/Electron in the first
# place).
#
# Works both ways:
#   - Cross-compiling from Linux (needs the mingw-w64 package, compiler
#     named x86_64-w64-mingw32-g++)
#   - Natively on Windows in an MSYS2 "MINGW64" shell, where the same
#     compiler is simply called g++
# See build_windows_msvc.bat for the Visual Studio route.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    CXX=x86_64-w64-mingw32-g++
elif command -v g++ >/dev/null 2>&1 && g++ -dumpmachine 2>/dev/null | grep -q mingw; then
    CXX=g++   # MSYS2 MINGW64 shell
else
    echo "ERROR: no MinGW-w64 C++ compiler found." >&2
    echo "  Linux: install the mingw-w64 package (x86_64-w64-mingw32-g++)." >&2
    echo "  Windows: install MSYS2, then in the 'MSYS2 MINGW64' shell run:" >&2
    echo "      pacman -S --needed mingw-w64-x86_64-gcc" >&2
    exit 1
fi

SRC="platform/WinMain.cpp platform/App.cpp platform/Screens.cpp platform/Editor.cpp platform/Draw.cpp platform/Sprites.cpp platform/HudSkin.cpp"
# -DNOMINMAX: windows.h otherwise defines min/max as macros, which breaks
# the std::min/std::max/std::clamp calls throughout platform/. MinGW's
# headers happen to tolerate it, MSVC's don't - defining it on the command
# line keeps both toolchains building from the same source.
FLAGS="-std=c++17 -O2 -municode -DNOMINMAX -Ithird_party -Wall -Wextra"
LIBS="-mwindows -lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -lcomctl32 -lcomdlg32 -lwinmm -static -static-libgcc -static-libstdc++"

echo "Compiling with $CXX: $SRC"
$CXX $FLAGS $SRC -o build/Kakuge.exe $LIBS
echo "Built build/Kakuge.exe"
command -v file >/dev/null 2>&1 && file build/Kakuge.exe || true
