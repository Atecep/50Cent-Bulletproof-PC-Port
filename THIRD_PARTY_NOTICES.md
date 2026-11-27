# Third-party notices

The project's own contributions use the [root MIT license](LICENSE). The components below retain their applicable terms.

Keep the complete license texts and applicable copyright notices with the source and the packages that contain the corresponding material. This inventory identifies the relevant upstream terms and the remaining provenance checks. [Publication audit](docs/LICENSING.md)

## SDK and pinned dependencies

- **XboxXBERecomp**, copyright (c) 2026 TGP482, MIT. This project retains the upstream SDK license and historical dependency notices. The five freestanding headers match the pinned Windows SDK byte for byte. The Linux import also has byte-identical files covered by the pinned branch MIT notice. The 5849 tables have confirmed upstream origin. Their derived-data review and the provenance of remaining imported changes are recorded in the licensing audit. Upstream credits SCDA for their introduction. Preserve the SDK MIT notice with these headers. [SDK license](sdk/LICENSE) · [SDK notices](sdk/THIRD_PARTY.txt) · [Vulkan snapshot notices](sdk/linux/THIRD_PARTY.txt) · [Pinned upstream SDK](https://github.com/TGP482/XboxXBERecomp/tree/cc9edda3a821a4204dc28783f429b88a27ade4f9) · [Imported branch license](https://github.com/TGP482/XboxXBERecomp/blob/adc40ab688be14da932514bd9f6f59a29dab3c65/LICENSE)
- **XenonRecomp**, MIT. XboxXBERecomp credits XenonRecomp for the overridable function layout and instruction hooks. Individual reused source files, if any, remain to be identified. [Upstream credit](https://github.com/TGP482/XboxXBERecomp/blob/main/README.md) · [XenonRecomp license](https://github.com/hedge-dev/XenonRecomp/blob/main/LICENSE.md)

| Pinned component | License and source |
| --- | --- |
| SDL 3.4.16 | zlib. Use the license and component notices from this tag, including those for the bitmap font used by the setup UI. [SDL license](https://github.com/libsdl-org/SDL/blob/release-3.4.16/LICENSE.txt) |
| Dear ImGui 1.91.9b | MIT. [ImGui license](https://github.com/ocornut/imgui/blob/v1.91.9b/LICENSE.txt) |
| toml++ 3.4.0 | MIT. [toml++ license](https://github.com/marzer/tomlplusplus/blob/v3.4.0/LICENSE) |
| Zydis 4.1.1 | MIT. [Zydis license](https://github.com/zyantific/zydis/blob/v4.1.1/LICENSE) |
| Zycore | MIT. Use the notice from the submodule revision selected by Zydis 4.1.1. [Zydis submodule record](https://github.com/zyantific/zydis/tree/v4.1.1/dependencies) |

## Portable and platform components

- **LLVM, Clang and LLD** use Apache-2.0 with LLVM exceptions for modern LLVM code, with additional terms for some included components. Carry the exact release notices and preserve attribution for those components. [LLVM license policy](https://llvm.org/docs/DeveloperPolicy.html#copyright-license-and-patents) · [Local reference text](sdk/tools/LLVM-LICENSE.txt)
- **DirectX Shader Compiler** carries legacy LLVM and other component terms. The CI source candidate keeps DXC's upstream license and third-party notices and the licenses of its pinned SPIRV-Headers and SPIRV-Tools gitlinks. The prebuilt 1.9.2609 ZIP keeps its separate MIT, LLVM and Microsoft notices. Redistribution permission for these exact binaries remains unresolved. The exact source-built payload still requires validation and review. [Pinned DXC source license](https://github.com/microsoft/DirectXShaderCompiler/blob/01b62ad47db7dee3dd33f90e7339cf9e860b3f93/LICENSE.TXT) · [Pinned DXC third-party notices](https://github.com/microsoft/DirectXShaderCompiler/blob/01b62ad47db7dee3dd33f90e7339cf9e860b3f93/ThirdPartyNotices.txt) · [DXC clearance decisions](docs/LICENSING.md#windows-dxc)
- **glslang and SPIRV-Tools** supply the Linux shader compilation path. glslang includes component-specific terms and SPIRV-Tools uses Apache-2.0. [glslang license](https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt) · [SPIRV-Tools license](https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE)
- **Vulkan-Headers and zlib-compatible compression** are build inputs. Retain the relevant notices from the actual header revision and the compression implementation used in each package. [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md) · [zlib](https://zlib.net/zlib_license.html) · [zlib-ng](https://github.com/zlib-ng/zlib-ng/blob/develop/LICENSE.md)
- **Linux transitive libraries** carry the terms of the exact packages staged into the archive. The Jammy compiler bundle ships `libgcc_s.so.1` and `libstdc++.so.6` from Ubuntu `gcc-12=12.3.0-1ubuntu1~22.04.3`. Keep the GCC license and Runtime Library Exception notices. CI stages the matching source, Ubuntu packaging and patches as a separate checksummed release asset. The full delivery awaits a hosted run. [Exact source record](docs/LICENSING.md#linux-corresponding-source) · [GNU libstdc++ license](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html)
- **Microsoft Visual C++ Runtime** app-local DLLs are subject to the matching licensed tools' redistribution terms. The default Windows package uses project-generated archives. The standalone MSVC and Windows SDK `.lib` files are reserved for the internal reference package. [Visual Studio 2022 REDIST](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) · [CRT library roles](https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features)
