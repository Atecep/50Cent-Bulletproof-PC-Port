// Exercise the production HLE state/mixer directly. Unreachable platform/guest
// entry points are discarded at link; only logging and guest RAM are test fixtures.
#include "../sdk/linux/runtime/xdk/dsound.cpp"
#include <array>
#include <iostream>

namespace kernel { uint8_t* g_base = nullptr; }
// PCM-only fixture: reaching packet/event completion would invalidate this test.
namespace kernel {
ObjectRef FromHandle(uint32_t) { std::abort(); }
int32_t SetEvent(Object&) { std::abort(); }
}
namespace logx { void Write(Level, const char*, ...) {} }
X86_EXTERN_FUNC(sub_0026CF8D);
X86_EXTERN_FUNC(sub_0026CDA5);
static void Check(bool yes, const char* message) {
    if (!yes) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    std::array<uint8_t, 0x4000> ram{};
    kernel::g_base = ram.data();
    Voice& v = g_voices[0x100];
    v.obj = 0x100;
    v.headroom = 1;
    float gains[2][2];
    Levels(v, gains);
    Check(gains[0][0] == 1 && gains[0][1] == 1, "neutral mono gains");
    Check(dsound::Use3DVoiceData(v.obj, true) == 0 && v.use_data, "enable real voice state");
    kernel::W32(0x1000, 1);
    kernel::W32(0x1004, uint32_t(-600));
    Check(dsound::Set3DVoiceData(v.obj, 0x1000) == 0, "set distance mB");
    Levels(v, gains);
    Check(std::fabs(gains[0][0] - std::pow(10.f, -600.f / 2000)) < 1e-6f, "3D data affects actual mixer gain");
    kernel::W32(0x1000, 2);
    kernel::W32(0x1004, uint32_t(-10000));  // excluded by mask
    kernel::W32(0x1008, uint32_t(-600));
    dsound::Set3DVoiceData(v.obj, 0x1000);
    Check(v.voice_data[1] == uint32_t(-600), "masked update retains distance");
    Levels(v, gains);
    Check(std::fabs(gains[0][0] - std::pow(10.f, -1200.f / 2000)) < 1e-6f, "distance and cone combine");
    Check(dsound::GetVoiceProperties(v.obj, 0x2000) == 0, "get voice properties");
    Check(kernel::R32(0x2000) == 2 && kernel::R32(0x2004) == 0 && kernel::R32(0x200c) == 1, "properties list real bins");
    Check(kernel::R32(0x2014) == UINT32_MAX && kernel::R32(0x2018) == uint32_t(-10000), "unused pairs bounded");
    Check(kernel::R32(0x2048) == uint32_t(-600) && kernel::R32(0x204c) == uint32_t(-600), "properties report active 3D state");
    dsound::Use3DVoiceData(v.obj, false);
    Levels(v, gains);
    Check(gains[0][0] == 1, "disable restores direct gains");
    Check(dsound::Use3DVoiceData(0xDEAD, true) != 0 && dsound::Set3DVoiceData(v.obj, 0) != 0, "invalid objects and pointers rejected");
    Voice& route = g_voices[0x200];
    route.obj = 0x200; route.nbins = 2; route.bin[0] = 0; route.bin[1] = 1;
    route.volume = 0.25f;
    route.bin_mb[0] = -600;
    X86Context ctx{}; ctx.esp = 0x3000;
    kernel::W32(ctx.esp + 4, v.obj);
    kernel::W32(ctx.esp + 8, route.obj);
    kernel::Args args{ctx};
    Check(buf_SetOutputBuffer(v, args) == 0 && v.output == route.obj, "output routing stored");
    Levels(v, gains);
    Check(std::fabs(gains[0][0] - std::pow(10.f, -.3f) * .25f) < 1e-6f && gains[0][1] == .25f, "submix bins and volume reach mixer");
    kernel::W32(ctx.esp + 8, v.obj);
    Check(buf_SetOutputBuffer(v, args) != 0, "reject self route");
    ctx.esp = 0x3000; ctx.ebx = 0x1234; ctx.esi = 0x5678; ctx.edi = 0x9abc; ctx.ebp = 0xdef0;
    kernel::W32(ctx.esp + 4, v.obj); kernel::W32(ctx.esp + 8, 1);
    sub_0026CF8D(ctx, ram.data());
    Check(ctx.esp == 0x300c && ctx.eax == 0 && v.use_data, "actual title binding: stdcall two args and real state");
    Check(ctx.ebx == 0x1234 && ctx.esi == 0x5678 && ctx.edi == 0x9abc && ctx.ebp == 0xdef0,
          "binding preserves nonvolatile guest registers");

    Check(dsound::SetPitch(v.obj, 0) == 0 && v.freq == 48000, "set pitch neutral");
    Check(dsound::SetPitch(v.obj, 4096) == 0 && v.freq == 96000, "set pitch octave up");
    Check(dsound::SetPitch(0xDEAD, 0) != 0, "set pitch invalid object");
    Check(dsound::SetLFO(v.obj, 0) == 0, "set lfo no-op");
    ctx.esp = 0x3000;
    kernel::W32(ctx.esp + 4, v.obj); kernel::W32(ctx.esp + 8, uint32_t(-4096));
    sub_0026CDA5(ctx, ram.data());
    Check(ctx.esp == 0x300c && ctx.eax == 0 && v.freq == 24000, "actual title binding: stdcall SetPitch octave down");

    // A continuous reported cursor must never authorize recycling unread PCM.
    for (auto& [id, voice] : g_voices) voice.playing = false;
    Voice& ring = g_voices[0x400];
    ring.obj=0x400; ring.tag=1; ring.channels=2; ring.align=4;
    ring.rate=44100; ring.freq=48000; ring.data=0x1000;
    ring.bytes=ring.play_len=ring.loop_len=4000; // 1000 stereo frames
    ring.playing=ring.looping=true;
    auto t0=std::chrono::steady_clock::time_point(std::chrono::seconds(100));
    ResetPlaybackClock(ring,t0);
    for (unsigned i=0;i<1000;++i) {
        int16_t sample=int16_t(i);
        std::memcpy(ram.data()+ring.data+i*4,&sample,2);
        std::memcpy(ram.data()+ring.data+i*4+2,&sample,2);
    }
    float first[960],second[960];
    audio::PullTiming timing{t0,128,0};
    {
        audio::PullScope pull(timing);
        Render(first,480); timing.generated_frames+=480;
        Render(second,480); timing.generated_frames+=480;
    }
    Check(ring.pos==960, "real read cursor advances only through generated PCM");
    Check(GetCurrentExtrapolatedPos(ring,t0+std::chrono::milliseconds(5))==240,
          "smooth cursor inside already generated batch");
    Check(GetCurrentExtrapolatedPos(ring,t0+std::chrono::milliseconds(15))==720,
          "smooth interpolation across successive chunks in one SDL pull");
    Check(GetCurrentExtrapolatedPos(ring,t0+std::chrono::seconds(1))==960,
          "stalled device cannot report unread PCM consumed");
    // The guest may replace only the reported consumed prefix. Already emitted
    // float samples stay unchanged and unread frame960 is never replaced.
    for(unsigned i=0;i<720;++i) kernel::W32(ring.data+i*4,0x7FFF7FFF);
    Check(second[958]==959/32768.f && kernel::R16(ring.data+960*4)==960,
          "recycling cannot corrupt queued floats or next unread sample");
    timing={t0+std::chrono::milliseconds(20),0,0};
    {
        audio::PullScope pull(timing); Render(first,480);
    }
    Check(first[0]==960/32768.f && first[80]==32767/32768.f,
          "ring wrap reads old tail followed by safely replaced prefix");
    Check(GetCurrentExtrapolatedPos(ring,timing.time)==960 &&
          GetCurrentExtrapolatedPos(ring,timing.time+std::chrono::milliseconds(10))==440,
          "bounded playback clock wraps into loop correctly");
    // Setters must not skip source PCM by committing a wall-clock estimate.
    ring.last_render_time=std::chrono::steady_clock::now()-std::chrono::milliseconds(5);
    const auto read_before=ring.pos;
    ctx.esp=0x3000; kernel::W32(ctx.esp+4,ring.obj); kernel::W32(ctx.esp+8,48000);
    buf_SetFrequency(ring,kernel::Args{ctx});
    Check(ring.pos==read_before, "SetFrequency preserves PCM read cursor");
    Check(dsound::SetPitch(ring.obj,0)==0 && ring.freq==48000 && ring.pos==read_before,
          "pitch0 is absolute48k even for a44100Hz format; cursor unchanged");
    Check(dsound::SetPitch(ring.obj,-4096)==0 && ring.freq==24000,
          "pitch octave down uses hardware48k reference");
    kernel::W32(ctx.esp+8,800); buf_SetCurrentPosition(ring,kernel::Args{ctx});
    Check(ring.pos==200 && ring.clock_origin==200 && ring.last_render_span==0,
          "seek resets read and reporting clocks together");
    buf_Stop(ring,kernel::Args{ctx});
    Check(!ring.playing && GetCurrentExtrapolatedPos(ring,t0)==200,"stop freezes cursor");
    kernel::W32(ctx.esp+8,1); buf_Pause(ring,kernel::Args{ctx});
    Check(ring.paused,"pause state");
    std::cout << "PASS: voice state, routing, absolute pitch, bounded smooth clock and safe PCM recycling\n";
}
