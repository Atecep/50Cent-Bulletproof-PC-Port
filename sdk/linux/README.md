# Linux runtime snapshot

Source: `bustinout-recomp-reference/sdk`, commit
`adc40ab688be14da932514bd9f6f59a29dab3c65` (XboxXBERecomp bustinout-port).
Imported only runtime, recompiler, signature scanner and CMake integration;
no title sources, hooks, configs, signatures or game assets were imported.
Upstream LICENSE and THIRD_PARTY.txt are retained here.

The Windows SDK at `sdk/` remains at `cc9edda3a821a4204dc28783f429b88a27ade4f9`.
Both advertise SDK_VERSION 0.1.0, but their generated-code ABIs differ:
Windows uses a delay-loaded game DLL and Linux compiles guest code into the ELF.
Keeping the Linux snapshot separate avoids replacing Windows's newer audio,
texture replacement, DSP and runtime code-compilation support with the older
reference implementation. Linux does not yet provide all those features.

Local adaptations:

- Use the project's existing `sdk/xdk`, including the unmodified 5849 layout and
  signatures. The reference's 5849 layout is byte-identical; its signatures differ
  and were deliberately not imported. Existing 4831 and 5659 are also retained.
- Put Linux-generated sources and stamps in the preset's binary directory and
  pass explicit XBE/output overrides to the recompiler. This prevents Windows,
  Debug and Release from accidentally sharing code generated with another ABI.
- Disable the reference's opt-in title-specific Hor+ environment gate. Guest
  projection changes require separate validation for Bulletproof.

`config/game.toml` remains the source of symbol addresses and partition count.
Its Windows `library` key is ignored by the Linux snapshot, whose guest code is
linked directly. Do not regenerate the config with the reference scanner.
- Remove the SDL app's dependency on `common/input_kbm.h` and its mouse cache;
  these belong to The Sims title sources and have no counterpart in Bulletproof.
- Route gamepad connection events from the SDL main loop to `input::DeviceChanged`.
  Guest pad polling no longer calls SDL_PumpEvents or consumes the window queue.
  This fixes the SDL main-thread assertion observed in an actual Debug launch.

- Route generated 32-bit read operands through a register-specific MMIO helper
  for the APU 48 kHz sample counter. See `docs/apu-mmio-validation.md` in the
  project root for diagnosis, tests, scope and the subsequent audio failure.

- Add precomputed 3D voice state/property operations and submix routing to the
  stereo mixer, with Linux-only verified Bulletproof bindings and original
  table initializers. Implement Load/SelectVertexShader using existing SDK
  program-slot logic adapted to Linux state marking. See
  `docs/dsound-validation.md` in the project root for evidence and limitations.

- Run glslang's IO mapper before emitting SPIR-V, use core SPIR-V without the
  unenabled HLSL metadata extension, and version the resulting shader cache ABI.
  Avoid binding the uninitialized gamma LUT during non-gamma blits. Resolve the
  title's verified shader instruction-count query through the HLE registry so
  resident programs occupy distinct slots. Opt-in Vulkan draw/readback evidence
  and regression tests are described in `docs/rendering-validation.md`.
