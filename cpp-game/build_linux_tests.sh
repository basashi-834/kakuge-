#!/usr/bin/env bash
# Native (Linux) build of the engine unit tests. The engine/ headers have
# zero Windows/WinAPI dependencies, so they compile and run directly here
# with the host g++ - this is the fast inner-loop check used while
# developing, independent of the mingw-w64 cross-compile used for the
# actual Windows .exe (see build_windows.sh).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Ithird_party -o build/EngineTests tests/EngineTests.cpp
echo "Built build/EngineTests"
./build/EngineTests
