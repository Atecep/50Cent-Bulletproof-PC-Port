"""Check portable release payloads, notices and binary dependency inventories."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import zipfile

HEADERS = {"audio/apu_clock.h", "cpu/x86_context.h", "cpu/x86_ops.h"}
FREESTANDING = {"cmath", "cstdint", "cstring", "limits", "type_traits"}
CRT = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
IMPORTS = {"xbe-vcruntime.lib", "xbe-ucrt-heap.lib", "xbe-ucrt-math.lib"}
WINDOWS_SYSTEM = set("""
    advapi32 bcrypt bcryptprimitives cfgmgr32 combase comdlg32 crypt32 dbghelp dwmapi
    gdi32 gdi32full hid imm32 iphlpapi kernel32 kernelbase msvcrt ntdll ole32 oleaut32
    powrprof psapi rpcrt4 secur32 setupapi shell32 shlwapi ucrtbase user32 userenv
    version vulkan-1 winhttp winmm wintrust ws2_32
""".split())


def validate(archive_path, title, llvm_readobj, coff_dir=None, source_dxc=None):
    windows = archive_path.suffix == ".zip"
    elf_tools = {"Bulletproof", "compiler/clang", "compiler/ld.lld", "compiler/xbe_recomp"}
    pe_payload = (
        {"Bulletproof.exe", "dxcompiler.dll", "dxil.dll", "compiler/clang.exe",
         "compiler/lld-link.exe", "compiler/xbe_recomp.exe"}
        | CRT | {"compiler/" + dll for dll in CRT}
    )
    required = (
        {"README.txt", "compiler/config/game.toml", "compiler/xdk/5849/layout.toml",
         "LICENSES/Bulletproof-MIT.txt", "LICENSES/THIRD_PARTY_NOTICES.md",
         "LICENSES/PROVENANCE.txt", "LICENSES/SDL3.txt", "LICENSES/SDK-THIRD-PARTY.txt"}
        | {"compiler/freestanding/" + name for name in FREESTANDING}
        | {"compiler/include/" + name for name in HEADERS}
    )
    if windows:
        required |= ((pe_payload - {"dxil.dll"}) | {"compiler/lib/" + name for name in IMPORTS}
                     | {"compiler/guest.def", "compiler/guest_hook_list.inc", "compiler/guest_hook_defines.h",
                        "compiler/portable_guest_crt.cc", "compiler/portable_guest_module.cc",
                        "compiler/include/app/portable_guest_abi.h", "LICENSES/BUILD-INPUTS.txt",
                        "LICENSES/SDK.txt", "LICENSES/LLVM.txt", "LICENSES/DXC.txt",
                        "LICENSES/DXC-ThirdPartyNotices.txt", "LICENSES/ImGui.txt",
                        "LICENSES/Vulkan-Headers.md", "LICENSES/Vulkan-Headers-Apache-2.0.txt",
                        "LICENSES/Vulkan-Headers-MIT.txt", "LICENSES/Vulkan-Loader.txt",
                        "LICENSES/Zydis.txt", "LICENSES/Zycore.txt", "LICENSES/tomlplusplus.txt"})
        archive = zipfile.ZipFile(archive_path)
        prefix = ""
    else:
        required.remove("LICENSES/SDL3.txt")
        required |= elf_tools | {"LICENSES/sdl3.txt", "LICENSES/ELF-REQUIREMENTS.tsv",
                                 "LICENSES/XboxXBERecomp-MIT.txt", "LICENSES/Shaders/SOURCES.txt",
                                 "LICENSES/imgui.txt", "LICENSES/zydis.txt", "LICENSES/zycore.txt",
                                 "LICENSES/tomlplusplus.txt"}
        archive = tarfile.open(archive_path, "r:xz")
        prefix = "Bulletproof/"

    with archive:
        files, dirs, names = {}, set(), set()
        members = archive.infolist() if windows else archive.getmembers()
        for item in members:
            raw = item.filename if windows else item.name
            path = PurePosixPath(raw)
            if (path.is_absolute() or "\\" in raw or ":" in raw or ".." in path.parts
                    or str(path) != raw.rstrip("/") or raw.rstrip("/").lower() in names):
                raise ValueError(f"Unsafe or duplicate archive member: {raw}")
            names.add(raw.rstrip("/").lower())
            if not windows and raw == "Bulletproof" and item.isdir():
                continue
            if not raw.startswith(prefix):
                raise ValueError(f"Unexpected path outside package root: {raw}")
            name = raw[len(prefix):].rstrip("/")
            is_dir = item.is_dir() if windows else item.isdir()
            if is_dir:
                dirs.add(name)
                continue
            if windows:
                if stat.S_IFMT(item.external_attr >> 16) not in (0, stat.S_IFREG):
                    raise ValueError(f"Unexpected ZIP member type: {name}")
            elif not (item.isfile() or item.islnk()):
                raise ValueError(f"Unexpected tar member type: {name}")
            files[name] = item

        missing = required - files.keys()
        if missing or "assets" not in dirs:
            raise ValueError(f"Missing required package entries: {sorted(missing)}")
        if any(directory.startswith("assets/") for directory in dirs):
            raise ValueError("The assets directory must be empty")
        if source_dxc:
            if "dxil.dll" in files:
                raise ValueError("The source-built DXC route must not stage a separate dxil.dll")
            with archive.open(files["dxcompiler.dll"]) as packaged, source_dxc.open("rb") as built:
                if hashlib.file_digest(packaged, "sha256").digest() != hashlib.file_digest(built, "sha256").digest():
                    raise ValueError("Packaged DXC DLL differs from the source-built DLL")

        def opened(item):
            return archive.open(item) if windows else archive.extractfile(item)

        elf_files = set()
        with tempfile.TemporaryDirectory() as scratch:
            probe = Path(scratch) / "probe.bin"
            for name, item in files.items():
                known = name in required or name in pe_payload and windows
                lib = not windows and re.fullmatch(r"(?:compiler/)?lib/lib[\w.+-]+\.so(?:\.[\w.+-]+)?", name)
                notice = name.startswith("LICENSES/") and re.fullmatch(
                    r"(?:[\w.+-]+/)*[\w.+-]+", name.removeprefix("LICENSES/")
                )
                if not (known or lib or notice):
                    raise ValueError(f"Unexpected payload: {name}")
                if not windows and item.islnk():
                    target = item.linkname.removeprefix(prefix)
                    if (not lib or not item.linkname.startswith(prefix)
                            or target not in files or not files[target].isfile()
                            or PurePosixPath(name).name != PurePosixPath(target).name):
                        raise ValueError(f"Unexpected hard link: {name}")
                with opened(item) as content:
                    if name in elf_tools or lib:
                        if content.read(4) != b"\x7fELF":
                            raise ValueError(f"Invalid ELF payload: {name}")
                        elf_files.add(name)
                    elif windows and name in pe_payload:
                        with probe.open("wb") as dest:
                            shutil.copyfileobj(content, dest)
                        inspect_pe(probe, name, files, llvm_readobj)
                    elif windows and name.startswith("compiler/lib/"):
                        if content.read(8) != b"!<arch>\n":
                            raise ValueError(f"Invalid generated COFF archive: {name}")
                        if coff_dir and b"!<arch>\n" + content.read() != (coff_dir / PurePosixPath(name).name).read_bytes():
                            raise ValueError(f"COFF archive does not match the build output: {name}")
                    else:
                        text = content.read(2 * 1024 * 1024 + 1)
                        if len(text) > 2 * 1024 * 1024 or b"\0" in text or text.startswith(b"XBEH"):
                            raise ValueError(f"Unexpected non-text payload: {name}")
                        if name == "compiler/config/game.toml" and text != title:
                            raise ValueError("Packaged title metadata differs from the build input")
            if not windows:
                with opened(files["LICENSES/ELF-REQUIREMENTS.tsv"]) as audit:
                    paths = {line.split("\t", 1)[0] for line in audit.read().decode().splitlines()[1:]}
                if paths != elf_files:
                    raise ValueError("The ELF audit does not cover the complete package payload")
    print(f"{archive_path.name}: {len(files)} allowed files, empty assets/, title input matched")


def inspect_pe(probe, name, files, readobj):
    report = subprocess.check_output([readobj, "--file-headers", "--coff-imports", probe], text=True)
    if "IMAGE_FILE_MACHINE_AMD64" not in report:
        raise ValueError(f"Expected AMD64 PE binary: {name}")
    directory = PurePosixPath(name).parent
    dependencies = sorted(set(re.findall(r"^\s+Name: ([\w.-]+\.dll)$", report, re.M | re.I)))
    for dll in dependencies:
        dll = dll.lower()
        if dll.removesuffix(".dll") not in WINDOWS_SYSTEM and not dll.startswith(("api-ms-win-", "ext-ms-win-")):
            if str(directory / dll) not in files:
                raise ValueError(f"{name}: unbundled and unrecognized PE dependency {dll}")
    print(f"{name}: {', '.join(dependencies)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--title", required=True, type=Path)
    parser.add_argument("--llvm-readobj", default="llvm-readobj")
    parser.add_argument("--coff-dir", type=Path)
    parser.add_argument("--source-dxc", type=Path)
    args = parser.parse_args()
    if not (args.archive.name.endswith(".tar.xz") or args.archive.suffix == ".zip"):
        parser.error("Expected a .tar.xz or .zip archive")
    if args.archive.suffix == ".zip" and args.coff_dir is None:
        parser.error("--coff-dir is required to audit a Windows archive")
    if args.source_dxc and args.archive.suffix != ".zip":
        parser.error("--source-dxc applies only to a Windows archive")
    validate(args.archive, args.title.read_bytes(), args.llvm_readobj, args.coff_dir, args.source_dxc)
