#!/usr/bin/env bash
# Build and run the host-independent core tests (Linux/macOS/Windows with CMake + a C++17 compiler).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --parallel >/dev/null
./build/fe_core_tests "$@"
./build/fe_gpu_emulation_tests
