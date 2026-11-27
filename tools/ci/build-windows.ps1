#Requires -Version 7.3
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Work,
    [string]$Title,
    [ValidateSet('source', 'release-zip')][string]$DxcMode = 'source',
    [switch]$TestOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$null = New-Item -ItemType Directory -Force $Work
$Work = (Resolve-Path $Work).Path

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio 2022 x64 build tools are required.' }
$setup = "$Work/setup-msvc.cmd"
@"
@call "$vs\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
@if errorlevel 1 exit /b 1
@set
"@ | Set-Content -Encoding ascii $setup
Push-Location $Work
try {
    & cmd.exe /d /c setup-msvc.cmd | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
} finally {
    Pop-Location
}
if ($env:VCToolsVersion.TrimEnd('\') -ne '14.44.35207' -or $env:WindowsSDKVersion.TrimEnd('\') -ne '10.0.26100.0') {
    throw 'The runner image must provide MSVC 14.44.35207 and Windows SDK 10.0.26100.0.'
}

$llvm = "$env:ProgramFiles/LLVM/bin"
$env:PATH = "$llvm;$env:PATH"
$clangVersion = (& "$llvm/clang.exe" --version | Select-Object -First 1)
if ($clangVersion -notmatch 'clang version 20\.1\.8\b' -or (& "$llvm/lld-link.exe" --version) -notmatch '\b20\.1\.8\b') {
    throw 'The runner image must provide LLVM / LLD 20.1.8.'
}
$runtime = "$root/sdk/linux/runtime"
Push-Location $Work
try {
    & "$llvm/clang++.exe" --target=x86_64-pc-windows-msvc -std=c++20 -fuse-ld=lld -DUNICODE -D_UNICODE -I $runtime `
        "$root/tests/savegame_portable_test.cpp" "$runtime/kernel/files.cpp" "$runtime/app/userdata.cpp" `
        -o "$Work/savegame_portable_test.exe"
    & "$Work/savegame_portable_test.exe"
} finally {
    Pop-Location
}
if ($TestOnly) { return }

if (!$Title -or !(Test-Path -LiteralPath $Title -PathType Leaf)) { throw 'Verified title metadata is required.' }
if ((cmake --version | Select-Object -First 1) -ne 'cmake version 3.31.6' -or (ninja --version) -ne '1.13.2') {
    throw 'The runner image must provide CMake 3.31.6 and Ninja 1.13.2.'
}
$deps = "$Work/deps"
python "$PSScriptRoot/fetch_sources.py" $deps --windows
$dxcLicense = "$Work/dxc-licenses.txt"
$dxcThirdParty = "$Work/dxc-third-party.txt"
if ($DxcMode -eq 'source') {
    & "$PSScriptRoot/build-dxc.ps1" -Work $Work
    $dxcProvenance = Get-Content -Raw -LiteralPath "$Work/dxc-provenance.txt"
} else {
    $dxcUrl = 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip'
    $dxcZip = "$Work/dxc.zip"
    Invoke-WebRequest $dxcUrl -OutFile $dxcZip
    $dxcHash = 'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1'
    if ((Get-FileHash -Algorithm SHA256 $dxcZip).Hash -ne $dxcHash) { throw 'DXC SHA-256 mismatch.' }
    Expand-Archive -LiteralPath $dxcZip -DestinationPath "$Work/dxc"
    Remove-Item -LiteralPath $dxcZip
    foreach ($name in 'LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt') {
        Add-Content -Encoding utf8 $dxcLicense "`n$name`n"
        Add-Content -Encoding utf8 $dxcLicense (Get-Content -Raw -LiteralPath "$Work/dxc/$name")
    }
    $dxcThirdPartyUrl = 'https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/v1.9.2609/ThirdPartyNotices.txt'
    Invoke-WebRequest $dxcThirdPartyUrl -OutFile $dxcThirdParty
    if ((Get-FileHash -Algorithm SHA256 $dxcThirdParty).Hash -ne '19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d') {
        throw 'DXC third-party notices SHA-256 mismatch.'
    }
    $dxcProvenance = "DXC 1.9.2609 upstream ZIP: $dxcUrl (SHA-256 $dxcHash)`nDXC source notices: $dxcThirdPartyUrl"
}

$vulkanImport = "$Work/vulkan-1.lib"
& "$llvm/lld-link.exe" /lib /machine:x64 "/def:$deps/vulkan-loader/loader/vulkan-1.def" "/out:$vulkanImport"
$build = "$Work/build"
cmake --preset release -S $root -B $build -DBULLETPROOF_PORTABLE_ONLY=ON `
    "-DBULLETPROOF_PORTABLE_CONFIG=$Title" -DXBE_SHADER_BACKEND=dxc "-DXBE_DXC_ROOT=$Work/dxc" `
    "-DVulkan_INCLUDE_DIR=$deps/vulkan-headers/include" "-DVulkan_LIBRARY=$vulkanImport" `
    "-DCMAKE_C_COMPILER=$llvm/clang.exe" "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" `
    "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld" `
    "-DFETCHCONTENT_SOURCE_DIR_ZYDIS=$deps/zydis-src" "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=$deps/tomlplusplus-src" `
    "-DFETCHCONTENT_SOURCE_DIR_IMGUI=$deps/imgui-src" "-DFETCHCONTENT_SOURCE_DIR_SDL3=$deps/sdl3-src"
cmake --build $build --target Bulletproof_portable xbe_recomp --parallel 4
if (Test-Path "$build/generated") { throw 'Unexpected guest translations in portable build.' }

$redist = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    if ((Get-Item "$redist/$dll").VersionInfo.FileVersion -ne '14.44.35211.0') {
        throw "Unexpected app-local release CRT version in $dll"
    }
}
$llvmNotice = "$Work/llvm-LICENSE.txt"
$llvmLicenseUrl = 'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.8/llvm/LICENSE.TXT'
Invoke-WebRequest $llvmLicenseUrl -OutFile $llvmNotice
if ((Get-FileHash -Algorithm SHA256 $llvmNotice).Hash -ne '8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee') {
    throw 'LLVM notice SHA-256 mismatch.'
}
$provenance = "$Work/build-inputs.txt"
@"
LLVM 20.1.8 ($clangVersion)
LLVM notice: $llvmLicenseUrl
GitHub runner image: $env:ImageOS $env:ImageVersion
MSVC 14.44.35207, Windows SDK 10.0.26100.0
Visual Studio 2022 x64 VC143 REDIST 14.44.35211.0
REDIST terms: https://learn.microsoft.com/visualstudio/releases/2022/redistribution#visual-c-runtime-files
CMake 3.31.6, Ninja 1.13.2
$dxcProvenance
Vulkan-Headers: https://github.com/KhronosGroup/Vulkan-Headers/tree/b379292b2ab6df5771ba9870d53cf8b2c9295daf
Vulkan import library (build only): https://github.com/KhronosGroup/Vulkan-Loader/tree/f8616928ee19f6c7fd648c1cf1f456cba3771855
"@ | Set-Content -Encoding utf8 $provenance

& "$root/tools/windows/package_portable.ps1" -Build $build -OutputDirectory "$Work/dist" -LlvmBin $llvm `
    -VcRedist $redist -DependencySources $deps -TitleConfig $Title -LlvmLicense $llvmNotice `
    -DxcLicense $dxcLicense -DxcThirdPartyNotices $dxcThirdParty `
    -BuildProvenance $provenance
Move-Item "$Work/dist/Bulletproof-windows-x86_64-internal.zip" "$Work/dist/Bulletproof-windows-x86_64.zip"
$dxcAudit = @()
if ($DxcMode -eq 'source') { $dxcAudit = @('--source-dxc', "$Work/dxc/bin/dxcompiler.dll") }
python "$PSScriptRoot/audit_package.py" "$Work/dist/Bulletproof-windows-x86_64.zip" `
    --title $Title --llvm-readobj "$llvm/llvm-readobj.exe" --coff-dir "$build/Bulletproof_portable_support/lib" @dxcAudit
