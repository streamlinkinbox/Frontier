#!/usr/bin/env python3
"""Overlap-aware startup CSV report. No third-party dependencies; never sum jobs as startup time."""
import argparse
import csv
import math
from pathlib import Path

MIB = 1024 * 1024


def load(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        required = {'event', 'since_main_ms', 'rss_bytes', 'peak_rss_bytes', 'private_commit_bytes'}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError('Not a Frontier startup CSV: missing required columns')
        rows = list(reader)
    if not rows:
        raise ValueError('Startup CSV has no samples')
    for row in rows:
        for key in required - {'event'}:
            row[key] = float(row[key])
            if not math.isfinite(row[key]):
                raise ValueError(f'Non-finite value in {key}')
        if row['since_main_ms'] < 0:
            raise ValueError('Negative event timestamp')
    return sorted(rows, key=lambda row: row['since_main_ms'])


def intervals(rows):
    pending, completed, warnings = {}, [], []
    for row in rows:
        event, time = row['event'], row['since_main_ms']
        if event.endswith(':begin'):
            pending.setdefault(event[:-6], []).append(time)
        elif event.endswith(':end'):
            name = event[:-4]
            if pending.get(name):
                completed.append((name, pending[name].pop(), time))
            else:
                warnings.append(f'{name}: end without begin; excluded from interval totals')
    for name, starts in pending.items():
        for start in starts:
            warnings.append(f'{name}: incomplete; began at {start/1000:.3f} s, no end recorded')
    return completed, warnings


def union_ms(spans):
    """Wall coverage of intervals, including nested and coincident jobs only once."""
    total, end = 0.0, -math.inf
    for start, stop in sorted(spans):
        if stop < start:
            raise ValueError('Reversed interval')
        total += max(0.0, stop - max(start, end))
        end = max(end, stop)
    return total


def memory(value):
    return 'N/A' if value < 0 else f'{value / MIB:,.2f} MiB'


def render(rows, title='Startup report'):
    phases, warnings = intervals(rows)
    spans = [(start, stop) for _, start, stop in phases]
    coverage = union_ms(spans)
    job_sum = sum(stop-start for start, stop in spans)
    final = rows[-1]
    lines = [f'# {title}', '', f"Last observed event: **{final['event']}**, at **{final['since_main_ms']/1000:.3f} s**.",
             'This is an observed timeline, not necessarily a completed startup.', '']
    for marker in ('FrameLoopReady', 'FirstPresentReturned'):
        matches = [r for r in rows if r['event'] == marker]
        if matches:
            lines.append(f"- {marker}: {matches[0]['since_main_ms']/1000:.3f} s since logger origin.")
        else:
            lines.append(f'- {marker}: not recorded; completion cannot be inferred.')
    pids = sorted({r['process_id'] for r in rows if r.get('process_id')})
    if pids:
        lines.append('- Logged process ID(s): ' + ', '.join(pids))
    lines += ['', '## Phase wall intervals', '', '| Phase | Begin (s) | End (s) | Wall span (s) |',
              '|---|---:|---:|---:|']
    for name, start, stop in sorted(phases, key=lambda p: p[1]):
        lines.append(f'| {name} | {start/1000:.3f} | {stop/1000:.3f} | {(stop-start)/1000:.3f} |')
    lines += ['', f'- **Completed-phase wall coverage, overlaps counted once: {coverage/1000:.3f} s.**',
              f'- Sum of job spans: {job_sum/1000:.3f} s — **NOT startup elapsed time or CPU time**.',
              f'- Duplicate coverage removed (including nested phases): {(job_sum-coverage)/1000:.3f} s.',
              '- Uninstrumented gaps and incomplete phases are not included in completed-phase coverage.',
              '- Interval arithmetic uses logged begin/end timestamps. Small differences from `phase_ms` include logging overhead.', '']
    lines.extend('- WARNING: ' + warning for warning in warnings)
    lines += ['', '## Process memory samples', '', '| Event | Time (s) | Resident working set | OS peak resident | Private commit |',
              '|---|---:|---:|---:|---:|']
    for row in rows:
        lines.append(f"| {row['event']} | {row['since_main_ms']/1000:.3f} | {memory(row['rss_bytes'])} | {memory(row['peak_rss_bytes'])} | {memory(row['private_commit_bytes'])} |")
    lines += ['', f"- Largest sampled private commit: {memory(max(r['private_commit_bytes'] for r in rows))} (not an OS lifetime peak).",
              '- Memory samples belong to the entire logged process, NOT exclusively to the phase named in the row.',
              '- Resident RAM can fall because memory was freed or Windows trimmed/paged the working set. Commit need not fall with it.',
              '- Task Manager app groups may include multiple processes and show private working set rather than this total working set.',
              '- GPU allocations, dedicated/shared GPU usage and process private commit are distinct; do not add them into a RAM total.',
              '- Compare the same event, resolution, scene and settings across runs; startup peaks and later steady-state values are not interchangeable.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output', type=Path, help='Write Markdown instead of stdout')
    args = parser.parse_args()
    try:
        report = render(load(args.csv))
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(report, encoding='utf-8')
        else:
            print(report, end='')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Startup report failed: {error}\n')


if __name__ == '__main__':
    main()
