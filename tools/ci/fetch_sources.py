"""Fetch the exact dependency source revisions used by portable CI."""
import argparse
from pathlib import Path
import subprocess

SOURCES = {
    "zydis-src": ("zyantific/zydis", "a2278f1d254e492f6a6b39f6cb5d1f5d515659dc"),
    "tomlplusplus-src": ("marzer/tomlplusplus", "30172438cee64926dc41fdd9c11fb3ba5b2ba9de"),
    "imgui-src": ("ocornut/imgui", "f5befd2d29e66809cd1110a152e375a7f1981f06"),
    "sdl3-src": ("libsdl-org/SDL", "fa2c02bb6e21974a89ea9824bc53c9932abe5f9c"),
}
WINDOWS = {
    "vulkan-headers": ("KhronosGroup/Vulkan-Headers", "b379292b2ab6df5771ba9870d53cf8b2c9295daf"),
    "vulkan-loader": ("KhronosGroup/Vulkan-Loader", "f8616928ee19f6c7fd648c1cf1f456cba3771855"),
}
ZYCORE = "0b2432ced0884fd152b471d97ecf0258ff4d859f"


def git(path, *args):
    subprocess.run(["git", "-C", path, *args], check=True)


def fetch(path, repository, revision):
    path.mkdir()
    git(path, "init", "--quiet")
    git(path, "fetch", "--quiet", "--depth=1", f"https://github.com/{repository}.git", revision)
    git(path, "checkout", "--quiet", "--detach", revision)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--windows", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, (repository, revision) in (SOURCES | (WINDOWS if args.windows else {})).items():
        fetch(args.output / name, repository, revision)
    zydis = args.output / "zydis-src"
    expected = subprocess.check_output(
        ["git", "-C", zydis, "rev-parse", "HEAD:dependencies/zycore"], text=True
    ).strip()
    if expected != ZYCORE:
        raise RuntimeError("Unexpected Zycore gitlink in the pinned Zydis revision")
    git(zydis, "submodule", "update", "--init", "--depth=1", "--", "dependencies/zycore")
