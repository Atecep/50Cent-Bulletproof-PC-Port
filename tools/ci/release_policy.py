"""Decide which portable workflow jobs can run and name their outputs."""

import os
import re
from pathlib import Path


VERSION_PATTERN = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def read_project_version(cmake):
    match = re.search(r"^\s*project\s*\(\s*Bulletproof\s+VERSION\s+([^\s)]+)", cmake, re.M | re.I)
    if not match or not re.fullmatch(VERSION_PATTERN, match[1]):
        raise ValueError("Could not read a literal Bulletproof version from CMakeLists.txt.")
    return match[1]


def plan(event, operation, version, project_version, windows_dxc="source"):
    package = draft = False
    if event == "workflow_dispatch":
        if operation not in {"build-only", "build-and-draft-release"}:
            raise ValueError("Select a valid manual operation.")
        draft = operation == "build-and-draft-release"
        if draft or version:
            stable = re.fullmatch("v" + VERSION_PATTERN, version)
            if not stable or tuple(map(int, stable.groups())) < (0, 8, 0):
                raise ValueError("Enter a stable vMAJOR.MINOR.PATCH version starting at v0.8.0.")
            if version != f"v{project_version}":
                raise ValueError(f"Version must match CMakeLists.txt (v{project_version}).")
        if windows_dxc not in {"source", "release-zip"}:
            raise ValueError("Select a valid Windows DXC source.")
        package = True

    suffix = f"-{version}" if draft else ""
    return {
        "package": str(package).lower(),
        "draft": str(draft).lower(),
        "version": version if draft else "",
        "linux_archive": f"Bulletproof{suffix}-linux-x86_64.tar.xz",
        "gnu_sources": f"Bulletproof{suffix}-linux-gnu-sources.tar",
        "windows_dxc": windows_dxc if package else "",
        "windows_archive": f"Bulletproof{suffix}-windows-x86_64.zip",
    }


if __name__ == "__main__":
    try:
        cmake = Path(__file__).resolve().parents[2] / "CMakeLists.txt"
        outputs = plan(
            os.environ["EVENT"], os.environ["OPERATION"], os.environ["VERSION"],
            read_project_version(cmake.read_text(encoding="utf-8")),
            os.environ["WINDOWS_DXC"],
        )
    except ValueError as error:
        raise SystemExit(str(error)) from None
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        for key, value in outputs.items():
            output.write(f"{key}={value}\n")
