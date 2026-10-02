#!/usr/bin/env python3
"""Build a test-only camera culling candidate against real engine objects."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_directory', type=Path)
parser.add_argument('output_directory', type=Path)
parser.add_argument('runtime_directory', type=Path)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--repeats', type=int, default=64)
args = parser.parse_args()
build, output, runtime = (p.resolve() for p in [args.build_directory, args.output_directory, args.runtime_directory])
repository = Path(__file__).resolve().parents[2]
output.mkdir(parents=True, exist_ok=True)
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(c['command'] for c in commands if c['file'].endswith('/MOSRotating.cpp')))
command = command[:command.index('-MD')]
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
index = command.index('-include')
command[index + 1] = str(repository / 'Source/System/StandardIncludes.h')
for name, source in [('Culling', Path(__file__).with_suffix('.cpp')), ('MOSRotating', repository / 'Source/Entities/MOSRotating.cpp')]:
    subprocess.run(command + ['-c', str(source), '-o', str(output / (name + '.o'))], cwd=build, check=True)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
link[link.index('-o') + 1] = str(output / 'culling')
replacements = [('Source_Main.cpp.o', output / 'Culling.o'), ('Source_Entities_MOSRotating.cpp.o', output / 'MOSRotating.o')] + args.replace_object
for name, path in replacements:
    link[link.index('CortexCommand.p/' + name)] = str(Path(path).resolve())
subprocess.run(link, cwd=build, check=True)
env = dict(os.environ, DYLD_LIBRARY_PATH=str(build.parent / 'external/lib/macos'))

def run(mode, number):
    log = subprocess.check_output([str(output / 'culling'), mode, str(args.repeats)], cwd=runtime, env=env, text=True)
    (output / (mode + '-' + str(number) + '.txt')).write_text(log)
    return [dict(item.split('=', 1) for item in line.split()) for line in log.splitlines()]

correctness = run('correctness', 0)
pixels = runtime / correctness[-1]['image_directory']
(output / 'pixels').mkdir(exist_ok=True)
for bitmap in pixels.glob('*-*.bmp'):
    shutil.copy2(bitmap, output / 'pixels' / bitmap.name)
shutil.rmtree(pixels)
results = {'baseline_ref': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repository, text=True).strip(),
           'runs': args.runs, 'repeats': args.repeats, 'correctness': correctness, 'timings': {}}
for number in range(1, args.runs + 1):
    for variant in (['baseline', 'culled'] if number % 2 else ['culled', 'baseline']):
        for row in run(variant, number):
            key = 'size' + row['size'] + ('-spread' if row['spread'] == '1' else '-onscreen')
            results['timings'].setdefault(key, {}).setdefault(variant, []).append(row)
for key, variants in results['timings'].items():
    assert [x['viewport_hash'] for x in variants['baseline']] == [x['viewport_hash'] for x in variants['culled']]
    for variant in ['baseline', 'culled']:
        variants[variant + '_median_ms'] = statistics.median(float(x['ms']) for x in variants[variant])
    variants['time_reduction_percent'] = 100 * (1 - variants['culled_median_ms'] / variants['baseline_median_ms'])
    print(key, variants['baseline_median_ms'], variants['culled_median_ms'], variants['time_reduction_percent'])
(output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
print(correctness[-1])
