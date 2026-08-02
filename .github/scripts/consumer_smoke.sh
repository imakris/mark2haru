#!/usr/bin/env bash
#
# Installs the build in "$1" (default: build) to a scratch prefix, then
# configures, builds and runs tests/consumer_smoke against it. The three CI
# workflows all call this, so the downstream-consumer check is defined once
# instead of once per platform. Run from the repository root.
set -euo pipefail

build_dir="${1:-build}"
build_abs="$(cd "$build_dir" && pwd)"
install_prefix="$build_abs/_install"
consumer_build="$build_abs/_consumer"

cmake --install "$build_dir" --prefix "$install_prefix" --config Release

cmake -S tests/consumer_smoke -B "$consumer_build" \
    -DCMAKE_PREFIX_PATH="$install_prefix" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$consumer_build" --config Release

smoke="$consumer_build/bin/consumer_smoke"
if [ ! -x "$smoke" ]; then
    smoke="$smoke.exe"
fi
"$smoke" "$install_prefix"
