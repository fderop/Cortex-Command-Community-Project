#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import statistics
import subprocess

parser = argparse.ArgumentParser(description='Measure real independent AtomGroup travel and native serial collision ordering.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--bodies', type=int, default=2000)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).with_suffix('.cpp').resolve()
repository = source.parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('MovableObject.cpp')))
command = command[:command.index('-MD')]
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
index = command.index('-include')
del command[index:index + 2]
fixture_object = str(output / 'ParallelPhysics.o')
subprocess.run(command + ['-c', str(source), '-o', fixture_object], cwd=build, check=True)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
executable = str(output / 'ParallelPhysics')
link[link.index('-o') + 1] = executable
link[link.index('CortexCommand.p/Source_Main.cpp.o')] = fixture_object
for name, replacement in args.replace_object:
    link[link.index('CortexCommand.p/' + name)] = str(pathlib.Path(replacement).resolve())
subprocess.run(link, cwd=build, check=True)

results = {'bodies': args.bodies, 'runs': args.runs, 'batches_per_sample': 180, 'modes': {}}
for atoms in (8, 32):
    hashes = None
    collision_states = None
    modes = results['modes'][str(atoms)] = {}
    for run in range(args.runs):
        for workers in ((0, 2, 4) if run % 2 == 0 else (4, 2, 0)):
            log = subprocess.check_output([executable, str(workers), str(atoms), str(args.bodies)], text=True)
            (output / f'atoms{atoms}-workers{workers}-run{run + 1}.txt').write_text(log)
            rows = [dict(item.split('=', 1) for item in line.split()) for line in log.splitlines()]
            states = [(int(row['body']), int(row['hash'])) for row in rows if row['kind'] == 'hash']
            collisions = [row for row in rows if row['kind'].startswith('collision') or row['kind'] == 'impulse']
            assert len(states) == args.bodies
            if hashes is None:
                hashes, collision_states = states, collisions
            assert states == hashes, f'{atoms} atoms, {workers} workers: per-body state differs'
            assert collisions == collision_states, 'native collision result changed between identical runs'
            forward = [row['hash'] for row in collisions if row['kind'] == 'collision' and row['order'] == 'forward']
            reverse = [row['hash'] for row in collisions if row['kind'] == 'collision' and row['order'] == 'reverse']
            assert forward != reverse, 'collision fixture did not demonstrate schedule dependence'
            assert any(int(row['impulses']) > 0 for row in collisions if row['kind'] == 'collision'), 'no native impulse response'
            entry = modes.setdefault(str(workers), {'sample_ms_per_batch': [], 'dispatch_us': []})
            for row in rows:
                if row['kind'] == 'timing':
                    entry['sample_ms_per_batch'].append(float(row['ms']) / int(row['batches']))
                elif row['kind'] == 'dispatch':
                    entry['dispatch_us'].append(1000 * float(row['ms']) / int(row['batches']))
    for workers, values in modes.items():
        values['median_ms_per_batch'] = statistics.median(values['sample_ms_per_batch'])
        values['speedup'] = statistics.median(modes['0']['sample_ms_per_batch']) / values['median_ms_per_batch']
        if values['dispatch_us']:
            values['median_dispatch_us'] = statistics.median(values['dispatch_us'])
        print(atoms, 'atoms', workers, 'workers', round(values['median_ms_per_batch'], 4), 'ms/batch', round(values['speedup'], 3), 'speedup', flush=True)
results['per_body_hashes_match'] = True
results['native_serial_collision_order_differs'] = True
(output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
