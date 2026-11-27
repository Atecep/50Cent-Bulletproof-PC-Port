#!/usr/bin/env python3
"""Inspect XBE_AUDIO_CAPTURE files and the real PipeWire monitor recording.

Requires NumPy and FFmpeg. Raw captures are stereo float32 LE, 48000 Hz.
A correlated monitor is evidence of delivery, not proof of correct guest PCM.
Optional reference movies compare complete, isolated voice playback intervals.
"""
import argparse
import csv
import json
import subprocess
from pathlib import Path
import numpy as np
RATE = 48000


def pcm(path):
    if path.suffix == '.f32':
        raw = path.read_bytes()
    else:
        raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path),
            '-ar', str(RATE), '-ac', '2', '-f', 'f32le', '-'])
    return np.frombuffer(raw, '<f4').reshape(-1, 2)


def statistics(a):
    finite = np.isfinite(a)
    if not finite.all():
        return dict(frames=len(a), nonfinite=int((~finite).sum()))
    delta = np.diff(a, axis=0)
    return dict(frames=len(a), seconds=len(a)/RATE, nonfinite=0,
                peak=float(abs(a).max(initial=0)), rms=float(np.sqrt(np.mean(a.astype(float)**2))),
                dc=[float(v) for v in a.mean(axis=0)],
                at_or_above_full_scale=int((abs(a)>=1).sum()),
                large_steps=int((abs(delta)>.5).sum()),
                max_step=float(abs(delta).max(initial=0)),
                render_boundary_large_steps=int((abs(delta[479::480])>.5).sum()))


def correlation(a, b):
    # Linear FFT correlation with zero padding; no SciPy dependency.
    n = 1 << (len(a)+len(b)-2).bit_length()
    c = np.fft.irfft(np.fft.rfft(a, n)*np.conj(np.fft.rfft(b, n)), n)
    return np.concatenate((c[n-len(b)+1:], c[:len(a)]))


def difference(observed, reference):
    n = min(len(observed), len(reference))
    a, b = observed[:n].astype(float), reference[:n].astype(float)
    ea, eb = np.sum(a*a), np.sum(b*b)
    if ea == 0 or eb == 0:
        return dict(frames=n, observed_silent=bool(ea==0), reference_silent=bool(eb==0),
                    corr=None, gain=None, snr_db=None)
    gain = float(np.sum(a*b)/eb)
    error = float(np.mean((a-gain*b)**2))
    return dict(frames=n, corr=float(np.corrcoef(a.flatten(), b.flatten())[0,1]),
                gain=gain, error_rms=error**.5,
                snr_db=float(10*np.log10(np.mean((gain*b)**2)/error)) if error else None)


def rows(path):
    if not path.exists(): return []
    # A bounded/terminated diagnostic run can end with an incomplete CSV line.
    return [r for r in csv.DictReader(path.open()) if r.get('data') is not None]


def delivery_windows(mixed, monitor):
    """Allow local latency to vary, exposing device gaps hidden by global NCC."""
    result = []
    for second in range(6, int(len(mixed)/RATE)-1, 2):
        x = mixed[second*RATE:second*RATE+RATE//4,0].astype(float)
        x -= x.mean()
        if np.sqrt(np.mean(x*x)) < .001: continue
        lo = max(0,(second-1)*RATE)
        y = monitor[lo:(second+2)*RATE,0].astype(float)
        if len(y) < len(x): continue
        c = correlation(y,x)[len(x)-1:len(y)]
        energy, total = np.r_[0,np.cumsum(y*y)], np.r_[0,np.cumsum(y)]
        e = energy[len(x):]-energy[:-len(x)]-(total[len(x):]-total[:-len(x)])**2/len(x)
        score = c/np.sqrt(np.maximum(e,1e-25)*np.dot(x,x))
        at = int(score.argmax())
        result.append(dict(second=second, lag_frames=lo+at-second*RATE, corr=float(score[at])))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--references', type=Path, help='original SFD directory; assumes Sierra/GGLogo/Dolby/Inter intro order')
    p.add_argument('--decoder-channel0', type=Path, help='independent, pre-DirectSound decoded PCM .npy, channel0')
    p.add_argument('--ring-frames', type=int, default=16384)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args(); folder = a.directory/'pcm'
    mixed, before, monitor = pcm(folder/'mixer.f32'), pcm(folder/'premix.f32'), pcm(a.directory/'pipewire.wav')
    report = dict(mixer=statistics(mixed), premix=statistics(before), monitor=statistics(monitor),
                  clamp_changed_samples=int((mixed!=before).sum()))
    c = correlation(monitor[:,0], mixed[:,0]); lag = int(c.argmax())-len(mixed)+1
    observed = monitor[max(0,lag):min(len(monitor), len(mixed)+lag)]
    reference = mixed[max(0,-lag):min(len(mixed),len(monitor)-lag)]
    report['monitor_delivery'] = difference(observed,reference)
    report['monitor_delivery']['lag_frames'] = lag
    windows = delivery_windows(mixed,monitor)
    report['monitor_windows'] = windows
    report['monitor_latency_changes'] = [dict(second=b['second'],
        delta_frames=b['lag_frames']-old['lag_frames']) for old,b in zip(windows,windows[1:])
        if b['lag_frames']!=old['lag_frames']]
    events = rows(folder/'events.csv'); pulls = [r for r in events if r['event']=='pull']
    intervals = np.diff([int(r['ns']) for r in pulls])/1e6
    report['pulls'] = dict(count=len(pulls), underfilled=sum(int(r['obj'])>int(r['data']) for r in pulls),
        median_interval_ms=float(np.median(intervals)), max_interval_ms=float(max(intervals)))
    voices = [r for r in events if r['event']=='voice' and r.get('pos') is not None]
    report['formats'] = sorted(set(tuple(r[k] for k in ('tag','bits','channels','align','rate','freq','output')) for r in voices))
    if voices:
        # Each uninterrupted intro interval has one active voice. Refuse to
        # pretend this grouping establishes a multi-voice menu mix reference.
        groups = []
        for r in voices:
            if not groups or int(r['frame'])!=int(groups[-1][-1]['frame'])+int(groups[-1][-1]['count']): groups.append([])
            groups[-1].append(r)
        first = groups[0]; source = pcm(folder/f'voice-{first[0]["obj"]}.f32')
        report['source'] = statistics(source)
        start = int(first[0]['frame'])
        if a.decoder_channel0:
            ref = np.load(a.decoder_channel0)
            n = min(len(ref)-a.ring_frames, len(source)-start)
            current = source[start:start+n,0] == ref[:n]
            future = source[start:start+n,0] == ref[a.ring_frames:a.ring_frames+n]
            report['decoder_identity'] = dict(samples=n, current_fraction=float(current.mean()),
                future_only_fraction=float((future & ~current).mean()),
                unexplained_fraction=float((~current & ~future).mean()))
        if a.references:
            report['original_audio'] = {}
            for name, group in zip(('sierra','gglogo','dolbyn','inter'), groups):
                src = pcm(folder/f'voice-{group[0]["obj"]}.f32')
                begin, end = int(group[0]['frame']), int(group[-1]['frame'])+int(group[-1]['count'])
                report['original_audio'][name] = difference(src[begin:end],pcm(a.references/(name+'.sfd')))
    controls_path = folder/'controls.csv'
    if controls_path.exists() and voices:
        controls = [r for r in csv.DictReader(controls_path.open()) if r.get('reported_pos') is not None]
        locks = [r for r in controls if r['event']=='lock' and r['obj']==voices[0]['obj'] and float(r['render_pos'])>0]
        report['next_unread_overwrites'] = dict(locks=len(locks), overlaps=sum(
            (int(float(r['render_pos']))-int(float(r['reported_pos']))//4)%a.ring_frames < int(r['value'])//4
            for r in locks))
    report['limits'] = 'Monitor capture excludes the analogue device/headphones. Correlation of delivery alone does not establish fidelity. Reference comparison must isolate the right clip, voice and rate; silent regions cannot establish fidelity.'
    a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k in ('monitor_delivery','decoder_identity','next_unread_overwrites')},indent=2))

if __name__ == '__main__': main()
