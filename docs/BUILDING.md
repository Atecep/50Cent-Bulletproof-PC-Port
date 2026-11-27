# Building

The distributed host for **50 Cent: Bulletproof – PC Port** is `Bulletproof` on Linux and `Bulletproof.exe` on Windows. CMake defines the initial version `0.8.0` for SDL's application metadata and the Windows version resource. Windows builds use the resource compiler from the Windows SDK.

Use the bundled SDK snapshot. Both hosts require CMake 3.25+, Ninja, Clang, LLD, Git for fetching pinned dependencies, a Vulkan-capable x86-64-v2 system and the platform development libraries. On Linux, install Vulkan headers and loader, glslang with SPIRV-Tools development files, zlib development files and SDL's desktop development dependencies for your session. On Windows, use an x64 Visual C++ Build Tools environment and Windows SDK with Clang targeting the MSVC ABI, Vulkan headers/loader import library, and a compatible DXC SDK with SPIR-V support. The Visual Studio IDE is optional.

For static development builds, extract `default.xbe` from the supported disc into `assets/` and restore the verified `config/game.toml` from your local inputs. The build derives its disc hash from this XBE. Keep the original ISO in `assets/` for playback. An extracted game data tree is unnecessary.

## Portable host without game assets

Configure with `BULLETPROOF_PORTABLE_ONLY`. The host uses the supported XBE SHA-1 `0abb01d5e8b323f13d26ac98b619e42220795093` and generates `image_symbols.h` from the title TOML and the bundled XDK 5849 layout. CMake does not need a disc, loose XBE or translated game code in this mode. Supply the title TOML separately through `BULLETPROOF_PORTABLE_CONFIG` or use the default `config/game.toml` if present.

```sh
cmake --preset linux-release -DBULLETPROOF_PORTABLE_ONLY=ON -DXBE_SHADER_BACKEND=glslang \
  -DBULLETPROOF_PORTABLE_CONFIG=/absolute/path/to/verified-title.toml
cmake --build --preset linux-release --target Bulletproof_portable xbe_recomp
```

```powershell
cmake --preset release -DBULLETPROOF_PORTABLE_ONLY=ON -DXBE_SHADER_BACKEND=dxc `
  "-DXBE_DXC_ROOT=$dxcSdk" "-DBULLETPROOF_PORTABLE_CONFIG=$titleToml"
cmake --build --preset release --target Bulletproof_portable xbe_recomp
```

Use a separate binary directory with `-B` when keeping both build modes. The Windows command generates the PE bridge and the guest's narrow COFF import archives, and stages DXC app-locally. The development host and recompiler build still uses the normal Windows SDK and MSVC development environment.

### Linux distribution build (Jammy)

Build the amd64 image from the small tools-only context. The recipe fixes the Ubuntu image digest, APT snapshot, CMake archive hash and Khronos source revisions. It uses Ubuntu's Clang/LLD 15 and the `linux-release` preset's x86-64-v2 flags. [Ubuntu Snapshot Service](https://snapshot.ubuntu.com/)

```sh
docker build --platform linux/amd64 -f tools/linux/Dockerfile.jammy \
  -t bulletproof-jammy tools/linux
```

Give the container the source files, a verified title TOML and a writable output directory. These mounts keep the local disc and caches outside the container.

```sh
repo="$PWD"
metadata=/absolute/path/to/verified-title.toml
output=/absolute/path/to/writable-output
mkdir -p "$output"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -e HOME=/work \
  -v "$repo/CMakeLists.txt:/src/CMakeLists.txt:ro" \
  -v "$repo/CMakePresets.json:/src/CMakePresets.json:ro" \
  -v "$repo/LICENSE:/src/LICENSE:ro" -v "$repo/THIRD_PARTY_NOTICES.md:/src/THIRD_PARTY_NOTICES.md:ro" \
  -v "$repo/sdk:/src/sdk:ro" -v "$repo/src:/src/src:ro" -v "$repo/tools:/src/tools:ro" \
  -v "$metadata:/inputs/title.toml:ro" -v "$output:/work" \
  bulletproof-jammy bash /src/tools/linux/build_portable_jammy.sh /inputs/title.toml /work
```

The script creates `Bulletproof-linux-x86_64.tar.xz` in the output directory. For an offline source build, mount the pinned dependency source trees and pass `-DFETCHCONTENT_SOURCE_DIR_*=...` after the two script arguments.

## Linux

```sh
./build.sh linux-release
cmake --preset linux-release -DBUILD_TESTING=ON -DXBE_LINUX_PORTABLE_EXPERIMENT=ON -DXBE_SHADER_BACKEND=glslang
cmake --build --preset linux-release --target all Bulletproof_portable
ctest --test-dir out/build/linux-release --output-on-failure
```

`linux-debug` selects Debug. Configure with `-B /absolute/local/path` and use `cmake --build /absolute/local/path` to keep builds outside the checkout. An out-of-tree executable needs the original ISO in its own `assets/` or another data location resolved by the host. An isolated copy or symlink to the ISO avoids duplicating the disc.

Build dependencies are SDL 3.4.16, ImGui 1.91.9b, toml++ 3.4.0 and Zydis 4.1.1 with its recorded Zycore submodule. For offline development, set the CMake `FETCHCONTENT_SOURCE_DIR_*` variables to verified copies of those revisions. Provide the shader compiler and system dependencies separately.

## Windows

```powershell
cmake --preset release -DXBE_WINDOWS_PORTABLE=ON -DXBE_SHADER_BACKEND=dxc "-DXBE_DXC_ROOT=$dxcSdk"
cmake --build --preset release --target Bulletproof Bulletproof_portable
```

The `debug` and `relwithdebinfo` presets are also available. `build.bat` is a convenience wrapper for Release. It expects external Git on PATH, the Vulkan/DXC SDKs and the verified title config. It no longer regenerates the title config.

## Regression tests

The 15 Linux CTests are opt-in with `-DBUILD_TESTING=ON`. `vertex_program` and `dsound_voice` depend on locally generated headers and are omitted in portable-only mode. `video_frame_order` requires Python with NumPy. `linux_disc_smoke` constructs a synthetic disc and `savegame_portable` uses isolated test files.
