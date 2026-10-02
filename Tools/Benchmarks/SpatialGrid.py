#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import statistics
import subprocess

parser = argparse.ArgumentParser(description='Compare production spatial grid routines with compiled engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--baseline-ref', required=True)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--calls', type=int, default=300000)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).with_suffix('.cpp').resolve()
repository = source.parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('SpatialPartitionGrid.cpp')))
command = command[:command.index('-MD')]
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
index = command.index('-include')
del command[index:index + 2]
benchmark_object = str(output / 'SpatialGrid.o')
subprocess.run(command + ['-c', str(source), '-o', benchmark_object], cwd=build, check=True)
baseline_source = output / 'SpatialPartitionGrid-baseline.cpp'
baseline_source.write_bytes(subprocess.check_output(['git', 'show', args.baseline_ref + ':Source/System/SpatialPartitionGrid.cpp'], cwd=repository))
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
for variant, engine_source in [('baseline', baseline_source), ('changed', repository / 'Source/System/SpatialPartitionGrid.cpp')]:
    engine_object = str(output / ('SpatialPartitionGrid-' + variant + '.o'))
    subprocess.run(command + ['-include', str(repository / 'Source/System/StandardIncludes.h'), '-c', str(engine_source), '-o', engine_object], cwd=build, check=True)
    link_command = link.copy()
    link_command[link_command.index('-o') + 1] = str(output / variant)
    link_command[link_command.index('CortexCommand.p/Source_Main.cpp.o')] = benchmark_object
    link_command[link_command.index('CortexCommand.p/Source_System_SpatialPartitionGrid.cpp.o')] = engine_object
    for name, replacement in args.replace_object:
        link_command[link_command.index('CortexCommand.p/' + name)] = str(pathlib.Path(replacement).resolve())
    subprocess.run(link_command, cwd=build, check=True)
results = {'baseline_ref': args.baseline_ref, 'calls_per_sample': args.calls, 'runs': args.runs, 'correctness': {}, 'modes': {}}
for run in range(args.runs):
    for variant in (['baseline', 'changed'] if run % 2 == 0 else ['changed', 'baseline']):
        log = subprocess.check_output([str(output / variant), str(args.calls)], text=True)
        (output / (variant + '-run' + str(run + 1) + '.txt')).write_text(log)
        hashes = []
        for line in log.splitlines():
            row = dict(item.split('=', 1) for item in line.split())
            if row['mode'] == 'correctness':
                hashes.append(row['behavior_hash'])
            else:
                entry = results['modes'].setdefault(row['mode'], {}).setdefault(variant, {'ms': [], 'checksums': []})
                entry['ms'].append(float(row['ms']))
                entry['checksums'].append(int(row['checksum']))
        results['correctness'].setdefault(variant, []).append(hashes)
assert results['correctness']['baseline'] == results['correctness']['changed']
for mode, values in results['modes'].items():
    assert values['baseline']['checksums'] == values['changed']['checksums'], mode
    for variant in ['baseline', 'changed']:
        values[variant + '_median_ms'] = statistics.median(values[variant]['ms'])
    values['time_reduction_percent'] = 100 * (1 - values['changed_median_ms'] / values['baseline_median_ms'])
    print(mode, values['baseline_median_ms'], values['changed_median_ms'], values['time_reduction_percent'])
(output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
