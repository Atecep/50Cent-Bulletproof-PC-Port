#!/usr/bin/env bash
# Native Linux build; assets and the existing XDK config are inputs only.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
preset="${1:-linux-release}"
case "$preset" in
    linux-debug|linux-release) ;;
    *) echo "Usage: $0 [linux-debug|linux-release]" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
    echo "Usage: $0 [linux-debug|linux-release]" >&2; exit 2
fi
for tool in clang clang++ cmake ninja ld.lld git; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
if [[ ! -f assets/default.xbe ]]; then
    echo 'Extract default.xbe from your original disc into assets/ for code generation.' >&2
    echo 'The original ISO can remain in assets/ for runtime disc access.' >&2
    exit 1
fi
if [[ ! -f config/game.toml ]]; then
    echo 'Missing config/game.toml; restore the verified Bulletproof XDK 5849 config.' >&2
    exit 1
fi
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
printf '\nBuilt: %s/out/build/%s/Bulletproof\n' "$PWD" "$preset"
