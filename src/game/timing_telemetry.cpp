#include "timing_telemetry.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

// Phase 6D.5: Reusable Timing & Frame Rate Diagnostic Telemetry
// Collects granular telemetry on particle emitters, routines, and game state
// for frame-rate independence auditing across 30, 60, 90, and 120 FPS.

namespace {

struct TelemetryState {
    bool initialized = false;
    bool enabled = false;
    int target_slot = 168; // Emitter 168 (smoke_seq4, ground manhole steam)
    uint32_t target_tex_id = 0; // 0 = any, or 0x0000023D
    uint32_t max_records = 100000;
    uint32_t recorded_count = 0;
    uint64_t update_counter = 0;
    std::FILE* out_file = nullptr;
    std::chrono::steady_clock::time_point start_time;
};

static TelemetryState s_state;

void EnsureInit() {
    if (__builtin_expect(s_state.initialized, 1)) return;
    s_state.initialized = true;

    const char* env_en = std::getenv("BP_TELEMETRY_ENABLED");
    if (!env_en || (std::strcmp(env_en, "1") != 0 && std::strcmp(env_en, "true") != 0)) {
        s_state.enabled = false;
        return;
    }

    s_state.enabled = true;
    s_state.start_time = std::chrono::steady_clock::now();

    const char* env_slot = std::getenv("BP_TELEMETRY_SLOT");
    if (env_slot) {
        s_state.target_slot = std::atoi(env_slot);
    }

    const char* env_tex = std::getenv("BP_TELEMETRY_TEX_ID");
    if (env_tex) {
        s_state.target_tex_id = static_cast<uint32_t>(std::strtoul(env_tex, nullptr, 0));
    }

    const char* env_max = std::getenv("BP_TELEMETRY_MAX");
    if (env_max) {
        s_state.max_records = static_cast<uint32_t>(std::strtoul(env_max, nullptr, 10));
    }

    const char* env_log = std::getenv("BP_TELEMETRY_LOG");
    std::string log_path = env_log ? env_log : "userdata/logs/timing_telemetry.csv";

    s_state.out_file = std::fopen(log_path.c_str(), "w");
    if (s_state.out_file) {
        std::fprintf(s_state.out_file,
            "timestamp,update_num,call_type,routine,buf,slot,tex_id,particle_idx,"
            "dt_in,dt_used,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z,age,life,phase,"
            "scale,alpha,tex_idx,rng_state,ax,ay,az,desc_flags,drag_xz,drag_y,elev_offset\n");
        std::fflush(s_state.out_file);
    } else {
        s_state.enabled = false;
    }
}

double GetElapsedSeconds() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = now - s_state.start_time;
    return diff.count();
}

} // namespace

namespace timing {

bool IsTelemetryEnabled() {
    if (__builtin_expect(!s_state.initialized, 0)) {
        EnsureInit();
    }
    return s_state.enabled;
}

void OnParticleIntegrate(uint32_t buf, float dt_in, float dt_used, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;

    const uint16_t slots = *reinterpret_cast<const uint16_t*>(base + buf + 0x08);
    const uint32_t desc_base = *reinterpret_cast<const uint32_t*>(base + buf + 0x04);
    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || slots == 0 || slots > 2000) return;

    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = ++s_state.update_counter;
    const uint32_t rng_state = *reinterpret_cast<const uint32_t*>(base + 0x00342AA4);

    const int start_slot = (s_state.target_slot >= 0) ? s_state.target_slot : 0;
    const int end_slot = (s_state.target_slot >= 0) ? (s_state.target_slot + 1) : slots;

    for (int s = start_slot; s < end_slot && s < slots; ++s) {
        const uint32_t desc = desc_base + s * 0x1B0;
        const uint32_t tex_id = *reinterpret_cast<const uint32_t*>(base + desc + 0x128);
        if (s_state.target_tex_id != 0 && tex_id != s_state.target_tex_id) continue;

        const float ax = *reinterpret_cast<const float*>(base + desc + 0x40);
        const float ay = *reinterpret_cast<const float*>(base + desc + 0x44);
        const float az = *reinterpret_cast<const float*>(base + desc + 0x48);
        const uint16_t desc_flags = *reinterpret_cast<const uint16_t*>(base + desc + 0x120);

        uint16_t idx = *reinterpret_cast<const uint16_t*>(base + buf + 0x14 + s * 2);
        for (uint32_t depth = 0; depth < 1000 && idx != 0xFFFF; ++depth) {
            const uint32_t p = pool + idx * 0x60;
            const float px = *reinterpret_cast<const float*>(base + p + 0x10);
            const float py = *reinterpret_cast<const float*>(base + p + 0x14);
            const float pz = *reinterpret_cast<const float*>(base + p + 0x18);
            const float base_sz = *reinterpret_cast<const float*>(base + p + 0x1C);
            const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
            const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
            const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
            const float sz_mult = *reinterpret_cast<const float*>(base + p + 0x2C);
            const float age = *reinterpret_cast<const float*>(base + p + 0x34);
            const float life = *reinterpret_cast<const float*>(base + p + 0x38);
            const float drag_xz = *reinterpret_cast<const float*>(base + p + 0x50);
            const float drag_y = *reinterpret_cast<const float*>(base + p + 0x54);
            const int16_t elev_offset = *reinterpret_cast<const int16_t*>(base + p + 0x0E);
            const uint8_t p_tex = *reinterpret_cast<const uint8_t*>(base + p + 0x07);
            const uint8_t p_alpha = *reinterpret_cast<const uint8_t*>(base + p + 0x0B);

            const float phase = (life > 0.0001f) ? (age / life) : 0.0f;
            const float scale = base_sz * sz_mult;

            if (s_state.out_file) {
                std::fprintf(s_state.out_file,
                    "%.6f,%llu,integrate,sub_001245F0,0x%08X,%d,0x%08X,%u,"
                    "%.6f,%.6f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
                    "%.5f,%u,%u,0x%08X,%.5f,%.5f,%.5f,0x%04X,%.5f,%.5f,%d\n",
                    t_real, (unsigned long long)frame_num, buf, s, tex_id, idx,
                    dt_in, dt_used, px, py, pz, vx, vy, vz, age, life, phase,
                    scale, (unsigned)p_alpha, (unsigned)p_tex, rng_state,
                    ax, ay, az, (unsigned)desc_flags, drag_xz, drag_y, (int)elev_offset);
                ++s_state.recorded_count;
                if (s_state.recorded_count >= s_state.max_records) break;
            }

            idx = *reinterpret_cast<const uint16_t*>(base + p + 0x02);
        }
    }
}

void OnParticleIntegratePost(uint32_t buf, float dt_used, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;

    const uint16_t slots = *reinterpret_cast<const uint16_t*>(base + buf + 0x08);
    const uint32_t desc_base = *reinterpret_cast<const uint32_t*>(base + buf + 0x04);
    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || slots == 0 || slots > 2000) return;

    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;
    const uint32_t rng_state = *reinterpret_cast<const uint32_t*>(base + 0x00342AA4);

    const int start_slot = (s_state.target_slot >= 0) ? s_state.target_slot : 0;
    const int end_slot = (s_state.target_slot >= 0) ? (s_state.target_slot + 1) : slots;

    for (int s = start_slot; s < end_slot && s < slots; ++s) {
        const uint32_t desc = desc_base + s * 0x1B0;
        const uint32_t tex_id = *reinterpret_cast<const uint32_t*>(base + desc + 0x128);
        if (s_state.target_tex_id != 0 && tex_id != s_state.target_tex_id) continue;

        const float ax = *reinterpret_cast<const float*>(base + desc + 0x40);
        const float ay = *reinterpret_cast<const float*>(base + desc + 0x44);
        const float az = *reinterpret_cast<const float*>(base + desc + 0x48);
        const uint16_t desc_flags = *reinterpret_cast<const uint16_t*>(base + desc + 0x120);

        uint16_t idx = *reinterpret_cast<const uint16_t*>(base + buf + 0x14 + s * 2);
        for (uint32_t depth = 0; depth < 1000 && idx != 0xFFFF; ++depth) {
            const uint32_t p = pool + idx * 0x60;
            const float px = *reinterpret_cast<const float*>(base + p + 0x10);
            const float py = *reinterpret_cast<const float*>(base + p + 0x14);
            const float pz = *reinterpret_cast<const float*>(base + p + 0x18);
            const float base_sz = *reinterpret_cast<const float*>(base + p + 0x1C);
            const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
            const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
            const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
            const float sz_mult = *reinterpret_cast<const float*>(base + p + 0x2C);
            const float age = *reinterpret_cast<const float*>(base + p + 0x34);
            const float life = *reinterpret_cast<const float*>(base + p + 0x38);
            const float drag_xz = *reinterpret_cast<const float*>(base + p + 0x50);
            const float drag_y = *reinterpret_cast<const float*>(base + p + 0x54);
            const int16_t elev_offset = *reinterpret_cast<const int16_t*>(base + p + 0x0E);
            const uint8_t p_tex = *reinterpret_cast<const uint8_t*>(base + p + 0x07);
            const uint8_t p_alpha = *reinterpret_cast<const uint8_t*>(base + p + 0x0B);

            const float phase = (life > 0.0001f) ? (age / life) : 0.0f;
            const float scale = base_sz * sz_mult;

            if (s_state.out_file) {
                std::fprintf(s_state.out_file,
                    "%.6f,%llu,post_integrate,sub_001245F0_post,0x%08X,%d,0x%08X,%u,"
                    "%.6f,%.6f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
                    "%.5f,%u,%u,0x%08X,%.5f,%.5f,%.5f,0x%04X,%.5f,%.5f,%d\n",
                    t_real, (unsigned long long)frame_num, buf, s, tex_id, idx,
                    dt_used, dt_used, px, py, pz, vx, vy, vz, age, life, phase,
                    scale, (unsigned)p_alpha, (unsigned)p_tex, rng_state,
                    ax, ay, az, (unsigned)desc_flags, drag_xz, drag_y, (int)elev_offset);
                ++s_state.recorded_count;
                if (s_state.recorded_count >= s_state.max_records) break;
            }

            idx = *reinterpret_cast<const uint16_t*>(base + p + 0x02);
        }
    }
}

void OnEmitterForceFieldBefore(uint32_t buf, uint16_t first_p, float dt, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;
    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || first_p == 0xFFFF) return;
    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;

    uint16_t idx = first_p;
    for (uint32_t depth = 0; depth < 1000 && idx != 0xFFFF; ++depth) {
        const uint32_t p = pool + idx * 0x60;
        const uint8_t slot = *reinterpret_cast<const uint8_t*>(base + p + 0x05);
        if (s_state.target_slot < 0 || slot == static_cast<uint8_t>(s_state.target_slot)) {
            const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
            const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
            const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
            if (s_state.out_file) {
                std::fprintf(s_state.out_file,
                    "%.6f,%llu,field_before,sub_00124090,0x%08X,%u,0x0,%u,"
                    "%.6f,%.6f,0.0,0.0,0.0,%.5f,%.5f,%.5f,0.0,0.0,0.0,"
                    "0.0,0,0,0x0,0.0,0.0,0.0,0x0,0.0,0.0,0\n",
                    t_real, (unsigned long long)frame_num, buf, (unsigned)slot, idx,
                    dt, dt, vx, vy, vz);
                ++s_state.recorded_count;
                if (s_state.recorded_count >= s_state.max_records) break;
            }
        }
        idx = *reinterpret_cast<const uint16_t*>(base + p + 0x3E);
        if (idx == first_p) break;
    }
}

void OnEmitterForceFieldAfter(uint32_t buf, uint16_t first_p, float dt, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;
    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || first_p == 0xFFFF) return;
    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;

    uint16_t idx = first_p;
    for (uint32_t depth = 0; depth < 1000 && idx != 0xFFFF; ++depth) {
        const uint32_t p = pool + idx * 0x60;
        const uint8_t slot = *reinterpret_cast<const uint8_t*>(base + p + 0x05);
        if (s_state.target_slot < 0 || slot == static_cast<uint8_t>(s_state.target_slot)) {
            const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
            const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
            const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
            if (s_state.out_file) {
                std::fprintf(s_state.out_file,
                    "%.6f,%llu,field_after,sub_00124090,0x%08X,%u,0x0,%u,"
                    "%.6f,%.6f,0.0,0.0,0.0,%.5f,%.5f,%.5f,0.0,0.0,0.0,"
                    "0.0,0,0,0x0,0.0,0.0,0.0,0x0,0.0,0.0,0\n",
                    t_real, (unsigned long long)frame_num, buf, (unsigned)slot, idx,
                    dt, dt, vx, vy, vz);
                ++s_state.recorded_count;
                if (s_state.recorded_count >= s_state.max_records) break;
            }
        }
        idx = *reinterpret_cast<const uint16_t*>(base + p + 0x3E);
        if (idx == first_p) break;
    }
}

void OnParticleDraw(uint32_t buf, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;

    const uint16_t slots = *reinterpret_cast<const uint16_t*>(base + buf + 0x08);
    const uint32_t desc_base = *reinterpret_cast<const uint32_t*>(base + buf + 0x04);
    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || slots == 0 || slots > 2000) return;

    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;
    const uint32_t rng_state = *reinterpret_cast<const uint32_t*>(base + 0x00342AA4);
    const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);

    const int start_slot = (s_state.target_slot >= 0) ? s_state.target_slot : 0;
    const int end_slot = (s_state.target_slot >= 0) ? (s_state.target_slot + 1) : slots;

    for (int s = start_slot; s < end_slot && s < slots; ++s) {
        const uint32_t desc = desc_base + s * 0x1B0;
        const uint32_t tex_id = *reinterpret_cast<const uint32_t*>(base + desc + 0x128);
        if (s_state.target_tex_id != 0 && tex_id != s_state.target_tex_id) continue;

        const uint8_t anim_mode = *reinterpret_cast<const uint8_t*>(base + desc + 0xB7);

        uint16_t idx = *reinterpret_cast<const uint16_t*>(base + buf + 0x14 + s * 2);
        for (uint32_t depth = 0; depth < 1000 && idx != 0xFFFF; ++depth) {
            const uint32_t p = pool + idx * 0x60;
            const float delay = *reinterpret_cast<const float*>(base + p + 0x30);
            if (delay <= 0.0f) {
                const float px = *reinterpret_cast<const float*>(base + p + 0x10);
                const float py = *reinterpret_cast<const float*>(base + p + 0x14);
                const float pz = *reinterpret_cast<const float*>(base + p + 0x18);
                const float base_sz = *reinterpret_cast<const float*>(base + p + 0x1C);
                const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
                const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
                const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
                const float sz_mult = *reinterpret_cast<const float*>(base + p + 0x2C);
                const float age = *reinterpret_cast<const float*>(base + p + 0x34);
                const float life = *reinterpret_cast<const float*>(base + p + 0x38);
                const uint8_t p_tex = *reinterpret_cast<const uint8_t*>(base + p + 0x07);
                const uint8_t p_alpha = *reinterpret_cast<const uint8_t*>(base + p + 0x0B);

                float phase = (life > 0.0001f) ? (age / life) : 0.0f;
                if (phase < 0.0f) phase = 0.0f;
                else if (phase > 1.0f) phase = 1.0f;

                uint8_t atlas_frame = p_tex;
                if (anim_mode == 3) {
                    atlas_frame = static_cast<uint8_t>(phase * 3.9f);
                }

                const float scale = base_sz * sz_mult;

                if (s_state.out_file) {
                    std::fprintf(s_state.out_file,
                        "%.6f,%llu,draw,sub_00124FF0,0x%08X,%d,0x%08X,%u,"
                        "%.6f,%.6f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
                        "%.5f,%u,%u,0x%08X,0.0,0.0,0.0,0x0000,0.0,0.0,0\n",
                        t_real, (unsigned long long)frame_num, buf, s, tex_id, idx,
                        eff_dt, eff_dt, px, py, pz, vx, vy, vz, age, life, phase,
                        scale, (unsigned)p_alpha, (unsigned)atlas_frame, rng_state);
                    ++s_state.recorded_count;
                    if (s_state.recorded_count >= s_state.max_records) break;
                }
            }

            idx = *reinterpret_cast<const uint16_t*>(base + p + 0x02);
        }
    }
}

void OnParticleSpawn(uint32_t buf, uint16_t idx, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !buf || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;

    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (!pool || idx == 0xFFFF) return;

    const uint32_t p = pool + idx * 0x60;
    const uint8_t slot = *reinterpret_cast<const uint8_t*>(base + p + 0x05);
    if (s_state.target_slot >= 0 && slot != static_cast<uint8_t>(s_state.target_slot)) return;

    const uint32_t desc_base = *reinterpret_cast<const uint32_t*>(base + buf + 0x04);
    const uint32_t desc = desc_base + slot * 0x1B0;
    const uint32_t tex_id = *reinterpret_cast<const uint32_t*>(base + desc + 0x128);
    if (s_state.target_tex_id != 0 && tex_id != s_state.target_tex_id) return;

    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;
    const uint32_t rng_state = *reinterpret_cast<const uint32_t*>(base + 0x00342AA4);

    const float px = *reinterpret_cast<const float*>(base + p + 0x10);
    const float py = *reinterpret_cast<const float*>(base + p + 0x14);
    const float pz = *reinterpret_cast<const float*>(base + p + 0x18);
    const float base_sz = *reinterpret_cast<const float*>(base + p + 0x1C);
    const float vx = *reinterpret_cast<const float*>(base + p + 0x20);
    const float vy = *reinterpret_cast<const float*>(base + p + 0x24);
    const float vz = *reinterpret_cast<const float*>(base + p + 0x28);
    const float sz_mult = *reinterpret_cast<const float*>(base + p + 0x2C);
    const float age = *reinterpret_cast<const float*>(base + p + 0x34);
    const float life = *reinterpret_cast<const float*>(base + p + 0x38);
    const uint8_t p_tex = *reinterpret_cast<const uint8_t*>(base + p + 0x07);
    const uint8_t p_alpha = *reinterpret_cast<const uint8_t*>(base + p + 0x0B);

    if (s_state.out_file) {
        std::fprintf(s_state.out_file,
            "%.6f,%llu,spawn,sub_00124090,0x%08X,%u,0x%08X,%u,"
            "0.0,0.0,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,0.0,"
            "%.5f,%u,%u,0x%08X,0.0,0.0,0.0,0x0000,0.0,0.0,0\n",
            t_real, (unsigned long long)frame_num, buf, (unsigned)slot, tex_id, idx,
            px, py, pz, vx, vy, vz, age, life,
            base_sz * sz_mult, (unsigned)p_alpha, (unsigned)p_tex, rng_state);
        ++s_state.recorded_count;
    }
}

void OnEmitterTick(uint32_t emitter_ptr, float dt_in, uint8_t* base) {
    if (__builtin_expect(!IsTelemetryEnabled() || !emitter_ptr || !base, 1)) return;
    if (s_state.recorded_count >= s_state.max_records) return;

    const uint16_t slot = *reinterpret_cast<const uint16_t*>(base + emitter_ptr + 0x3DC);
    if (s_state.target_slot >= 0 && slot != 0xFFFF && slot != static_cast<uint16_t>(s_state.target_slot)) return;

    const double t_real = GetElapsedSeconds();
    const uint64_t frame_num = s_state.update_counter;
    const uint32_t rng_state = *reinterpret_cast<const uint32_t*>(base + 0x00342AA4);
    const float cooldown = *reinterpret_cast<const float*>(base + emitter_ptr + 0x270);

    if (s_state.out_file) {
        std::fprintf(s_state.out_file,
            "%.6f,%llu,tick,sub_0003A0A0,0x%08X,%d,0,0,"
            "%.6f,%.6f,0.0,%.5f,0.0,0.0,0.0,0.0,0.0,0.0,0.0,"
            "0.0,0,0,0x%08X,0.0,0.0,0.0,0x0000,0.0,0.0,0\n",
            t_real, (unsigned long long)frame_num, emitter_ptr, (slot != 0xFFFF ? (int)slot : -1),
            dt_in, dt_in, cooldown, rng_state);
        ++s_state.recorded_count;
    }
}

} // namespace timing

// Hook sub_00124FF0: Particle render/draw dispatch
extern "C" __attribute__((weak)) void __imp__sub_00124FF0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00124FF0) {
    if (timing::IsTelemetryEnabled()) {
        timing::OnParticleDraw(ctx.ecx, base);
    }
    if (__imp__sub_00124FF0) {
        __imp__sub_00124FF0(ctx, base);
    } else {
        ctx.esp += 4;
    }
}

// Hook sub_0003A0A0: Emitter update / spawn cooldown tick
extern "C" __attribute__((weak)) void __imp__sub_0003A0A0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0003A0A0) {
    if (timing::IsTelemetryEnabled()) {
        const float dt_in = *reinterpret_cast<const float*>(base + ctx.esp + 4);
        timing::OnEmitterTick(ctx.ecx, dt_in, base);
    }
    if (__imp__sub_0003A0A0) {
        __imp__sub_0003A0A0(ctx, base);
    } else {
        ctx.esp += 8;
    }
}
