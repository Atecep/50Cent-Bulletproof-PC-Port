#Requires -Version 7.3
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Work)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$repo = 'https://github.com/microsoft/DirectXShaderCompiler'
$revision = '01b62ad47db7dee3dd33f90e7339cf9e860b3f93' # v1.9.2609
$gitlinks = [ordered]@{
    'SPIRV-Headers' = '496543121ce6419f23d6fa5d7194ba66c36212d2'
    'SPIRV-Tools' = 'ef96ed763b43b59b33b31b362f09a02b729fa1c9'
}

function Get-PinnedSource($Path, $Url, $Commit) {
    $null = New-Item -ItemType Directory $Path
    git -C $Path init --quiet
    git -C $Path -c core.autocrlf=false fetch --quiet --depth=1 "$Url.git" $Commit
    git -C $Path -c core.autocrlf=false checkout --quiet --detach $Commit
    if ((git -C $Path rev-parse HEAD) -ne $Commit) { throw "Wrong source revision for $Url" }
}

$source = "$Work/dxc-src"
Get-PinnedSource $source $repo $revision
foreach ($name in $gitlinks.Keys) {
    if ((git -C $source rev-parse "HEAD:external/$name") -ne $gitlinks[$name]) {
        throw "Unexpected DXC gitlink for $name"
    }
    git -C $source -c core.autocrlf=false submodule update --init --depth=1 -- "external/$name"
    if ((git -C "$source/external/$name" rev-parse HEAD) -ne $gitlinks[$name]) {
        throw "Wrong $name revision"
    }
}

$build = "$Work/dxc-build"
cmake -S $source -B $build -G Ninja -C "$source/cmake/caches/PredefinedParams.cmake" `
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
    -DENABLE_SPIRV_CODEGEN=ON -DENABLE_METAL_CODEGEN=OFF -DHLSL_OFFICIAL_BUILD=OFF `
    -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF `
    -DSPIRV_SKIP_TESTS=ON -DHLSL_BUILD_DXILCONV=OFF
cmake --build $build --target dxcompiler --parallel 4

$stage = "$Work/dxc"
$null = New-Item -ItemType Directory "$stage/bin", "$stage/include/dxc"
Copy-Item -LiteralPath "$build/bin/dxcompiler.dll" "$stage/bin/dxcompiler.dll"
Copy-Item -LiteralPath "$source/include/dxc/dxcapi.h" "$stage/include/dxc/dxcapi.h"
Copy-Item -LiteralPath "$source/ThirdPartyNotices.txt" "$Work/dxc-third-party.txt"
$files = [ordered]@{ 'DXC' = "$source/LICENSE.TXT" }
foreach ($name in $gitlinks.Keys) { $files[$name] = "$source/external/$name/LICENSE" }
foreach ($name in $files.Keys) {
    Add-Content -Encoding utf8 "$Work/dxc-licenses.txt" "`n$name`n"
    Add-Content -Encoding utf8 "$Work/dxc-licenses.txt" (Get-Content -Raw -LiteralPath $files[$name])
}

# Exercise the actual Windows Vulkan shader path, isolated beside the only candidate DLL.
$runtime = "$root/sdk/linux/runtime"
$fixture = "$stage/bin/game-info.cpp"
@'
#include "app/app.h"
namespace app { const GameInfo kGame{}; }
'@ | Set-Content -Encoding ascii $fixture
clang++.exe --target=x86_64-pc-windows-msvc -std=c++20 -fuse-ld=lld `
    -DUNICODE -D_UNICODE -DXBE_VK_TARGET=1 -DXBE_HAVE_DXC=1 -I $runtime -I "$stage/include" `
    "$root/tests/spirv_bindings.cpp" "$runtime/gpu/spirv.cpp" "$runtime/gpu/shaders.cpp" `
    "$runtime/app/userdata.cpp" $fixture -o "$stage/bin/spirv_bindings_test.exe"
& "$stage/bin/spirv_bindings_test.exe" "$Work/dxc-shader-test-cache"

$digest = (Get-FileHash -Algorithm SHA256 "$stage/bin/dxcompiler.dll").Hash.ToLowerInvariant()
@"
DXC source: $repo/tree/$revision (v1.9.2609)
SPIRV-Headers: https://github.com/KhronosGroup/SPIRV-Headers/tree/$($gitlinks['SPIRV-Headers'])
SPIRV-Tools: https://github.com/KhronosGroup/SPIRV-Tools/tree/$($gitlinks['SPIRV-Tools'])
Source-built x64 dxcompiler.dll SHA-256: $digest
DXC and SPIR-V component licenses: DXC.txt
DXC source notices: $repo/blob/$revision/ThirdPartyNotices.txt
"@ | Set-Content -Encoding utf8 "$Work/dxc-provenance.txt"
