#!/usr/bin/env python3
"""Package a Bulletproof Linux Release build for glibc 2.35 and x86-64-v2."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GLIBC_MAX = (2, 35)
SYSTEM_LIBRARIES = {
    "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libpthread.so.0",
    "libdl.so.2", "librt.so.1", "libresolv.so.2", "libutil.so.1",
    "libvulkan.so.1",
}
# The Vulkan ICD and desktop plug-ins share the host process. Let them use the
# distribution's C++ runtime and zlib. The isolated compiler processes keep theirs.
HOST_SYSTEM_LIBRARIES = SYSTEM_LIBRARIES | {"libstdc++.so.6", "libgcc_s.so.1", "libz.so.1"}
INTERPRETER = "/lib64/ld-linux-x86-64.so.2"


def run(*args):
    return subprocess.check_output(args, text=True, env={
        **os.environ, "LC_ALL": "C", "LD_LIBRARY_PATH": "", "LD_PRELOAD": "",
    })


def cache_path(cache, name, default):
    entry = re.search(rf"^{name}:[^=]+=(.+)$", cache, re.M)
    return Path(entry[1]) if entry else default


def dependencies(path, system_libraries):
    for line in run("ldd", path).splitlines():
        fields = line.split()
        if len(fields) < 3 or fields[1] != "=>":
            continue
        name, target = Path(fields[0]).name, fields[2]
        if not target.startswith("/"):
            raise RuntimeError(f"{path}: unresolved dependency {name}")
        if name not in system_libraries:
            yield name, Path(target).resolve()


def elf_requirements(path, library_dir, system_libraries):
    headers = run("readelf", "-hW", path)
    if "ELF64" not in headers or "Advanced Micro Devices X86-64" not in headers:
        raise RuntimeError(f"{path}: expected an ELF x86-64 binary")
    program = run("readelf", "-lW", path)
    interpreter = re.search(r"Requesting program interpreter: (.+)\]", program)
    if interpreter and interpreter[1] != INTERPRETER:
        raise RuntimeError(f"{path}: unexpected interpreter {interpreter[1]}")
    # Jammy's readelf does not decode SDL's unrelated .note.dlopen records.
    sections = run("readelf", "-SW", path)
    if ".note.gnu.property" in sections or "GNU_PROPERTY" in program:
        for line in run("readelf", "-nW", path).splitlines():
            if "ISA needed:" in line and re.search(r"x86-64-v[34]\b", line):
                raise RuntimeError(f"{path}: {line.strip()}")
    needed = sorted(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", run("readelf", "-dW", path)))
    missing = [name for name in needed if name not in system_libraries and not (library_dir / name).is_file()]
    if missing:
        raise RuntimeError(f"{path}: missing bundled dependencies: {', '.join(missing)}")

    # Definitions exported by a bundled library do not raise the system ABI floor.
    _, _, needs = run("readelf", "--version-info", "-W", path).partition("Version needs section")
    versions = sorted(set(re.findall(r"\bName: (\S+)", needs)))
    for version in versions:
        if version.startswith("GLIBC_"):
            number = version.removeprefix("GLIBC_")
            if not re.fullmatch(r"\d+(?:\.\d+)+", number) or tuple(map(int, number.split("."))) > GLIBC_MAX:
                raise RuntimeError(f"{path}: {version} exceeds the glibc 2.35 baseline")
    return ", ".join(needed), ", ".join(versions)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as data:
        for chunk in iter(lambda: data.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_owner(path, debian):
    # Jammy's merged /usr may differ from the path recorded in dpkg's file list.
    paths = [path]
    if path.is_relative_to("/usr/lib"):
        paths.append(Path("/") / path.relative_to("/usr"))
    for candidate in paths:
        args = ("dpkg-query", "-S", candidate) if debian else ("pacman", "-Qoq", candidate)
        result = subprocess.run(args, capture_output=True, text=True)
        if result.returncode == 0 and result.stdout:
            return result.stdout.splitlines()[0].split(": ", 1)[0]
    return ""


def add_provenance(notices, stage, provenance, shader_notices):
    debian = shutil.which("dpkg-query") is not None
    packages = set()
    with (notices / "PROVENANCE.txt").open("w") as output:
        output.write(f"Build libc: {run('getconf', 'GNU_LIBC_VERSION')}")
        output.write("The packager rewrites the executables' ELF RPATH to ${ORIGIN}/lib for relocation.\n")
        output.write("SDL bitmap font: public domain VGA, Marcel Sondaar / IBM (SDL source header).\n")
        output.write("\nArchive path\tSHA-256\tBinary package\tSource filename\n")
        for path, source in sorted(provenance.items()):
            package = package_owner(source, debian)
            if package:
                packages.add(package)
            output.write(f"{path}\t{sha256(stage / path)}\t{package or 'project'}\t{source.name}\n")

        if debian:
            output.write("\nBinary package\tVersion\tSource package\tSource version\n")
            host_packages = {"libc6:amd64", "libstdc++6:amd64", "libgcc-s1:amd64", "zlib1g:amd64", "libvulkan1:amd64"}
            output.write(run("dpkg-query", "-W",
                             "-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n",
                             *sorted(packages | host_packages)))
        else:
            output.write("\nBinary package versions\n")
            output.write(run("pacman", "-Q", *sorted(packages | {"glibc"})))

    if debian:
        for package in sorted(packages):
            source = Path("/usr/share/doc") / package.split(":")[0] / "copyright"
            if not source.is_file():
                raise RuntimeError(f"Missing Debian copyright file for {package}")
            shutil.copyfile(source, notices / f"{package.split(':')[0]}.txt")
        for license_name in ("Apache-2.0", "GPL-2", "GPL-3", "LGPL-2", "LGPL-2.1", "LGPL-3"):
            source = Path("/usr/share/common-licenses") / license_name
            shutil.copyfile(source, notices / license_name)
    else:
        for package in sorted(packages):
            source = Path("/usr/share/licenses") / package
            if source.is_dir():
                shutil.copytree(source, notices / package)
        for license_name in ("Apache-2.0", "GPL-3.0-or-later", "LGPL-2.1-or-later", "LGPL-3.0-or-later"):
            source = Path("/usr/share/licenses/spdx") / (license_name + ".txt")
            shutil.copyfile(source, notices / source.name)
    shutil.copytree(shader_notices, notices / "Shaders")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--shader-notices", type=Path, required=True,
                        help="license and source records for the glslang build and its dependencies")
    args = parser.parse_args()
    build, archive = args.build.resolve(), args.archive.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache or "XBE_SHADER_BACKEND:STRING=glslang" not in cache:
        parser.error("Use a Release build configured with -DXBE_SHADER_BACKEND=glslang")
    if not archive.name.endswith(".tar.xz"):
        parser.error("The output filename must end in .tar.xz")
    if not (args.shader_notices / "SOURCES.txt").is_file():
        parser.error("--shader-notices must contain SOURCES.txt and the shader compiler notices")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    archive.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=archive.parent) as temp:
        temp = Path(temp)
        stage = temp / "Bulletproof"
        compiler = stage / "compiler"
        notices = stage / "LICENSES"
        for rel in ("assets", "lib", "compiler/lib", "compiler/config", "compiler/xdk/5849",
                    "compiler/include/cpu", "compiler/include/audio", "compiler/freestanding", "LICENSES"):
            (stage / rel).mkdir(parents=True)
        provenance = {}

        def copy(source, dest, executable=False):
            source = Path(source).resolve(strict=True)
            shutil.copyfile(source, dest)
            dest.chmod(0o755 if executable else 0o644)
            provenance[dest.relative_to(stage).as_posix()] = source

        host = stage / "Bulletproof"
        binaries = {
            host: build / "Bulletproof_portable",
            compiler / "xbe_recomp": build / "xbe_recomp",
            compiler / "clang": shutil.which(cache_path(cache, "CMAKE_C_COMPILER", "clang")),
            compiler / "ld.lld": shutil.which("ld.lld"),
        }
        library_copies = {}
        for binary, source in binaries.items():
            if not source:
                raise RuntimeError(f"Cannot locate {binary.name}")
            copy(source, binary, True)
            target_lib = stage / "lib" if binary == host else compiler / "lib"
            system_libraries = HOST_SYSTEM_LIBRARIES if binary == host else SYSTEM_LIBRARIES
            # Resolve against the build environment, then make the staged closure relocatable.
            for name, dependency in dependencies(source, system_libraries):
                dest = target_lib / name
                if dest.exists():
                    if provenance[dest.relative_to(stage).as_posix()] != dependency:
                        raise RuntimeError(f"Conflicting libraries for {dest}")
                elif dependency in library_copies:
                    os.link(library_copies[dependency], dest)
                    provenance[dest.relative_to(stage).as_posix()] = dependency
                else:
                    copy(dependency, dest)
                    library_copies[dependency] = dest
            subprocess.run(["patchelf", "--force-rpath", "--set-rpath", "$ORIGIN/lib", binary], check=True)

        sdk = ROOT / "sdk"
        linux = sdk / "linux/runtime"
        title = cache_path(cache, "BULLETPROOF_PORTABLE_CONFIG", ROOT / "config/game.toml")
        for source, dest in {
            title: compiler / "config/game.toml",
            sdk / "xdk/5849/layout.toml": compiler / "xdk/5849/layout.toml",
            linux / "audio/apu_clock.h": compiler / "include/audio/apu_clock.h",
            linux / "cpu/x86_context.h": compiler / "include/cpu/x86_context.h",
            linux / "cpu/x86_ops.h": compiler / "include/cpu/x86_ops.h",
        }.items():
            copy(source, dest)
        for source in sorted((sdk / "runtime/cpu/freestanding").iterdir()):
            copy(source, compiler / "freestanding" / source.name)

        with (notices / "ELF-REQUIREMENTS.tsv").open("w") as output:
            output.write("Archive path\tDT_NEEDED\tRequired symbol versions\n")
            for binary in (*binaries, *sorted((stage / "lib").iterdir()), *sorted((compiler / "lib").iterdir())):
                tool = binary.is_relative_to(compiler)
                target_lib = compiler / "lib" if tool else stage / "lib"
                system_libraries = SYSTEM_LIBRARIES if tool else HOST_SYSTEM_LIBRARIES
                needed, versions = elf_requirements(binary, target_lib, system_libraries)
                output.write(f"{binary.relative_to(stage)}\t{needed}\t{versions}\n")

        shutil.copyfile(ROOT / "LICENSE", notices / "Bulletproof-MIT.txt")
        shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", notices / "THIRD_PARTY_NOTICES.md")
        shutil.copyfile(sdk / "LICENSE", notices / "XboxXBERecomp-MIT.txt")
        shutil.copyfile(sdk / "THIRD_PARTY.txt", notices / "SDK-THIRD-PARTY.txt")
        for dep, name in (("sdl3", "LICENSE.txt"), ("imgui", "LICENSE.txt"), ("tomlplusplus", "LICENSE"),
                          ("zydis", "LICENSE"), ("zydis", "dependencies/zycore/LICENSE")):
            source = cache_path(cache, f"FETCHCONTENT_SOURCE_DIR_{dep.upper()}", build / "_deps" / f"{dep}-src")
            shutil.copyfile(source / name, notices / f"{'zycore' if '/' in name else dep}.txt")
        add_provenance(notices, stage, provenance, args.shader_notices)
        shutil.copyfile(ROOT / "tools/linux/PORTABLE_README.txt", stage / "README.txt")

        temporary_archive = temp / "package.tar.xz"
        subprocess.run(["tar", "--sort=name", "--format=gnu", f"--mtime=@{epoch}", "--owner=0", "--group=0",
                        "--numeric-owner", "-I", "xz -T1 -6", "-cf", temporary_archive, "-C", temp, "Bulletproof"],
                       check=True, env={**os.environ, "LC_ALL": "C", "XZ_OPT": "", "XZ_DEFAULTS": ""})
        os.replace(temporary_archive, archive)


if __name__ == "__main__":
    main()
