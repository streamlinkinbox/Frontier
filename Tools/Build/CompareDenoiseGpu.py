#!/usr/bin/env python3
"""Compare real TelemetryProbe CSVs. Does not run a renderer or manufacture GPU timings."""
import argparse, csv, math, statistics
from pathlib import Path


def load(path, warmup):
    with Path(path).open(newline='') as f:
        rows=list(csv.DictReader(f))
    required={'GpuValid','GpuRestirMs','GpuPostMs'}
    if not rows or not required.issubset(rows[0]):
        raise ValueError(f'{path}: missing GPU telemetry columns')
    valid=[]
    for row in rows[warmup:]:
        if row['GpuValid']!='1':continue
        values={k:float(v) for k,v in row.items() if k.startswith('Gpu') and k.endswith('Ms')}
        if not all(math.isfinite(v) and v>=0 for v in values.values()):
            raise ValueError(f'{path}: invalid GPU measurements')
        if values['GpuRestirMs']<=0:continue # do not mix debug/map-only frames into compute-path tests
        values['GpuPostPlusSnapshotMs']=values['GpuPostMs']+values.get('GpuHistorySnapshotMs',0)
        levels=[f'GpuDenoiseL{i}Ms' for i in range(5)]
        if all(k in values for k in levels):values['GpuDenoiseLevelSumMs']=sum(values[k] for k in levels)
        valid.append(values)
    if len(valid)<30:raise ValueError(f'{path}: only {len(valid)} usable frames after warm-up; need 30 or more')
    return valid


def stats(rows,key):
    if any(key not in row for row in rows):return None
    values=sorted(row[key] for row in rows)
    return statistics.median(values),values[math.ceil(.95*len(values))-1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline');p.add_argument('candidate');p.add_argument('--warmup',type=int,default=120)
    a=p.parse_args()
    if a.warmup<0:p.error('warmup cannot be negative')
    try:base=load(a.baseline,a.warmup);cand=load(a.candidate,a.warmup)
    except (ValueError,OSError) as e:p.error(str(e))
    print(f'# GPU timing comparison\n\nUsable compute-path frames: baseline {len(base)}, candidate {len(cand)}.')
    print('Match GPU, scene, resolution, quality, camera path, exposure and denoiser state externally. This tool cannot establish visual equivalence.\n')
    print('| Metric | Baseline median / p95 ms | Candidate median / p95 ms | Median delta |\n|---|---:|---:|---:|')
    keys=['GpuRestirMs','GpuPostMs','GpuHistorySnapshotMs','GpuPostPlusSnapshotMs','GpuDenoiseLevelSumMs',*[f'GpuDenoiseL{i}Ms' for i in range(5)]]
    for key in keys:
        b,c=stats(base,key),stats(cand,key)
        cell=lambda x:'not recorded' if x is None else f'{x[0]:.4f} / {x[1]:.4f}'
        delta='n/a' if b is None or c is None or b[0]==0 else f'{(c[0]/b[0]-1)*100:+.2f}%'
        print(f'| {key} | {cell(b)} | {cell(c)} | {delta} |')
    print('\nPost now excludes the separately timed history snapshot; compare PostPlusSnapshot across old/new builds. Denoise level intervals include synchronization and are diagnostic, not guaranteed additive. Zero means skipped/unavailable or below timing resolution, not proven free. GPU timing is not image-quality evidence.')

if __name__=='__main__':main()
