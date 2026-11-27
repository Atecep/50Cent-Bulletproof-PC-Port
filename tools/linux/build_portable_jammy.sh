#!/usr/bin/env bash
set -euo pipefail

source_dir=$(cd -- "$(dirname -- "$0")/../.." && pwd)
title=${1:?Usage: build_portable_jammy.sh /absolute/title.toml /absolute/output [CMake options]}
output=${2:?Usage: build_portable_jammy.sh /absolute/title.toml /absolute/output [CMake options]}
shift 2

if [[ $(getconf GNU_LIBC_VERSION) != "glibc 2.35" || $(uname -m) != x86_64 ]]; then
    echo "Run this script inside the Jammy amd64 build image." >&2
    exit 1
fi
mkdir -p "$output"
cmake --preset linux-release -S "$source_dir" -B "$output/build" "$@" \
    -DBULLETPROOF_PORTABLE_ONLY=ON -DBUILD_TESTING=OFF -DXBE_SHADER_BACKEND=glslang \
    -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
    -DBULLETPROOF_PORTABLE_CONFIG="$title"
cmake --build "$output/build" --target Bulletproof_portable xbe_recomp --parallel "${BUILD_JOBS:-6}"
python3 "$source_dir/tools/linux/package_portable.py" "$output/build" "$output/Bulletproof-linux-x86_64.tar.xz" \
    --shader-notices /opt/shader-notices
