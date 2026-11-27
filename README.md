# 50 Cent: Bulletproof – PC Port

**Native Vulkan PC Port for Windows and Linux**

`50Cent-Bulletproof-PC-Port` · Planned first release `v0.8.0`

An unofficial native x86-64 port of the original Xbox version of *50 Cent: Bulletproof*. It uses a shared Vulkan renderer and SDL3 host on Windows and Linux. The portable build prepares the game code locally from your own supported original disc image.

## Features

- Portable installation with automatic offline preparation on the first launch and cache reuse on later launches.
- Xbox controller and keyboard/mouse support, with remappable keyboard and mouse gameplay actions.
- In-game graphics settings for resolution, render scale, 4:3 and 16:9 presentation, window mode, VSync and frame-rate limits.
- Saves, settings, logs and generated game code stored under `userdata/` next to the executable.

## Requirements

- **Windows** — Windows 11 x64 with a working Vulkan driver.
- **Linux** — an x86-64-v2 CPU, glibc 2.35 or newer, a desktop session, the system C++ runtime and zlib, and a working Vulkan loader and driver. Ubuntu 22.04 defines the package baseline.
- **Both platforms** — a Vulkan 1.2-capable GPU and your own original Xbox disc image of the supported revision. See the [supported XBE fingerprint](docs/BUILDING.md#portable-host-without-game-assets).

## Installation

1. When a portable release is available, download the archive for your platform from GitHub Releases. Compare its SHA-256 with `SHA256SUMS.txt`.
2. Extract the complete archive to a writable directory. Put your own original Xbox ISO in the extracted `assets/` directory. Any filename ending in `.iso` works for a supported disc.
3. Run `Bulletproof.exe` on Windows or `./Bulletproof` on Linux. The first launch prepares the game code using the bundled tools. Later launches reuse that code. No separate compiler or builder installation is required.

If preparation fails, check `userdata/logs/` and use the **Installation problem** issue form. Keep `userdata/` when moving the installation to retain your settings and saves.

## Compatibility and support

Release builds have reached the menus and opening campaign gameplay on Linux and Windows 11. Full campaign completion, additional disc revisions and clean-system compatibility still need wider testing. Fullscreen modes depend on the monitor and compositor, and 120 FPS is not guaranteed. See [known issues](docs/KNOWN_ISSUES.md) and the [Campaign Compatibility Tracker](docs/CAMPAIGN_COMPATIBILITY.md).

Use GitHub Issues to report bugs, installation problems or campaign results. Share only relevant, redacted log excerpts and reproduction details. Keep disc images, extracted game files, generated code and saves out of issue attachments.

## Development and credits

- [Build and test from source](docs/BUILDING.md)
- [Runtime and portable guest architecture](docs/ARCHITECTURE.md)
- [Package and release workflow](docs/DISTRIBUTION.md)
- [Licensing and provenance](docs/LICENSING.md) and [third-party notices](THIRD_PARTY_NOTICES.md)

The port builds on [XboxXBERecomp by TGP482](https://github.com/TGP482/XboxXBERecomp), which credits [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for its function layout and hook design. Thanks to the original game developers and the contributors to the port and its dependencies. The port's own contributions use the [MIT License](LICENSE).

This is an independent, unofficial fan project. The original game and its trademarks belong to their respective owners. The port is unaffiliated with and has no endorsement from those owners. Game files are supplied by the player.
