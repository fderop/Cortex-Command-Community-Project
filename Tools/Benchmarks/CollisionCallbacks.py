#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import statistics
import subprocess

parser = argparse.ArgumentParser(description='Build baseline and changed collision callback benchmarks with compiled engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--baseline-ref', required=True)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=0, help='Run this many alternating baseline/changed pairs after compilation.')
parser.add_argument('--calls', type=int, default=4096)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).with_suffix('.cpp').resolve()
repository = source.parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('MovableObject.cpp')))
command = command[:command.index('-MD')]
# Compile against the engine headers without the generated precompiled header.
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
index = command.index('-include')
del command[index:index + 2]
benchmark_object = str(output / 'CollisionCallbacks.o')
subprocess.run(command + ['-c', str(source), '-o', benchmark_object], cwd=build, check=True)
baseline_source = output / 'MovableObject-baseline.cpp'
baseline_source.write_bytes(subprocess.check_output(
    ['git', 'show', args.baseline_ref + ':Source/Entities/MovableObject.cpp'], cwd=repository))
link = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1]
for variant, engine_source in [('baseline', baseline_source), ('changed', repository / 'Source/Entities/MovableObject.cpp')]:
    engine_object = str(output / ('MovableObject-' + variant + '.o'))
    # The source relies on the project's normal forced StandardIncludes.h include.
    subprocess.run(command + ['-include', str(repository / 'Source/System/StandardIncludes.h'),
                             '-c', str(engine_source), '-o', engine_object], cwd=build, check=True)
    link_command = shlex.split(link)
    link_command[link_command.index('-o') + 1] = str(output / variant)
    link_command[link_command.index('CortexCommand.p/Source_Main.cpp.o')] = benchmark_object
    link_command[link_command.index('CortexCommand.p/Source_Entities_MovableObject.cpp.o')] = engine_object
    for name, replacement in args.replace_object:
        link_command[link_command.index('CortexCommand.p/' + name)] = str(pathlib.Path(replacement).resolve())
    subprocess.run(link_command, cwd=build, check=True)

results = {'baseline_ref': args.baseline_ref, 'calls_per_sample': args.calls, 'runs': args.runs, 'modes': {}}
for run in range(args.runs):
    for variant in (['baseline', 'changed'] if run % 2 == 0 else ['changed', 'baseline']):
        log = subprocess.check_output([str(output / variant), str(args.calls)], text=True)
        (output / (variant + '-run' + str(run + 1) + '.txt')).write_text(log)
        for line in log.splitlines():
            row = dict(item.split('=', 1) for item in line.split())
            mode = results['modes'].setdefault(row['mode'], {})
            entry = mode.setdefault(variant, {'behavior_hashes': [], 'responses': [], 'allocations': [], 'bytes': [], 'ms': []})
            for key in entry:
                field = 'behavior_hash' if key == 'behavior_hashes' else key
                if field in row:
                    entry[key].append(float(row[field]) if key == 'ms' else int(row[field]))
for mode, values in results['modes'].items():
    baseline, changed = values['baseline'], values['changed']
    assert baseline['behavior_hashes'] == changed['behavior_hashes'], mode + ': outcome or collision order differs'
    assert baseline['responses'] == changed['responses'], mode + ': response count differs'
    values['baseline_median_ms'] = statistics.median(baseline['ms'])
    values['changed_median_ms'] = statistics.median(changed['ms'])
    values['time_reduction_percent'] = 100 * (1 - values['changed_median_ms'] / values['baseline_median_ms'])
    print(mode, 'baseline_ms=', round(values['baseline_median_ms'], 3),
          'changed_ms=', round(values['changed_median_ms'], 3),
          'time_reduction_percent=', round(values['time_reduction_percent'], 2))
if args.runs:
    (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
