# 50 Cent: Bulletproof PC Port

<!-- Cover image -->

An unofficial native PC port of the original Xbox version of *50 Cent: Bulletproof* for Windows and Linux. It uses Vulkan and runs from your own supported Xbox disc image.

## Features

- **Graphics:** Vulkan rendering at up to 4K, with adjustable render scale and VSync.
- **Display:** Widescreen 16:9 and classic 4:3 in windowed, borderless and fullscreen modes.
- **Frame rate:** Choose a 30, 60, 90 or 120 FPS cap. The port adapts the original game's timing and frame pacing for PC.
- **Controls:** Aim with the mouse, adjust sensitivity, remap keyboard and mouse actions, or play with an Xbox controller.
- **Audio:** The port adapts the game's audio playback and mixing for modern Windows and Linux systems.
- **Portable and offline:** Extract the archive and run it from a writable folder. The port automatically recompiles the game code on first launch and reuses it on later launches. Preparation and play work offline.

| Preset | Widescreen (16:9) | Classic (4:3) |
| --- | ---: | ---: |
| 720p | 1280 × 720 | 960 × 720 |
| 1080p | 1920 × 1080 | 1440 × 1080 |
| 1440p | 2560 × 1440 | 1920 × 1440 |
| 2160p | 3840 × 2160 | 2880 × 2160 |

<!-- Gameplay screenshot -->

## Requirements

- **Windows:** Windows 11 x64 with a working Vulkan driver.
- **Linux:** An x86-64-v2 CPU, glibc 2.35 or newer, a desktop session, the system C++ runtime and zlib, and a working Vulkan loader and driver. Ubuntu 22.04 is the package baseline.
- **Both platforms:** A Vulkan 1.2-capable GPU and your own Xbox disc image of the supported revision. Check the [supported XBE fingerprint](docs/BUILDING.md#portable-host-without-game-assets).

## Installation

1. When a portable release is available, download the Windows ZIP or Linux tar.xz archive from GitHub Releases. Compare its SHA-256 checksum with the entry in `SHA256SUMS.txt`.
2. Extract the entire archive to a writable folder. Place your supported Xbox disc image, with an `.iso` extension, in the extracted `assets/` folder.
3. Run `Bulletproof.exe` on Windows or `./Bulletproof` from the extracted folder on Linux. On first launch, let the bundled tools finish recompiling the game code.

Keep `userdata/` when moving the installation to retain saves, settings and the compiled code. If setup fails, check `userdata/logs/` and use the **Installation problem** issue form. Include a short, redacted log excerpt and keep disc images, extracted files, generated code and saves private.

## Compatibility and limitations

The menus and opening campaign gameplay have been reached on Windows 11 and Linux. Full campaign completion, other disc revisions and compatibility on clean systems still need wider testing. See the [Campaign Compatibility Tracker](docs/CAMPAIGN_COMPATIBILITY.md) for coverage and reporting instructions.

Fullscreen resolution options depend on your monitor and compositor. Wayland can limit exclusive fullscreen modes, and performance at 120 FPS depends on your system. See [known issues](docs/KNOWN_ISSUES.md).

## Credits and licenses

Thanks to the original game developers and the port's contributors. The port builds on TGP482's XboxXBERecomp, which credits XenonRecomp for its function layout and hook design. [XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp) · [XenonRecomp](https://github.com/hedge-dev/XenonRecomp)

The port's own contributions are under the MIT License. Bundled components retain their respective licenses. [MIT License](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md) · [Licensing details](docs/LICENSING.md)

This independent fan project is unaffiliated with the game's rights holders. The original game and its trademarks belong to their respective owners. Players supply the original game files.
