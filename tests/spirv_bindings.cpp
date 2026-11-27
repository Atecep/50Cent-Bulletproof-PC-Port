#include "gpu/spirv.h"
#include "gpu/shaders.h"
#include "core/log.h"
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <cstdlib>
namespace logx {
void Write(Level, const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::vfprintf(stderr, fmt, args); va_end(args); std::fputc('\n', stderr);
}
}
static void Check(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
static void Verify(const std::vector<uint32_t>& words, bool checkBindings = true) {
    std::map<uint32_t,uint32_t> binding, sets, pointers, kinds, vars;
    for (size_t i=5; i<words.size();) {
        uint32_t n=words[i]>>16, op=words[i]&65535;
        Check(n && i+n<=words.size(), "valid SPIR-V instruction lengths");
        const auto* p=&words[i];
        if (op==10 && n>=2) // OpExtension
            Check(std::string(reinterpret_cast<const char*>(p+1)).find("SPV_GOOGLE_hlsl_functionality1") == std::string::npos,
                  "shader requires HLSL functionality extension not enabled by the Vulkan device");
        if (op==71 && n>=4) { // OpDecorate
            if (p[2]==33) binding[p[1]]=p[3];
            if (p[2]==34) sets[p[1]]=p[3];
        }
        if ((op==25 || op==26 || op==27 || op==30) && n>=2) kinds[p[1]]=op; // image, sampler, struct
        if (op==32 && n==4) pointers[p[1]]=p[3]; // OpTypePointer
        if (op==59 && n>=4) { // OpVariable
            vars[p[2]]=p[1];
            const auto kind = kinds[pointers[p[1]]];
            if (p[3]==7) // Function
                Check(kind!=25 && kind!=26 && kind!=27, "opaque texture or sampler spilled to function storage");
        }
        i+=n;
    }
    if (!checkBindings) return;
    std::map<uint32_t,std::set<uint32_t>> seen;
    std::set<uint32_t> slots;
    for (auto [id,slot]:binding) {
        Check(sets.count(id) && sets[id]==0, "all resources use descriptor set zero");
        Check(slots.insert(slot).second, "resource binding collision: shifts were not applied");
        uint32_t kind=kinds[pointers[vars[id]]];
        seen[kind].insert(slot);
    }
    Check(seen[30]==std::set<uint32_t>({0,1}), "cbuffers must occupy b0/b1 => bindings 0/1");
    Check(seen[25]==std::set<uint32_t>({2,5}), "textures t0/t3 must occupy bindings 2/5");
    Check(seen[26]==std::set<uint32_t>({6,9}), "samplers s0/s3 must occupy bindings 6/9");
}
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    gpu::SetSpirvCacheDir(argv[1], "binding-test");
    const std::string source=R"(
cbuffer First : register(b0) { float4 a; };
cbuffer Second : register(b1) { float4 b; };
Texture2D image0 : register(t0);
Texture2D image3 : register(t3);
SamplerState sampler0 : register(s0);
SamplerState sampler3 : register(s3);
float4 main(float2 uv : TEXCOORD0) : SV_Target {
    return image0.Sample(sampler0, uv) + image3.Sample(sampler3, uv) + a + b;
})";
    std::vector<uint32_t> words;
    std::string error;
    Check(gpu::CompileHlslToSpirv(source,"main",true,words,&error), error.c_str());
    Verify(words);
    // Exercise the in-memory cache as well as the compiler output.
    std::vector<uint32_t> cached;
    Check(gpu::CompileHlslToSpirv(source,"main",true,cached,&error) && words==cached, "cache preserves shader ABI");
    Verify(cached);
    // Upscaled render-target reads must sample the global descriptors instead
    // of spilling opaque HLSL arguments to function-local SPIR-V variables.
    gpu::PixelKey wide{};
    wide.tex[0].kind = gpu::kTex2D;
    wide.tex[0].flags = gpu::kTexWide;
    wide.ff[0][0] = 2; // SELECTARG1
    wide.ff[0][1] = 2; // TEXTURE
    wide.ff[0][4] = 2;
    wide.ff[0][5] = 2;
    std::vector<uint32_t> wideWords;
    Check(gpu::CompileHlslToSpirv(gpu::PixelHlsl(wide),"main",true,wideWords,&error), error.c_str());
    Verify(wideWords, false);
    std::cout << "PASS: shader descriptor ABI for uniform buffers, separate images and samplers\n";
}
