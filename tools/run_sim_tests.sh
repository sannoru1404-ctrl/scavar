#!/usr/bin/env bash
# Builds and runs the engine-agnostic simulation tests (plain CMake, ASan+UBSan) and the SIGKILL fault-injection test.
# Usage: tools/run_sim_tests.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${TMPDIR:-/tmp}/scavar-sim-build}"
cmake -S "$ROOT/Tests/Sim" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD"
"$BUILD/scavar_sim_tests"
if [[ -x "$BUILD/scavar_fault_injection" ]]; then
  "$BUILD/scavar_fault_injection"
fi
