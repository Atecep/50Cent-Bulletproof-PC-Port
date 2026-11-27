# Architecture

`src/common` defines the title. `src/game` contains the title's hooks. `sdk/linux` is the shared SDK for Linux and Windows. The directory name is historical. Both hosts route D3D8 guest operations through HLE to Vulkan. The guest producer publishes ordered batches to a render thread. Vulkan staging, framebuffer and descriptor lifetimes follow the submission fences. SDL3 owns windowing, audio and the desktop input path. Kernel HLE mounts XDVDFS data from the original ISO and maps writable title data to the emulated HDD.

The static build links generated C++ and title hooks. The portable host ships without compiled game partitions. It verifies the XBE and translates and compiles the guest locally. The cache key includes the title and toolchain inputs and the host contract. Preparation locks and publishes the guest atomically. Cancellation leaves the existing valid cache available.

Linux loads an ELF PIC module. Generated bridges let host overrides invoke original guest functions and maintain the indirect dispatch mapping. Windows loads a PE/COFF x64 module using a versioned host services table, generated hook metadata and an unwind check. The Windows module is named by digest so a later build can publish while an existing instance still uses the old module.

The APU and DirectSound paths, video buffer snapshots, guest timing hooks and Lua unwinding are shared runtime requirements. Preserve the archive of Phase 9 performance and display findings when changing those contracts.
