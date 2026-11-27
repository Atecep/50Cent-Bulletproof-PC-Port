50 Cent: Bulletproof – PC Port
Native Vulkan PC Port for Windows and Linux
Linux x86-64 internal test package

1. Extract the complete archive to a writable directory.
2. Place your original Xbox Bulletproof ISO in assets/. Any name ending in
   .iso works. This build accepts the same game revision as its developer build.
3. Open Bulletproof. The first launch prepares the game code offline, then
   starts the game. Later launches reuse userdata/guest/guest.so.

Use Esc or close the preparation window to cancel. Open Bulletproof again
to retry. Diagnostics are in userdata/logs/. Keep userdata/ with the package
when moving it if you want to retain the generated code, settings and saves.

Requires an x86-64-v2 processor, glibc 2.35 or newer, and a Linux desktop
session with the system C++ runtime, zlib, Vulkan loader and a working driver.
The Ubuntu 22.04 (Jammy) build environment defines the library baseline.
See LICENSES/PROVENANCE.txt and LICENSES/ELF-REQUIREMENTS.tsv for the package
inputs and required symbol versions. The port's own contributions use MIT.
Public redistribution clearance is pending.
