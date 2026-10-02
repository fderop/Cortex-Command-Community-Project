#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import statistics
import subprocess

parser = argparse.ArgumentParser(description='Build baseline and changed movement segment benchmarks with compiled engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--baseline-ref', required=True)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=0, help='Run this many alternating baseline/changed pairs after compilation.')
parser.add_argument('--trace', action='store_true', help='Add segment counters for correctness analysis. Do not use trace timings.')
parser.add_argument('--calls', type=int, default=4096)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).with_suffix('.cpp').resolve()
repository = source.parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('AtomGroup.cpp')))
command = command[:command.index('-MD')]
# Compile against the engine headers without the generated precompiled header.
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
index = command.index('-include')
del command[index:index + 2]
benchmark_object = str(output / 'MovementSegments.o')
subprocess.run(command + ['-c', str(source), '-o', benchmark_object], cwd=build, check=True)
baseline_source = output / 'AtomGroup-baseline.cpp'
baseline_source.write_bytes(subprocess.check_output(
    ['git', 'show', args.baseline_ref + ':Source/Entities/AtomGroup.cpp'], cwd=repository))
link = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1]
# Keep the experimental reset outside production until the behavior comparison passes.
changed_source = output / 'AtomGroup-changed.cpp'
reset_marker = '\t\tfor (Atom* atom: m_Atoms) {\n\t\t\t// Calculate the segment trajectory'
changed_source.write_text(baseline_source.read_text().replace(reset_marker, '\t\tstepsOnSeg = 0;\n\n' + reset_marker, 1))
for variant, engine_source in [('baseline', baseline_source), ('changed', changed_source)]:
    if args.trace:
        contents = engine_source.read_text()
        contents = 'extern void segmentSample(int, int, int, float);\nextern void segmentFinish(int, int, bool, bool, float);\n' + contents
        marker = '\t\tfor (Atom* atom: m_Atoms) {\n\t\t\tatom->SetStepRatio'
        contents = contents.replace(marker, '\t\tint actualSteps = 0;\n\t\tfor (Atom* atom: m_Atoms) actualSteps = std::max(actualSteps, atom->GetStepsLeft());\n\t\t::segmentSample(segCount, stepsOnSeg, actualSteps, timeLeft);\n' + marker, 1)
        contents = contents.replace('\t\t++segCount;', '\t\t::segmentFinish(segCount, stepCount, hitStep, halted, segProgress);\n\t\t++segCount;', 1)
        engine_source = output / ('AtomGroup-' + variant + '-trace.cpp')
        engine_source.write_text(contents)
    engine_object = str(output / ('AtomGroup-' + variant + '.o'))
    # The source relies on the project's normal forced StandardIncludes.h include.
    subprocess.run(command + ['-include', str(repository / 'Source/System/StandardIncludes.h'),
                             '-c', str(engine_source), '-o', engine_object], cwd=build, check=True)
    link_command = shlex.split(link)
    link_command[link_command.index('-o') + 1] = str(output / variant)
    link_command[link_command.index('CortexCommand.p/Source_Main.cpp.o')] = benchmark_object
    link_command[link_command.index('CortexCommand.p/Source_Entities_AtomGroup.cpp.o')] = engine_object
    for name, replacement in args.replace_object:
        link_command[link_command.index('CortexCommand.p/' + name)] = str(pathlib.Path(replacement).resolve())
    subprocess.run(link_command, cwd=build, check=True)

results = {'baseline_ref': args.baseline_ref, 'calls_per_sample': args.calls, 'runs': args.runs, 'trace': args.trace, 'modes': {}}
for run in range(args.runs):
    for variant in (['baseline', 'changed'] if run % 2 == 0 else ['changed', 'baseline']):
        log = subprocess.check_output([str(output / variant), str(args.calls)], text=True)
        (output / (variant + '-run' + str(run + 1) + '.txt')).write_text(log)
        for line in log.splitlines():
            row = dict(item.split('=', 1) for item in line.split())
            if 'mode' not in row: continue
            mode = results['modes'].setdefault(row['mode'], {})
            entry = mode.setdefault(variant, {'behavior_hashes': [], 'responses': [], 'segments': [], 'scheduled_steps': [], 'shorter': [], 'zero': [], 'one': [], 'halted': [], 'ms': []})
            for key in entry:
                field = 'behavior_hash' if key == 'behavior_hashes' else key
                if field in row:
                    entry[key].append(float(row[field]) if key == 'ms' else int(row[field]))
for mode, values in results['modes'].items():
    baseline, changed = values['baseline'], values['changed']
    values['behavior_matches'] = baseline['behavior_hashes'] == changed['behavior_hashes']
    values['response_count_matches'] = baseline['responses'] == changed['responses']
    values['baseline_median_ms'] = statistics.median(baseline['ms'])
    values['changed_median_ms'] = statistics.median(changed['ms'])
    values['time_reduction_percent'] = 100 * (1 - values['changed_median_ms'] / values['baseline_median_ms'])
    print(mode, 'behavior_matches=', values['behavior_matches'], 'baseline_ms=', round(values['baseline_median_ms'], 3),
          'changed_ms=', round(values['changed_median_ms'], 3),
          'time_reduction_percent=', round(values['time_reduction_percent'], 2))
if args.runs:
    (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
