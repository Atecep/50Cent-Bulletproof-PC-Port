#!/usr/bin/env python3
"""
Phase 6D.5: Timing & Frame Rate Telemetry Comparison Tool
Compares telemetry runs between different FPS targets (e.g. 30 vs 120 FPS)
at equivalent real-time instants, analyzing:
- Integration rate and draw rate (calls/sec)
- Effective clock progression (guest seconds / real second)
- Particle births and population density
- Kinematics (pos_y, vel_y)
- Lifecycle and normalized phase (age / life)
- Visual attributes (atlas sub-texture index, scale, opacity)
- RNG seed advance rate (distinguishing random variation from temporal acceleration)
"""

import sys
import os
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path

def parse_telemetry(csv_path, target_slot=168, min_time=None, max_time=None):
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Telemetry file not found: {csv_path}")

    records = {
        'integrate': [],
        'draw': [],
        'spawn': [],
        'tick': []
    }

    with open(csv_path, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                t = float(row['timestamp'])
                if min_time is not None and t < min_time:
                    continue
                if max_time is not None and t > max_time:
                    continue

                slot = int(row['slot'])
                if target_slot is not None and slot != target_slot:
                    continue

                call_type = row['call_type']
                if call_type not in records:
                    continue

                entry = {
                    't': t,
                    'frame': int(row['update_num']),
                    'routine': row['routine'],
                    'buf': row['buf'],
                    'slot': slot,
                    'tex_id': row['tex_id'],
                    'idx': int(row['particle_idx']),
                    'dt_in': float(row['dt_in']),
                    'dt_used': float(row['dt_used']),
                    'pos': (float(row['pos_x']), float(row['pos_y']), float(row['pos_z'])),
                    'vel': (float(row['vel_x']), float(row['vel_y']), float(row['vel_z'])),
                    'age': float(row['age']),
                    'life': float(row['life']),
                    'phase': float(row['phase']),
                    'scale': float(row['scale']),
                    'alpha': int(row['alpha']),
                    'tex_idx': int(row['tex_idx']),
                    'rng': int(row['rng_state'], 16) if row['rng_state'].startswith('0x') else int(row['rng_state'])
                }
                records[call_type].append(entry)
            except (ValueError, KeyError, TypeError):
                continue

    return records

def median(lst):
    if not lst: return 0.0
    s = sorted(lst)
    n = len(s)
    mid = n // 2
    return (s[mid] if n % 2 != 0 else (s[mid - 1] + s[mid]) / 2.0)

def mean(lst):
    return sum(lst) / len(lst) if lst else 0.0

def analyze_run(records, label):
    integrates = records['integrate']
    draws = records['draw']
    spawns = records['spawn']
    ticks = records['tick']

    all_t = [r['t'] for r in integrates + draws + spawns if r['t'] > 0]
    if not all_t:
        return {'label': label, 'valid': False}

    t_start = min(all_t)
    t_end = max(all_t)
    duration = t_end - t_start
    if duration <= 0.01:
        return {'label': label, 'valid': False}

    # Integration call frequency and dt
    integ_frames = sorted(list(set(r['frame'] for r in integrates)))
    draw_frames = sorted(list(set(r['frame'] for r in draws)))

    integ_fps = len(integ_frames) / duration if duration > 0 else 0
    draw_fps = len(draw_frames) / duration if duration > 0 else 0

    dts = [r['dt_used'] for r in integrates]
    med_dt = median(dts)

    # Effective clock rate: sum of dt per frame / duration
    # Group by frame to get dt per frame
    frame_dts = {}
    for r in integrates:
        frame_dts[r['frame']] = r['dt_used']
    guest_time_integ = sum(frame_dts.values())
    clock_ratio = guest_time_integ / duration if duration > 0 else 0

    # Spawns / Birth rate
    spawn_indices = set(r['idx'] for r in spawns)
    births_per_sec = len(spawns) / duration if duration > 0 else 0

    # Particle properties across draws
    draw_particles_per_frame = defaultdict(int)
    for r in draws:
        draw_particles_per_frame[r['frame']] += 1
    med_pop = median(list(draw_particles_per_frame.values()))

    vys = [r['vel'][1] for r in draws]
    pys = [r['pos'][1] for r in draws]
    vhs = [math.hypot(r['vel'][0], r['vel'][2]) for r in draws]
    ages = [r['age'] for r in draws]
    lifes = [r['life'] for r in draws]
    phases = [r['phase'] for r in draws]
    scales = [r['scale'] for r in draws]
    alphas = [r['alpha'] for r in draws]
    tex_indices = [r['tex_idx'] for r in draws]

    # Horizontal dispersion at ~1s of age and delta v_h per particle
    particle_history = defaultdict(list)
    for r in integrates:
        particle_history[r['idx']].append(r)

    disp_1s_list = []
    delta_vh_steps = []
    particle_rates = []

    for idx, p_records in particle_history.items():
        if len(p_records) < 2: continue
        p_records.sort(key=lambda x: x['t'])
        p0 = p_records[0]
        # find record at age ~1.0s (0.85 to 1.15s)
        for r in p_records:
            if 0.85 <= r['age'] <= 1.15:
                disp = math.hypot(r['pos'][0] - p0['pos'][0], r['pos'][2] - p0['pos'][2])
                disp_1s_list.append(disp)
                break

        # compute step deltas
        sum_dvh = 0.0
        p_duration = p_records[-1]['t'] - p_records[0]['t']
        for i in range(len(p_records) - 1):
            ra = p_records[i]
            rb = p_records[i+1]
            dvx = rb['vel'][0] - ra['vel'][0]
            dvz = rb['vel'][2] - ra['vel'][2]
            dvh = math.hypot(dvx, dvz)
            delta_vh_steps.append(dvh)
            sum_dvh += dvh
        if p_duration > 0.1:
            particle_rates.append(sum_dvh / p_duration)

    med_disp_1s = median(disp_1s_list) if disp_1s_list else 0.0
    med_delta_vh_step = median(delta_vh_steps) if delta_vh_steps else 0.0
    med_delta_vh_sec = median(particle_rates) if particle_rates else 0.0

    # RNG advancement
    rng_states = [r['rng'] for r in integrates]
    unique_rng = len(set(rng_states))
    rng_adv_per_sec = unique_rng / duration if duration > 0 else 0

    # Atlas frames distribution
    tex_hist = defaultdict(int)
    for idx in tex_indices:
        tex_hist[idx] += 1
    total_tex = len(tex_indices) or 1
    tex_dist = {k: round(v / total_tex * 100.0, 1) for k, v in sorted(tex_hist.items())}

    return {
        'label': label,
        'valid': True,
        'duration': duration,
        'integ_fps': integ_fps,
        'draw_fps': draw_fps,
        'median_dt': med_dt,
        'clock_ratio': clock_ratio,
        'births_per_sec': births_per_sec,
        'unique_particles': len(spawn_indices),
        'median_population': med_pop,
        'med_vy': median(vys),
        'med_vh': median(vhs),
        'med_py': median(pys),
        'med_disp_1s': med_disp_1s,
        'med_delta_vh_step': med_delta_vh_step,
        'med_delta_vh_sec': med_delta_vh_sec,
        'med_age': median(ages),
        'med_life': median(lifes),
        'med_phase': median(phases),
        'med_scale': median(scales),
        'med_alpha': median(alphas),
        'tex_dist': tex_dist,
        'rng_adv_per_sec': rng_adv_per_sec
    }

def print_comparison(stat30, stat120, slot=168):
    print("=" * 82)
    print(f" FASE 6D.6: COMPARAÇÃO DE TELEMETRIA TEMPORAL — EMISSOR {slot}")
    print("=" * 82)
    print(f"{'Propriedade Analisada':<36} | {'30 FPS':<18} | {'120 FPS':<18} | {'Divergência':<8}")
    print("-" * 82)

    def row(name, v30, v120, unit="", fmt=".4f", tol_pct=5.0):
        if isinstance(v30, (int, float)) and isinstance(v120, (int, float)):
            s30 = f"{v30:{fmt}} {unit}".strip()
            s120 = f"{v120:{fmt}} {unit}".strip()
            if v30 != 0:
                diff = abs(v120 - v30) / abs(v30) * 100.0
                div_str = f"{diff:.1f}%" if diff > tol_pct else "0% (OK)"
            else:
                div_str = "N/A"
        else:
            s30 = str(v30)
            s120 = str(v120)
            div_str = "OK" if s30 == s120 else "DIFF"
        print(f"{name:<36} | {s30:<18} | {s120:<18} | {div_str:<8}")

    row("Duração da Amostra", stat30['duration'], stat120['duration'], "s", ".2f")
    row("Frequência Integração (sub_1245F0)", stat30['integ_fps'], stat120['integ_fps'], "Hz", ".2f", 50.0)
    row("Frequência Draw (sub_124FF0)", stat30['draw_fps'], stat120['draw_fps'], "Hz", ".2f", 50.0)
    row("Delta Mediano Consumido", stat30['median_dt'] * 1000.0, stat120['median_dt'] * 1000.0, "ms", ".2f", 50.0)
    row("Rácio Relógio Guest/Real", stat30['clock_ratio'], stat120['clock_ratio'], "", ".4f", 2.0)
    row("Nascimentos de Partículas / s", stat30['births_per_sec'], stat120['births_per_sec'], "part/s", ".2f", 15.0)
    row("População Média no Ecrã", stat30['median_population'], stat120['median_population'], "part", ".1f", 15.0)
    row("Velocidade Vertical Mediana (vy)", stat30['med_vy'], stat120['med_vy'], "u/s", ".3f", 5.0)
    row("Velocidade Horizontal Mediana (vh)", stat30['med_vh'], stat120['med_vh'], "u/s", ".3f", 15.0)
    row("Dispersão Lateral aos ~1s idade", stat30['med_disp_1s'], stat120['med_disp_1s'], "u", ".3f", 15.0)
    row("Delta vh Mediano por Update", stat30['med_delta_vh_step'], stat120['med_delta_vh_step'], "u/update", ".4f", 100.0)
    row("Variação vh por Segundo Real", stat30['med_delta_vh_sec'], stat120['med_delta_vh_sec'], "u/s²", ".3f", 15.0)
    row("Altura Mediana no Mundo (py)", stat30['med_py'], stat120['med_py'], "u", ".3f", 5.0)
    row("Idade Mediana da Partícula", stat30['med_age'], stat120['med_age'], "s", ".3f", 10.0)
    row("Tempo de Vida Configurado", stat30['med_life'], stat120['med_life'], "s", ".3f", 1.0)
    row("Fase Normalizada Mediana", stat30['med_phase'], stat120['med_phase'], "", ".3f", 10.0)
    row("Escala Mediana", stat30['med_scale'], stat120['med_scale'], "", ".3f", 5.0)
    row("Opacidade / Alpha Mediana", stat30['med_alpha'], stat120['med_alpha'], "/255", ".1f", 5.0)
    row("Avanços do RNG por Segundo Real", stat30['rng_adv_per_sec'], stat120['rng_adv_per_sec'], "passos/s", ".1f", 30.0)

    print("-" * 82)
    print(" Distribuição das Células do Atlas (smoke_seq4 frames 0..3):")
    print(f"  30 FPS:  {stat30['tex_dist']}")
    print(f"  120 FPS: {stat120['tex_dist']}")
    print("=" * 82)

def main():
    parser = argparse.ArgumentParser(description="Compara telemetria de 30 e 120 FPS para Fase 6D.5.")
    parser.add_argument("file30", help="Caminho do CSV a 30 FPS")
    parser.add_argument("file120", help="Caminho do CSV a 120 FPS")
    parser.add_argument("--slot", type=int, default=168, help="Slot do emissor (default: 168)")
    parser.add_argument("--min-time", type=float, default=None, help="Tempo mínimo de filtro (s)")
    parser.add_argument("--max-time", type=float, default=None, help="Tempo máximo de filtro (s)")
    parser.add_argument("--min-time-30", type=float, default=None, help="Tempo mínimo de filtro para 30 FPS (s)")
    parser.add_argument("--max-time-30", type=float, default=None, help="Tempo máximo de filtro para 30 FPS (s)")
    parser.add_argument("--min-time-120", type=float, default=None, help="Tempo mínimo de filtro para 120 FPS (s)")
    parser.add_argument("--max-time-120", type=float, default=None, help="Tempo máximo de filtro para 120 FPS (s)")
    args = parser.parse_args()

    min_t30 = args.min_time_30 if args.min_time_30 is not None else args.min_time
    max_t30 = args.max_time_30 if args.max_time_30 is not None else args.max_time
    min_t120 = args.min_time_120 if args.min_time_120 is not None else args.min_time
    max_t120 = args.max_time_120 if args.max_time_120 is not None else args.max_time

    rec30 = parse_telemetry(args.file30, target_slot=args.slot, min_time=min_t30, max_time=max_t30)
    rec120 = parse_telemetry(args.file120, target_slot=args.slot, min_time=min_t120, max_time=max_t120)

    stat30 = analyze_run(rec30, "30 FPS")
    stat120 = analyze_run(rec120, "120 FPS")

    if not stat30['valid'] or not stat120['valid']:
        print("Erro: dados insuficientes num dos ficheiros de telemetria.", file=sys.stderr)
        sys.exit(1)

    print_comparison(stat30, stat120, slot=args.slot)

if __name__ == '__main__':
    main()
