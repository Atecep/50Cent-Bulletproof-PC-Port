"""Deliver the pinned Jammy sources corresponding to the packaged GNU runtimes."""

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
import tempfile
from urllib.request import urlopen


SNAPSHOT = "https://snapshot.ubuntu.com/ubuntu/20261126T000000Z/pool/main/g/gcc-12/"
SOURCE = "gcc-12"
VERSION = "12.3.0-1ubuntu1~22.04.3"
DESCRIPTOR = f"{SOURCE}_{VERSION}.dsc"
CHECKSUMS = {
    DESCRIPTOR: "ec040a1fa94186bd53535f7156e086e67863bb55836ae7fd7fef64f1ab66b04d",
    "gcc-12_12.3.0.orig.tar.gz": "62b0fc89b6d41f9df2470d0fb4995f6ff5885f910518a2c6a44a6888ea5a9ea1",
    f"{SOURCE}_{VERSION}.debian.tar.xz": "44074c3d5e7d97365f1f7b45291ade2ee40ed6300176530d912e7cc0ceba77ab",
}
LIBRARIES = {
    "compiler/lib/libgcc_s.so.1": "libgcc-s1:amd64",
    "compiler/lib/libstdc++.so.6": "libstdc++6:amd64",
}


def verify_binary_archive(archive_path):
    prefix = "Bulletproof/"
    with tarfile.open(archive_path, "r:xz") as archive:
        with archive.extractfile(prefix + "LICENSES/PROVENANCE.txt") as notice:
            lines = notice.read().decode("utf-8").splitlines()
        rows = [line.split("\t") for line in lines if "\t" in line]
        gcc_packages = {row[0] for row in rows if len(row) == 4 and re.fullmatch(r"gcc-\d+", row[2])}
        packaged = {
            row[0] for row in rows if len(row) == 4 and row[2] in gcc_packages
        } | {
            item.name.removeprefix(prefix) for item in archive.getmembers()
            if PurePosixPath(item.name).name.startswith(("libgcc_s.so", "libstdc++.so"))
        }
        if packaged != LIBRARIES.keys():
            raise ValueError("GNU runtime payload changed; review the matching source pins.")
        for path, package in LIBRARIES.items():
            versions = [row for row in rows if row[0] == package]
            if versions != [[package, VERSION, SOURCE, VERSION]]:
                raise ValueError(f"Unexpected GNU package/source version for {package}: {versions}")
            entries = [row for row in rows if row[0] == path]
            with archive.extractfile(prefix + path) as library:
                digest = hashlib.file_digest(library, "sha256").hexdigest()
            if len(entries) != 1 or entries[0][:3] != [path, digest, package]:
                raise ValueError(f"GNU archive member does not match its provenance: {path}")


def verify_descriptor(text):
    for key, value in (("Source", SOURCE), ("Version", VERSION)):
        if re.findall(rf"^{key}: (.+)$", text, re.M) != [value]:
            raise ValueError(f"Unexpected {key} in the pinned source descriptor.")
    block = re.search(r"^Checksums-Sha256:\n((?:[ \t]+[^\n]+\n)+)", text, re.M)
    if not block:
        raise ValueError("Missing SHA-256 manifest in the pinned source descriptor.")
    rows = [line.split() for line in block[1].splitlines()]
    expected = {name: digest for name, digest in CHECKSUMS.items() if name != DESCRIPTOR}
    if len(rows) != len(expected) or {name: digest for digest, _, name in rows} != expected:
        raise ValueError("Pinned descriptor names or checksums differ from the expected source set.")
    return {name: int(size) for _, size, name in rows}


def download_verified(directory, name):
    path = directory / name
    with urlopen(SNAPSHOT + name, timeout=90) as response, path.open("wb") as output:
        shutil.copyfileobj(response, output)
    with path.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != CHECKSUMS[name]:
            raise ValueError(f"Ubuntu source SHA-256 mismatch: {name}")
    return path


def bundle(archive_path, output):
    verify_binary_archive(archive_path)
    with tempfile.TemporaryDirectory(dir=output.parent) as scratch:
        directory = Path(scratch)
        descriptor = download_verified(directory, DESCRIPTOR)
        for name, size in verify_descriptor(descriptor.read_text(encoding="utf-8")).items():
            if download_verified(directory, name).stat().st_size != size:
                raise ValueError(f"Ubuntu source size mismatch: {name}")
        (directory / "SHA256SUMS.txt").write_text(
            "".join(f"{digest}  {name}\n" for name, digest in CHECKSUMS.items()), encoding="ascii"
        )
        (directory / "README.txt").write_text(
            f"Corresponding source for the GNU runtime libraries in {archive_path.name}.\n"
            f"Ubuntu Jammy source: {SOURCE} {VERSION}\n"
            f"Original source and Ubuntu packaging, patches and build rules from {SNAPSHOT}\n"
            "Verify with sha256sum -c SHA256SUMS.txt, then unpack with dpkg-source -x " + DESCRIPTOR + ".\n",
            encoding="utf-8",
        )
        candidate = directory / "sources.tar"
        with tarfile.open(candidate, "w") as archive:
            for name in sorted([*CHECKSUMS, "README.txt", "SHA256SUMS.txt"]):
                with (directory / name).open("rb") as source:
                    member = tarfile.TarInfo(name)
                    member.size = (directory / name).stat().st_size
                    archive.addfile(member, source)
        candidate.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("linux_archive", type=Path)
    parser.add_argument("source_archive", type=Path)
    args = parser.parse_args()
    bundle(args.linux_archive, args.source_archive)
