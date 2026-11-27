# Known issues and coverage

Phase 9 validated Linux and Windows 11 Release hosts, portable preparation and cache reuse, the initial campaign gameplay, and the presentation controls. Campaign-wide coverage remains pending. Phase 10 revalidates Linux after source consolidation. Equivalent Windows compile, package preparation, cache reuse and a brief save/gameplay/display smoke test remain to be repeated on Windows.

The selected aspect ratio supports four heights at 16:9 and 4:3. The compositor and display mode list determine available physical fullscreen modes. Borderless can render internally at a different resolution from the monitor. Wayland can constrain exclusive modes. Frame pacing can exceed the nominal 8.33 ms frame budget at 120 FPS. Present timings describe application presentation, while physical scanout depends on the compositor and monitor.

The Linux NWFSX idle backoff is bounded and specific to the title worker. Windows keeps the existing scheduling by default. A complete signaling contract for the relevant guest tables remains unproven. Keep the existing VBlank, lifecycle and audio behavior when investigating pacing.
