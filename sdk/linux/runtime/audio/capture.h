#pragma once
// Opt-in diagnostic only: interleaved stereo float32 LE at mixer rate (48 kHz).
// Files are process-lifetime and created only under XBE_AUDIO_CAPTURE.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace audio_capture {
inline bool Enabled() { static bool yes = std::getenv("XBE_AUDIO_CAPTURE") != nullptr; return yes; }
inline uint64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Capture {
    std::string directory;
    uint64_t frame = 0;
    uint32_t count = 0;
    FILE* events = nullptr;
    FILE* controls = nullptr;
    std::map<std::string, FILE*> files;
    std::vector<float> source;
    Capture() {
        directory = std::getenv("XBE_AUDIO_CAPTURE");
        std::filesystem::create_directories(directory);
        events = std::fopen((directory+"/events.csv").c_str(),"w");
        if (events) std::fputs("ns,frame,count,event,obj,data,tag,bits,channels,align,rate,freq,pos,volume,headroom,output,g00,g01,g10,g11\n",events);
        controls = std::fopen((directory+"/controls.csv").c_str(),"w");
        if (controls) std::fputs("ns,event,obj,render_pos,reported_pos,value\n",controls);
    }
    void Write(const std::string& name,const float* pcm) {
        auto& file = files[name];
        if (!file) file = std::fopen((directory+"/"+name+".f32").c_str(),"wb");
        if (file) {
            std::fseek(file,long(frame*2*sizeof(float)),SEEK_SET);
            std::fwrite(pcm,sizeof(float),count*2,file);
            if (frame%48000==0) std::fflush(file);
        }
    }
    template<class V> void Voice(const V& v, float g[2][2]) {
        if (events) std::fprintf(events,"%llu,%llu,%u,voice,%u,%u,%u,%u,%u,%u,%u,%u,%.9f,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%.9g\n",
            (unsigned long long)Now(),(unsigned long long)frame,count,v.obj,v.data,v.tag,v.bits,v.channels,v.align,v.rate,v.freq,v.pos,v.volume,v.headroom,v.output,g[0][0],g[0][1],g[1][0],g[1][1]);
    }
    void Pull(int requested,int supplied) {
        if (events) std::fprintf(events,"%llu,%llu,%u,pull,%d,%d\n",(unsigned long long)Now(),(unsigned long long)frame,count,requested,supplied);
    }
    void Control(const char* name,uint32_t obj,double rendered,double reported,uint32_t value) {
        if (controls) std::fprintf(controls,"%llu,%s,%u,%.9f,%.9f,%u\n",(unsigned long long)Now(),name,obj,rendered,reported,value);
    }
    void Advance() { frame += count; if (events && frame%48000==0) std::fflush(events); }
};
inline Capture& Get() { static Capture state; return state; }
} // namespace audio_capture
