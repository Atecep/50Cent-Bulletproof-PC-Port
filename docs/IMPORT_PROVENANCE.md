# Surviving code requiring provenance confirmation

This list follows up the [initial-import comparison](LICENSING.md#imported-contributions). It is a review queue for attribution and permission. A difference from the SDK establishes where to check, and attribution must come from a source revision, an author record or permission from the rights holder.

The comparison used the first local import `56f5d44` and the XboxXBERecomp snapshots [Linux `adc40ab6`](https://github.com/TGP482/XboxXBERecomp/tree/adc40ab688be14da932514bd9f6f59a29dab3c65) and [main `cc9edda3`](https://github.com/TGP482/XboxXBERecomp/tree/cc9edda3a821a4204dc28783f429b88a27ade4f9). Exact initial blob matches to either snapshot were excluded. For modified upstream files, the table lists current line ranges for nonblank added or replaced lines retained verbatim from the initial diff. For files introduced with the port, the table shows the current first-to-last surviving line, which may enclose later edits. A rewritten hunk can still carry earlier work. Treat the containing function as the review unit if authorship remains unclear. Removed files and deletion-only changes have no surviving text in this list.

## Modified upstream paths

Paths are relative to `sdk/linux/`. Line numbers refer to the Phase 12C working tree.

| File | Surviving lines |
| --- | --- |
| `cmake/XbeRecomp.cmake` | 251, 257, 261 |
| `runtime/app/app.h` | 44, 95–97 |
| `runtime/app/game_files.cpp` | 172–173 |
| `runtime/app/main_sdl.cpp` | 22, 312–314, 396, 398–401, 511–515, 517–518, 524, 600, 602, 614 |
| `runtime/app/settings.cpp` | 14, 49, 201–220, 223, 237–238 |
| `runtime/audio/sdl_audio.cpp` | 19–20, 60, 66–69, 72–73 |
| `runtime/core/input.cpp` | 3, 53, 69–76, 78–81, 111–121 |
| `runtime/core/input.h` | 15, 18, 23–30 |
| `runtime/cpu/x86_ops.h` | 9, 25–30 |
| `runtime/gpu/render_thread.cpp` | 32 |
| `runtime/gpu/shaders.cpp` | 581, 583–593, 595–604, 606–616, 618, 664 |
| `runtime/gpu/spirv.cpp` | 75–78, 245–246, 263–268 |
| `runtime/gpu/state.h` | 158, 193 |
| `runtime/gpu/texture.cpp` | 373–382 |
| `runtime/gpu/vulkan.cpp` | 7, 24, 42, 419–420, 615–617, 945, 968, 1201, 1213, 1469–1470, 1475–1476, 1543–1546, 1878, 1880–1882, 1886, 1891, 1894, 1900, 1907, 1917, 1935, 1938, 1942, 1989–1993, 2012, 2053, 2071, 2099, 2117–2119, 2174–2242, 2320, 2511, 2513, 2531, 2548, 2658 |
| `runtime/kernel/files.cpp` | 154, 157, 159–177, 179, 181–193, 197, 200, 630–633, 640, 645, 647, 653, 655–658, 741–745, 762–765 |
| `runtime/kernel/kernel.h` | 156, 160, 191 |
| `runtime/kernel/threads.cpp` | 8, 148–151, 161, 169–174, 192–196, 223–225, 249, 251–254, 256–257 |
| `runtime/kernel/xboxkrnl.cpp` | 10, 811, 825–828 |
| `runtime/xdk/d3d8.cpp` | 7, 19–20, 22–23, 387–391, 396, 398–401, 416, 430, 450–459, 461–466, 544, 617, 639, 705–720, 762, 897–898, 928–931, 1128, 1187–1193, 1195–1196, 1198–1202, 1204–1213, 1215–1236, 1238, 1240–1247, 1251, 1253–1263 |
| `runtime/xdk/d3d8.h` | 12–14 |
| `runtime/xdk/dsound.cpp` | 1, 13–15, 112–116, 126–128, 232–236, 245–254, 260, 291–304, 306–308, 310–325, 327–332, 337–339, 344, 347, 400, 404, 406–416, 418, 420, 602, 644–650, 748, 754, 759, 767, 772, 776–777, 783–784, 793–794, 802, 807, 822–825, 831–834, 839, 855–856, 901–905, 907–908, 1076–1118, 1120–1130 |
| `runtime/xdk/xapi.cpp` | 23 |
| `tools/recomp/codegen.cpp` | 116–118 |

## SDK files with no corresponding path in the pinned snapshots

Paths are relative to `sdk/linux/`.

| File | Surviving region |
| --- | --- |
| `runtime/app/userdata.cpp` | 1–159 |
| `runtime/audio/apu_clock.cpp` | 1–17 |
| `runtime/audio/apu_clock.h` | 1–14 |
| `runtime/audio/capture.h` | 1–52 |
| `runtime/audio/dsound_voice.h` | 1–12 |
| `runtime/audio/pull_timing.h` | 1–18 |
| `runtime/core/desktop_input.cpp` | 1–377 |
| `runtime/core/desktop_input.h` | 1–40 |
| `runtime/core/mouse_rate.h` | 1–32 |
| `runtime/core/vblank.h` | 1–25 |
| `runtime/core/video_trace.h` | 1–66 |
| `runtime/gpu/raster_domain.h` | 1–40 |

## Title code already present at the initial import

Paths are relative to `src/`.

| File | Surviving region |
| --- | --- |
| `common/game_info.cpp` | 2–55 |
| `game/crouch_grounding.cpp` | 2–27 |
| `game/framerate_timing.cpp` | 1–521 |
| `game/framerate_timing.h` | 1–20 |
| `game/graphics_menu.cpp` | 1–1169 |
| `game/graphics_menu.h` | 1–46 |
| `game/hooks.cpp` | 1–314 |
| `game/input_system.cpp` | 1–80 |
| `game/input_system.h` | 1–9 |
| `game/linux_dsound.cpp` | 1–235 |
| `game/linux_gameplay_prompts.cpp` | 2–222 |
| `game/linux_gameplay_prompts.h` | 1–10 |
| `game/linux_graphics.cpp` | 1–37 |
| `game/linux_mouse_camera.cpp` | 2–166 |
| `game/linux_video.cpp` | 1–241 |
| `game/linux_weapon_selector.cpp` | 2–51 |
| `game/native_menu_state.h` | 1–93 |
| `game/native_video_menu.cpp` | 1–483 |
| `game/prompt_bindings.cpp` | 2–105 |
| `game/prompt_bindings.h` | 1–41 |
| `game/stair_contact.cpp` | 2–108 |
| `game/stair_contact.h` | 1–21 |
| `game/timing_telemetry.cpp` | 1–453 |
| `game/timing_telemetry.h` | 1–35 |
| `game/video_frame_snapshot.h` | 1–55 |

For each affected portion, record the author or upstream repository and exact revision, the applicable license and any notices. The project's MIT approval applies to confirmed original contributions. Use a clean export allowlist once these records and the separate metadata decisions are complete.
