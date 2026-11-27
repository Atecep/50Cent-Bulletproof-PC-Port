#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$1
deps=$2
mkdir -p "$work"
printf '[xbe]\n' > "$work/test-only.toml"
cmake --preset linux-debug -S "$root" -B "$work/build" \
    -DBULLETPROOF_PORTABLE_ONLY=ON -DBUILD_TESTING=ON -DXBE_SHADER_BACKEND=glslang \
    -DBULLETPROOF_PORTABLE_CONFIG="$work/test-only.toml" \
    -DFETCHCONTENT_SOURCE_DIR_ZYDIS="$deps/zydis-src" \
    -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="$deps/tomlplusplus-src" \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI="$deps/imgui-src" \
    -DFETCHCONTENT_SOURCE_DIR_SDL3="$deps/sdl3-src"
cmake --build "$work/build" --parallel "${BUILD_JOBS:-4}" --target \
    native_menu_state_test stair_contact_test framerate_timing_test desktop_input_test \
    l3_crouch_test video_frame_snapshot_test linux_disc_smoke apu_mmio_test \
    worker_sync_test vblank_wait_test spirv_bindings_test savegame_portable_test
ctest --test-dir "$work/build" --show-only=json-v1 | python3 -c '
import json, sys
tests = json.load(sys.stdin)["tests"]
if len(tests) != 13 or not any(test["name"] == "video_frame_order" for test in tests):
    sys.exit("Expected 13 asset-free CTests, including the NumPy video test")
'
ctest --test-dir "$work/build" --output-on-failure
