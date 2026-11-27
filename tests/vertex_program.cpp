// White-box contract test of the production Load/Select implementation.
#include "../sdk/linux/runtime/xdk/d3d8.cpp"
#include <vector>
#include <iostream>
namespace kernel { uint8_t* g_base = nullptr; }
namespace logx { void Write(Level, const char*, ...) {} }
X86_EXTERN_FUNC(sub_00250E10);
X86_EXTERN_FUNC(sub_0024DC70);
static void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    std::vector<uint8_t> ram(d3d::kDevice + XDK_CDevice_Size);
    kernel::g_base = ram.data();
    using namespace d3d;
    auto shader = std::make_shared<gpu::VertexShader>();
    shader->program = {0, 0, 0, 1};
    shader->inputs[0] = {0, 12, 0x32};
    shader->stream_stride[0] = 24;
    g_vs[0x101] = shader;
    X86Context ctx{}; ctx.esp = 0x100;
    kernel::W32(0x104, 0x101); kernel::W32(0x108, 5);
    kernel::Args args{ctx};
    Check(hle_D3DDevice_LoadVertexShader(ctx, args) == 0, "load real shader program");
    Check(hle_D3DDevice_SelectVertexShader(ctx, args) == 0, "select program and inputs");
    Check(g_state.vs_handle == 0x101 && g_state.vs && g_state.vs->program == shader->program,
          "selection binds programmable shader to render state");
    Check(g_state.vs->inputs[0].offset == 12 && g_state.vs->stream_stride[0] == 24, "input layout retained");
    auto kept = g_state.vs;
    hle_D3DDevice_SelectVertexShader(ctx, args);
    Check(kept == g_state.vs, "reuse immutable selected shader");
    shader->program[0] = 0x1234;
    hle_D3DDevice_LoadVertexShader(ctx, args);
    kernel::W32(0x104, 0);
    hle_D3DDevice_SelectVertexShader(ctx, args);
    Check(g_state.vs != kept && g_state.vs->program[0] == 0x1234 && g_state.vs->inputs[0].offset == 12,
          "handle zero changes start while preserving inputs; old draw snapshot unchanged");
    Check(kept->program[0] == 0, "recorded shader immutable after program upload");
    kernel::W32(0x104, 0xDEAD);
    Check(hle_D3DDevice_SelectVertexShader(ctx, args) != 0, "reject unknown handle without guessing guest layout");
    kernel::W32(0x104, 0x101); kernel::W32(0x108, 136);
    Check(hle_D3DDevice_SelectVertexShader(ctx, args) != 0, "reject invalid program slot");
    // The game's shader manager asks for instruction counts and advances its
    // resident slot by that count. Exercise the real title binding, without
    // console object fields in the opaque handles' backing memory.
    auto first = std::make_shared<gpu::VertexShader>(*shader);
    auto second = std::make_shared<gpu::VertexShader>(*shader);
    first->program.assign(6 * 4, 0); first->program.back() = 1;
    second->program.assign(4 * 4, 0); second->program.back() = 1;
    first->program[0] = 0x1111; second->program[0] = 0x2222;
    g_vs[0x201] = first; g_vs[0x301] = second;
    uint32_t slot = 0;
    for (uint32_t handle : {0x201u, 0x301u}) {
        ctx.esp = 0x100; ctx.ebx = 0xCAFEBABE;
        kernel::W32(0x104, handle); kernel::W32(0x108, 0x200);
        kernel::W32(0x200, 0);
        sub_00250E10(ctx, kernel::g_base);
        Check(ctx.esp == 0x10C && ctx.ebx == 0xCAFEBABE && ctx.eax == 0x200,
              "size query void stdcall ABI and nonvolatile register");
        const uint32_t size = kernel::R32(0x200);
        Check(size == (handle == 0x201 ? 6u : 4u), "size is instructions, not bytes or opaque memory");
        ctx.esp = 0x100; kernel::W32(0x108, slot);
        hle_D3DDevice_LoadVertexShader(ctx, args);
        slot += size;
    }
    for (int frame = 0; frame < 3; ++frame) {
        kernel::W32(0x104, 0x201); kernel::W32(0x108, 0);
        hle_D3DDevice_SelectVertexShader(ctx, args);
        Check(g_state.vs->program == first->program, "resident six-instruction program survives later upload");
        kernel::W32(0x104, 0x301); kernel::W32(0x108, 6);
        hle_D3DDevice_SelectVertexShader(ctx, args);
        Check(g_state.vs->program == second->program, "resident four-instruction program selected at slot six");
    }
    ctx.esp = 0x100; kernel::W32(0x108, 0);
    sub_00250E10(ctx, kernel::g_base);
    Check(ctx.esp == 0x10C && ctx.eax == 0, "null size output is permitted by original body");
    uint32_t invalid = 0xABCD;
    Check(!VertexProgramSize(0xDEAD, invalid) && invalid == 0xABCD, "invalid handles do not invent a size");
    // Test SetPalette binding and state update
    ctx.esp = 0x100;
    kernel::W32(0x104, 0);       // stage 0
    kernel::W32(0x108, 0x12340); // palette pointer
    sub_0024DC70(ctx, kernel::g_base);
    Check(ctx.esp == 0x10C, "sub_0024DC70 stdcall pops 8 bytes");
    Check(g_state.palettes[0] == 0x12340, "sub_0024DC70 updates g_state.palettes[0]");
    // Exercise the real render-state entry points and the binding-state refresh.
    const gpu::Surface scene{0x400, 0x10000, 0x12 << 8, 0};
    const gpu::Surface back{0x500, 0x20000, 0x1e << 8, 0};
    SetDev(kDevBackBuffer, back.header);
    auto bind = [&](const gpu::Surface& target) {
        Set(g_state.rt, target);
        UpdateRasterDomain();
    };
    auto expect_extent = [&](float w, float h, const char* why) {
        const auto e = gpu::DrawPositionExtent(g_state, true, true, 640, 480);
        Check(e.width == w && e.height == h, why);
    };
    bind(scene);
    expect_extent(640, 480, "mode 0 uses the physical extent");
    ctx.esp = 0x100;
    kernel::W32(0x104, 2);
    hle_D3DDevice_SetRenderState_MultiSampleRenderTargetMode(ctx, args);
    expect_extent(320, 240, "mode 2 reinterprets the pitched offscreen target");
    const auto mode_two_draw = g_state;
    Check(g_state.rt.data == scene.data && g_state.viewport.w == 640,
          "logical domain leaves resource identity and viewport request intact");
    Set(g_state.viewport, {64, 32, 512, 384, 0.0f, 1.0f});
    expect_extent(320, 240, "sub-viewport does not replace the logical target extent");
    Set(g_state.viewport, mode_two_draw.viewport);
    const auto wide = gpu::DrawPositionExtent(g_state, true, true, 720, 480);
    Check(wide.width == 360 && wide.height == 240, "wide target uses its own logical extent");
    const auto odd = gpu::DrawPositionExtent(g_state, true, true, 641, 481);
    Check(odd.width == 321 && odd.height == 241, "XDK rounds odd logical extents up");
    const auto fixed = gpu::DrawPositionExtent(g_state, false, true, 640, 480);
    const auto depth_only = gpu::DrawPositionExtent(g_state, true, false, 640, 480);
    Check(fixed.width == 640 && depth_only.width == 640, "fixed and depth-only paths are unchanged");
    bind(back);
    expect_extent(640, 480, "the backbuffer retains its own raster domain");
    Set(g_state.super_x, 2.0f);
    Set(g_state.super_y, 2.0f);
    expect_extent(1280, 960, "backbuffer supersampling is independent");
    Set(g_state.super_x, 1.0f);
    Set(g_state.super_y, 1.0f);
    bind(gpu::Surface{0x600, 0x30000, 0x06 << 8, 0});
    expect_extent(640, 480, "swizzled targets are excluded by the XDK format guard");
    bind(scene);
    expect_extent(320, 240, "rebinding the scene recovers mode 2");
    kernel::W32(0x104, gpu::kRsMultiSampleRenderTargetMode);
    kernel::W32(0x108, 1);
    hle_D3DDevice_SetRenderStateNotInline(ctx, args);
    expect_extent(640, 480, "other modes retain the existing path");
    kernel::W32(0x108, 0);
    hle_D3DDevice_SetRenderStateNotInline(ctx, args);
    expect_extent(640, 480, "mode 0 clears the half-size domain");
    const auto retained = gpu::DrawPositionExtent(mode_two_draw, true, true, 640, 480);
    Check(retained.width == 320 && retained.height == 240, "recorded state retains its per-draw domain");

    // The truck uses MODULATE followed by BLENDFACTORALPHA with no stage-1 texture.
    gpu::State material{};
    gpu::TexInfo textures[4]{};
    textures[0].kind = gpu::kTex2D;
    material.tss[0][gpu::kTssColorOp] = 4;
    material.tss[0][gpu::kTssColorArg1] = 2;
    material.tss[0][gpu::kTssColorArg2] = 0;
    material.tss[1][gpu::kTssColorOp] = 15;
    material.tss[1][gpu::kTssColorArg1] = 2;
    material.tss[1][gpu::kTssColorArg2] = 1;
    material.tss[1][gpu::kTssAlphaOp] = 4;
    material.tss[1][gpu::kTssAlphaArg1] = 2;
    material.tss[1][gpu::kTssAlphaArg2] = 1;
    auto pixel = gpu::MakePixelKey(material, textures);
    Check(pixel.ff[0][0] == 4 && pixel.ff[1][0] == 2 && pixel.ff[1][1] == 1,
          "unbound texture preserves the modulated colour");
    Check(pixel.ff[1][4] == 2 && pixel.ff[1][5] == 1,
          "unbound texture preserves current alpha");
    textures[1].kind = gpu::kTex2D;
    pixel = gpu::MakePixelKey(material, textures);
    Check(pixel.ff[1][0] == 15 && pixel.ff[1][1] == 2 && pixel.ff[1][4] == 4,
          "bound texture keeps the configured blend");
    textures[1].kind = 0;
    material.tss[1][gpu::kTssColorOp] = 3;
    pixel = gpu::MakePixelKey(material, textures);
    Check(pixel.ff[1][0] == 3 && pixel.ff[1][2] == 1,
          "an unused texture argument does not replace the operation");
    std::cout << "PASS: program slots, shader inputs, binding, snapshots, cache, invalid handles and raster domains\n";
}
