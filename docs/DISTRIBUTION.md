# Portable distribution

**50 Cent: Bulletproof – PC Port** is a Native Vulkan PC Port for Windows and Linux. The planned initial release is `v0.8.0` under the future repository name `50Cent-Bulletproof-PC-Port`. The [MIT license](../LICENSE) covers the port's own contributions. Publication remains subject to the existing [licensing review](LICENSING.md).

Extract the platform package into a writable directory, put the original supported game ISO in `assets/`, and launch Bulletproof. The image filename can vary. The preparation window reports translation, compilation and link progress. Once a valid guest exists, later launches use the local cache. The runtime creates `userdata/` for settings, the emulated HDD, saves, logs and guest output. Keep it with the package when moving the game.

Internal packaging uses `tools/linux/package_portable.py` and `tools/windows/package_portable.ps1`. Both use a portable Release build. The Linux recipe uses Jammy, glslang and the distro's Clang and LLD executables. Windows uses explicit x64 toolchain, release CRT DLL, dependency-source and DXC notice paths. These packages are for internal validation pending the [licensing review](LICENSING.md).

The portable player uses bundled build tools offline and the platform Vulkan loader and driver. The default Windows package uses the generated guest import archives in `Bulletproof_portable_support/lib` and compiles the local CRT support object on first launch. `-VcRedist` supplies eligible app-local x64 runtime DLLs under Microsoft REDIST terms. The optional `-MsvcLib` and `-UcrtLib` arguments together select the original guest linkage for internal comparison. Phase 11B validated the generated linkage and cache on the Windows development host. The clean-machine check remains open. [Windows linkage validation](LICENSING.md#phase-11b-windows-linkage-implementation)

A package should contain an empty `assets/` and no original game files, prebuilt guest, save or cache.

## Build inputs for future CI

The portable host and recompiler can be built without an ISO, XBE or guest code. Supply a verified title TOML, the bundled XDK 5849 layout and the host build dependencies listed in [Building](BUILDING.md). The host embeds the supported XBE hash and compiles title addresses from that TOML. A host binary therefore still contains title metadata and is subject to its publication review. The signature table is used for scanning, while the portable host build and first-launch translation read the layout.

The Linux packager stages the same title TOML selected at host build time under `compiler/config/game.toml`. The Windows packager uses the local `config/game.toml`, which must be equivalent to the host metadata. The player's disc is introduced at runtime. Keep the verified title TOML as a separately provisioned CI input until its redistribution terms are cleared. [Provenance and regeneration](LICENSING.md)

## Linux compatibility and redistribution

The Jammy recipe builds Linux x86-64-v2 against glibc 2.35. The packager checks every shipped ELF's architecture, interpreter, declared ISA requirements, dependency closure and required GLIBC symbol versions. Host-side zlib and the C++ and GCC runtimes come from the user's distribution so the Vulkan driver can share them. The isolated compiler subprocesses get a separate bundled dependency closure. SDL uses the desktop's drivers and services when available.

`LICENSES/ELF-REQUIREMENTS.tsv` lists each shipped ELF's dependencies and required symbol versions. `LICENSES/PROVENANCE.txt` records file hashes and exact binary and source package versions. CI verifies the bundled `libgcc_s.so.1` and `libstdc++.so.6` against that manifest. It downloads the pinned Ubuntu `gcc-12` descriptor and source archives, verifies their sizes and SHA-256 digests, and stages them in a separate `linux-gnu-sources.tar` asset. Keep the source asset with the Linux archive whenever distributing the binaries. Review source-delivery obligations again if the dependency closure changes. [Licensing audit](LICENSING.md) · [GNU libstdc++ license](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html)

Phase 11C built the portable host and recompiler offline in Jammy, with no ISO, XBE or translated guest mounted. The host, recompiler, Clang and LLD each required at most `GLIBC_2.34`. The 18 shipped ELF files collectively required up to `GLIBC_2.35`, from the bundled `libgcc_s.so.1`. The package gate rejected separate fixtures that required `GLIBC_2.38` or declared x86-64-v3.

The 46.5 MiB test archive had an empty `assets/` and contained no translated guest or user data. An offline minimal Jammy container resolved the host against its system libraries and the Jammy Vulkan loader. With no developer tools on its PATH, the bundled Clang 15 and LLD 15 compiled and linked a synthetic guest-style module, and `xbe_recomp` started. On CachyOS, the same archive prepared all 65 guest translation units from the original ISO, published `guest.so`, initialized Vulkan on Wayland with the RX 9070 XT, and exited normally after the smoke test.

Phase 11D should cover a full graphical first launch on a clean Jammy desktop, another distribution and a CPU or VM restricted to x86-64-v2. The temporary test archive, build directories and Docker images were removed.

### Phase 11A Linux validation

- A fresh Release build of `Bulletproof_portable` and `xbe_recomp` succeeded from a temporary source tree and pinned dependency sources. The sandbox hid the development tree and its quarantine. The build graph had no static target or XBE or guest translation inputs. The host metadata came from a separately supplied scanner TOML.
- The extracted package started with an empty `assets/` and no cache. After linking in the original ISO, the first Wayland launch published `guest.so` at about 72 seconds and rendered the introduction on the RX 9070 XT. The second launch loaded the same module at about 1.4 seconds, with unchanged module and cache file identity and no translation. It rendered the same introductory frame.
- The static Release build with local game inputs still succeeded and passed 15 of 15 CTests.

## GitHub Actions and releases

The [portable workflow](../.github/workflows/portable.yml) runs the 13 asset-free Linux CTests and the native Windows save test on pull requests. Both manual operations are available in public and private repositories. Start from **Actions → Portable builds and releases → Run workflow** and choose the ref that contains the commit to build.

- **`build-only`** uploads the audited Linux and Windows archives, the corresponding GNU source archive and `SHA256SUMS.txt` as workflow artifacts. The version field can be left blank.
- **`build-and-draft-release`** requires an explicit stable version starting at `v0.8.0`. Any version entered must match `project(Bulletproof VERSION ...)` in `CMakeLists.txt` on the selected commit. Use a version with no existing tag or release. The draft targets the exact selected SHA and contains two versioned portable packages, `Bulletproof-v0.8.0-linux-gnu-sources.tar` and `SHA256SUMS.txt` for all three archives. Open the draft in GitHub, add the changelog while retaining the source reference, and use **Publish release** after clearance.
- **`windows_dxc`** defaults to `source`. Source mode compiles the pinned DXC and Khronos revisions, runs the shader descriptor regression through the port's Windows DXC loader and audits the packaged `dxcompiler.dll` against the tested build output. The `release-zip` option verifies the upstream ZIP against its pinned SHA-256 and includes its license notices. Redistribution permission for those exact ZIP binaries remains unresolved. [DXC licensing](LICENSING.md#windows-dxc)

For a configured repository:

1. Protect `portable-build-inputs` and restrict it to trusted refs. Add `BULLETPROOF_TITLE_TOML_B64` as an environment secret and `BULLETPROOF_TITLE_TOML_SHA256` as an environment variable. Obtain both from the verified local TOML using `base64 -w0 config/game.toml` and `sha256sum config/game.toml`. Transfer the base64 directly to the secret. Keep the ISO and XBE local.
2. The build verifies the decoded title input against its configured SHA-256 outside the checkout and packages those exact bytes. The XDK 5849 layout remains a source-tree input.
3. The `draft-release` job alone receives `contents: write` and uses the `draft-release` environment. Verify the target commit, the three archives and `SHA256SUMS.txt` in the draft before publishing it manually in GitHub.

The operator is responsible for resolving the documented licensing questions before making packages available. Signed-in readers can download workflow artifacts from a public repository, including artifacts generated while the GitHub Release is still a draft. [Licensing review](LICENSING.md) · [GitHub Actions artifact access](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts)

The archive audit enforces the payload allowlist, empty `assets/`, exact title input, expected notices and ELF or PE dependency inventory. The Windows archive accepts only the three project-generated COFF import archives. It carries the app-local CRT DLLs under the applicable REDIST terms. [Licensing](LICENSING.md)

Linux reuses the pinned Jammy image. Both packaging jobs validate the restricted title input before fetching build dependencies. Windows targets `windows-2022` with Python 3.12, LLVM/LLD 20.1.8, MSVC 14.44, Windows SDK 10.0.26100.0, CMake 3.31.6 and Ninja 1.13.2. The app-local x64 Visual Studio release CRT must be from the 14.x series and at least 14.44.35211.0. The package records the actual toolchain and DLL versions. It builds its Vulkan import library from a pinned Khronos definition.

The Phase 11D validation covered actionlint, the 13 asset-free Linux CTests and the release policy, title hash and archive checks. Phase 12C adds fixture tests for version matching, the DXC variant policy and GNU archive verification (`python3 -B -m unittest discover -s tools/ci -p 'test_*.py'`). Downloading and assembling the full GNU source archive, compiling the source-built DXC DLL, running the Windows shader regression, exchanging artifacts and creating a protected draft require a hosted run. Clean desktop, x86-64-v2-only CPU, first-launch guest generation and graphical checks with the original ISO remain manual validation.
