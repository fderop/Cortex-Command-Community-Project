#!/usr/bin/env python3
"""Run quiet, alternating pairs and summarize paired language timings."""
import argparse
import collections
import json
import os
import pathlib
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary_directory', type=pathlib.Path)
parser.add_argument('library_directory', type=pathlib.Path)
parser.add_argument('result_directory', type=pathlib.Path)
parser.add_argument('--summarize-only', action='store_true')
args = parser.parse_args()
binaries = args.binary_directory.resolve()
results = args.result_directory.resolve()
results.mkdir(parents=True, exist_ok=True)
env = os.environ | {'DYLD_LIBRARY_PATH': str(args.library_directory.resolve())}
rows = []
correctness = []
for pair in range(3):
    for variant in (['baseline', 'cached'] if pair % 2 == 0 else ['cached', 'baseline']):
        path = results / f'{variant}-{pair}.txt'
        if not args.summarize_only:
            run = subprocess.run([str(binaries / variant), '7'], env=env, text=True, capture_output=True, check=True)
            path.write_text(run.stdout)
            print(f'finished variant={variant} pair={pair}', flush=True)
        for line in path.read_text().splitlines():
            row = dict(field.split('=', 1) for field in line.split())
            row.update(variant=variant, pair=pair)
            if row['mode'] in ['correctness', 'batch-correctness']:
                correctness.append(row)
            else:
                row['ms'] = float(row['ms'])
                row['sample'] = int(row['sample'])
                rows.append(row)
groups = collections.defaultdict(list)
for row in rows:
    groups[(row['variant'], row['mode'], row['language'])].append(row['ms'])
summary = []
for (variant, mode, language), values in sorted(groups.items()):
    summary.append(dict(variant=variant, mode=mode, language=language, samples=len(values),
                        mean_ms=statistics.mean(values), median_ms=statistics.median(values),
                        min_ms=min(values), max_ms=max(values)))
ratios = []
for variant in ['baseline', 'cached']:
    for mode in ['adapter', 'scalar', 'batch', 'pack-batch']:
        rust = {(r['pair'], r['sample']): r['ms'] for r in rows
                if r['variant'] == variant and r['mode'] == mode and r['language'] == 'rust'}
        for language in ['gcc', 'clang']:
            control = {(r['pair'], r['sample']): r['ms'] for r in rows
                       if r['variant'] == variant and r['mode'] == mode and r['language'] == language}
            changes = [(rust[k] / control[k] - 1) * 100 for k in rust]
            ratios.append(dict(variant=variant, mode=mode, control=language,
                               mean_rust_time_change_percent=statistics.mean(changes),
                               median_rust_time_change_percent=statistics.median(changes),
                               min_percent=min(changes), max_percent=max(changes)))
(results / 'summary.json').write_text(json.dumps(dict(correctness=correctness, timings=summary, paired_changes=ratios), indent=2) + '\n')
print(json.dumps(dict(timings=summary, paired_changes=ratios), indent=2))
