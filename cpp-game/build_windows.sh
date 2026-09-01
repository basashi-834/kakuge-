#!/usr/bin/env bash
# Cross-compiles the Windows .exe with mingw-w64, statically linked so it
# has zero runtime DLL dependencies beyond what every Windows 10/11
# install already ships (no separate "install the C++ runtime" step for
# the user - this was the whole point of avoiding Godot/Python/Node/
# Electron in the first place).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CXX=x86_64-w64-mingw32-g++
SRC="platform/WinMain.cpp platform/App.cpp platform/Screens.cpp platform/Editor.cpp platform/Draw.cpp"
FLAGS="-std=c++17 -O2 -municode -Ithird_party -Wall -Wextra"
LIBS="-mwindows -lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -lcomctl32 -lcomdlg32 -lwinmm -static -static-libgcc -static-libstdc++"

echo "Compiling: $SRC"
$CXX $FLAGS $SRC -o build/Kakuge.exe $LIBS
echo "Built build/Kakuge.exe"
file build/Kakuge.exe
