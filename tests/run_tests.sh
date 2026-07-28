#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/tests/build"
OUTPUT="$ROOT/tests/output"
mkdir -p "$BUILD" "$OUTPUT"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/main/include" \
  "$ROOT/main/src/body_manager.cpp" \
  "$ROOT/main/src/pairwise_force_solver.cpp" \
  "$ROOT/main/src/physics_engine.cpp" \
  "$ROOT/main/src/presets.cpp" \
  "$ROOT/tests/physics_engine_test.cpp" \
  -o "$BUILD/physics_engine_test"

"$BUILD/physics_engine_test" "$OUTPUT/figure8.csv"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/main/include" \
  "$ROOT/main/src/body_colors.cpp" \
  "$ROOT/main/src/body_manager.cpp" \
  "$ROOT/main/src/universe_codec.cpp" \
  "$ROOT/main/src/universe_qr.cpp" \
  "$ROOT/tests/universe_codec_test.cpp" \
  -o "$BUILD/universe_codec_test"

"$BUILD/universe_codec_test"
echo "trajectory CSV: $OUTPUT/figure8.csv"
