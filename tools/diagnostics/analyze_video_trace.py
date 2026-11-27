#!/usr/bin/env python3
"""Summarize a trace and X11 feedback; no game data or pixels are modified."""
import argparse
import collections
import csv
import json
import statistics
from video_frame_order import signature_returns
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--start',type=float,default=1,help='seconds after first acquired video frame')
p.add_argument('--duration',type=float,default=5)
a=p.parse_args()
r=list(csv.DictReader((a.directory/'events.csv').open()))
for x in r:
    for k in ['ns','a','b','c','d']:x[k]=int(x[k])
r.sort(key=lambda x:x['ns'])
first=next(x['ns'] for x in r if x['event']=='frame_info')
start=first+round(a.start*1e9);end=start+round(a.duration*1e9)
s=[x for x in r if start<=x['ns']<end]
def select(event):return [x for x in s if x['event']==event]
def timing(events,field='ns',divisor=1e6):
    intervals=[(y[field]-x[field])/divisor for x,y in zip(events,events[1:])]
    if not intervals:return {'count':len(events)}
    v=sorted(intervals)
    return dict(count=len(events),fps=1000/statistics.mean(v),interval_ms=dict(mean=statistics.mean(v),median=statistics.median(v),p95=v[int((len(v)-1)*.95)],maximum=max(v)))
def costs(event,field):
    v=[x[field]/1e6 for x in select(event)]
    if not v:return None
    v.sort();return dict(count=len(v),mean_ms=statistics.mean(v),median_ms=statistics.median(v),p95_ms=v[int((len(v)-1)*.95)],maximum_ms=max(v),total_ms=sum(v))
out={'directory':str(a.directory),'first_frame_ns':first,'start_ns':start,'end_ns':end,'seconds':a.duration}
out['queue_present']=timing(select('queue_present'))
out['decoded']=timing(sorted(select('decoder_ready')+select('decoder_ready4')+select('decoder_field_ready'),key=lambda x:x['ns']))
out['acquired']=timing(select('frame_info'))
out['mpeg_decode']=costs('mpeg_decode_end','d')
out['convert']=costs('convert_end','c')
for event in ['sample_rt_after','sample_rt_before']:
    images=select(event)
    if not images:continue
    data=collections.Counter(x['b'] for x in images).most_common(1)[0][0]
    images=[x for x in images if x['b']==data]
    fresh=[];holds=[];hold=1
    for x,y in zip(images,images[1:]):
        if x['c']==y['c']:hold+=1
        else:fresh.append(y);holds.append(hold);hold=1
    out['sampled_images']={'source_va':hex(data),'observations':len(images),'new_content_intervals':timing(fresh),
        'repeated':sum(n-1 for n in holds),'holds_in_presents':dict(sorted(collections.Counter(holds).items())),
        'signature_comparison':event,'guest_vs_gpu_source_mismatch':sum(x['c']!=x['d'] for x in images),
        'nonconsecutive_signature_returns':len(signature_returns([x['c'] for x in images])),
        'order_caveat':'Signature returns flag possible stale content, not proof of temporal order. Match real images to the original MPEG display sequence.'}
    break
frames=select('frame_info')
out['frame_rate_milli']=dict(collections.Counter(x['b'] for x in frames))
counts=select('frame_counts')
if counts:out['player_discard_count_delta']=counts[-1]['d']-counts[0]['d']
clock=select('core_clock')
if len(clock)>1 and clock[0]['c'] and clock[0]['c']==clock[-1]['c']:
    x,y=clock[0],clock[-1];out['playback_clock']={'scale':x['c'],'seconds':(y['b']-x['b'])/x['c'],
       'host_seconds':(y['ns']-x['ns'])/1e9,'ratio':(y['b']-x['b'])/x['c']/((y['ns']-x['ns'])/1e9)}
feedback=a.directory/'presentation.csv'
if feedback.exists():
    events=list(csv.DictReader(feedback.open()))
    for x in events:
        for k in ['ns','serial','ust_us','msc','kind','mode']:x[k]=int(x[k])
    events=[x for x in events if start<=x['ust_us']*1000<end and x['kind']==0]
    out['x11_complete']=timing(events,'ust_us',1000)
    out['x11_modes']=dict(collections.Counter(x['mode'] for x in events))
print(json.dumps(out,indent=2))
