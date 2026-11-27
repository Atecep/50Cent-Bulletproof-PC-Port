#!/usr/bin/env python3
"""Match real captured images against MPEG display-order reference images.

Requires ffmpeg, numpy. No monotonic constraint is imposed on the matcher: a
backward best match must remain visible. Ambiguous/static frames are excluded
from temporal assertions, not assigned an invented frame number.
"""
import argparse
import json
import subprocess
from pathlib import Path


def reversals(indices):
    return [(i, a, b) for i, (a, b) in enumerate(zip(indices, indices[1:]), 1) if b < a]


def signature_returns(signatures):
    seen = set()
    previous = None
    returns = []
    for i, signature in enumerate(signatures):
        if signature != previous and signature in seen:
            returns.append(i)
        seen.add(signature)
        previous = signature
    return returns


def decode(path, crop=None, start=0., duration=None):
    import numpy as np
    filters = ([f'crop={crop}'] if crop else []) + ['scale=96:72']
    interval = ['-ss', str(start)] if start else []
    length = ['-t', str(duration)] if duration else []
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', *interval, '-i', str(path), *length,
                                  '-map', '0:v:0', '-vf', ','.join(filters),
                                  '-pix_fmt', 'gray', '-threads', '1', '-f', 'rawvideo', '-'])
    return np.frombuffer(raw, np.uint8).reshape(-1, 72, 96)


def match(observed, reference, max_error=8., margin=1.):
    import numpy as np
    ref = reference.astype(np.float32)
    rows = []
    for i, image in enumerate(observed):
        costs = np.abs(ref - image.astype(np.float32)).mean(axis=(1, 2))
        top = np.argsort(costs)[:2]
        best, second = map(int, top)
        rows.append(dict(capture_index=i, reference_index=best, error=float(costs[best]),
                         margin=float(costs[second]-costs[best]),
                         confident=bool(costs[best]<=max_error and costs[second]-costs[best]>=margin)))
    confident = [r for r in rows if r['confident']]
    backward = []
    for a, b in zip(confident, confident[1:]):
        if b['reference_index'] < a['reference_index']:
            backward.append(dict(previous=a, current=b))
    return dict(observations=len(rows), confident=len(confident),
                reversal_count=len(backward), reversals=backward, frames=rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('recording', type=Path)
    p.add_argument('reference', type=Path, help='original clip, decoded by FFmpeg in display order')
    p.add_argument('--crop', help='w:h:x:y, exclude real window borders/letterbox if present')
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--max-error', type=float, default=8.)
    p.add_argument('--margin', type=float, default=1.)
    p.add_argument('--start', type=float, default=0., help='recording seconds; isolate this clip from other movies')
    p.add_argument('--duration', type=float, help='recording interval length in seconds')
    p.add_argument('--check', action='store_true', help='fail on reversals or insufficient confident observations')
    p.add_argument('--min-confident', type=int, default=1)
    a = p.parse_args()
    result = match(decode(a.recording,a.crop,a.start,a.duration),decode(a.reference),a.max_error,a.margin)
    metadata = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_frames',
        '-show_entries', 'frame=pts_time,best_effort_timestamp_time,pict_type',
        '-of', 'json', str(a.reference)]))['frames']
    for row in result['frames']:
        meta = metadata[row['reference_index']]
        row['reference_pts_seconds'] = meta.get('pts_time', meta.get('best_effort_timestamp_time'))
        row['picture_type'] = meta.get('pict_type')
    result.update(recording=str(a.recording), reference=str(a.reference), crop=a.crop,
                  start_seconds=a.start, duration_seconds=a.duration,
                  caveat='Only confident reference matches assert order; static/ambiguous/unmatched images are inconclusive.')
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('frames','reversals')},indent=2))
    if a.check and (result['reversal_count'] or result['confident'] < a.min_confident):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
