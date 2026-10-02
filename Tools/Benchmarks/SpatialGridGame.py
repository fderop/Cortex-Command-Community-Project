#!/usr/bin/env python3
"""Capture seeded simulation ticks in a configured PerformanceStress runtime."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_directory', type=Path)
parser.add_argument('benchmark_directory', type=Path)
parser.add_argument('runtime_directory', type=Path)
parser.add_argument('--replace-object', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--wave-size', type=int, default=2000)
parser.add_argument('--warmup-ticks', type=int, default=60)
parser.add_argument('--native-default', action='store_true')
args = parser.parse_args()
build, output, runtime = (p.resolve() for p in [args.build_directory, args.benchmark_directory, args.runtime_directory])
repository = Path(__file__).resolve().parents[2]
prefix = 'stock' if args.native_default else 'collidable'
main = (repository / 'Source/Main.cpp').read_text()
main = main.replace('void RunGameLoop() {', '''namespace RTE { extern long long profileTravel, profileColor, profileClear; }
void RunGameLoop() {
    struct CaptureSample { long long simulation, movable; int particles; long long travel, color, clear, gridWait; };
    std::vector<CaptureSample> captureSamples;
    captureSamples.reserve(240);''')
main = main.replace('g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_RandomGenerator.Seed(731 + captureSamples.size());
            const long long captureStart = g_TimerMan.GetAbsoluteTime();
            g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);''')
main = main.replace('g_MovableMan.Update();', '''const long long captureMovableStart = g_TimerMan.GetAbsoluteTime();
            g_MovableMan.Update();
            const long long captureMovableEnd = g_TimerMan.GetAbsoluteTime();''')
main = main.replace('g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);
            captureSamples.push_back({g_TimerMan.GetAbsoluteTime() - captureStart, captureMovableEnd - captureMovableStart,
                g_MovableMan.GetParticleCount(), profileTravel, profileColor, profileClear, profileWait});''')
main = main.replace('g_MovableMan.CompleteQueuedMOIDDrawings();', '''const auto profileWaitStart = g_TimerMan.GetAbsoluteTime();
            g_MovableMan.CompleteQueuedMOIDDrawings();
            const auto profileWait = g_TimerMan.GetAbsoluteTime() - profileWaitStart;''')
main = main.replace('g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);', '''g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);
        if (captureSamples.size() >= 240) {
            for (size_t i = 0; i < captureSamples.size(); ++i) {
                const auto& value = captureSamples[i];
                std::printf("PERF_PROFILE,%zu,%lld,%lld,%d,%lld,%lld,%lld,%lld\\n", i, value.simulation, value.movable,
                    value.particles, value.travel, value.color, value.clear, value.gridWait);
            }
            std::printf("PERF_CAPTURE_DONE\\n");
            std::fflush(stdout);
            System::SetQuit(true);
        }''')
movable = 'namespace RTE { long long profileTravel = 0, profileColor = 0, profileClear = 0; }\n' + (repository / 'Source/Managers/MovableMan.cpp').read_text()
movable = movable.replace('void MovableMan::Update() {', 'void MovableMan::Update() {\n profileTravel = profileColor = profileClear = 0;')
for original, counter in [('g_SceneMan.ClearMOColorLayer();', 'profileClear'), ('\tTravel();', 'profileTravel'), ('Draw(g_SceneMan.GetMOColorBitmap());', 'profileColor')]:
    movable = movable.replace(original, '{ const auto started = g_TimerMan.GetAbsoluteTime(); ' + original + ' ' + counter + ' = g_TimerMan.GetAbsoluteTime() - started; }')
commands = json.loads((build / 'compile_commands.json').read_text())
for name, source in [('Main', main), ('MovableMan', movable)]:
    path = output / (name + '-capture.cpp')
    path.write_text(source)
    command = shlex.split(next(c['command'] for c in commands if c['file'].endswith('/' + name + '.cpp')))
    command = command[:command.index('-MD')]
    command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
    command = ['-I' + str(repository / arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
    index = command.index('-include')
    command[index + 1] = str(repository / 'Source/System/StandardIncludes.h')
    subprocess.run(command + ['-c', str(path), '-o', str(output / (name + '-capture.o'))], cwd=build, check=True)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
for variant in ['baseline', 'changed']:
    command = link.copy()
    command[command.index('-o') + 1] = str(output / ('game-' + variant))
    replacements = args.replace_object + [('Source_Main.cpp.o', output / 'Main-capture.o'),
        ('Source_Managers_MovableMan.cpp.o', output / 'MovableMan-capture.o'),
        ('Source_System_SpatialPartitionGrid.cpp.o', output / ('SpatialPartitionGrid-' + variant + '.o'))]
    for name, path in replacements:
        command[command.index('CortexCommand.p/' + name)] = str(Path(path).resolve())
    subprocess.run(command, cwd=build, check=True)
stress = runtime / 'Mods/PerformanceStress.rte/Stress.lua'
original = stress.read_text()
stress.write_text(original.replace('for i = 1, 400 do', 'for i = 1, ' + str(args.wave_size) + ' do').replace(
    '            gib.GetsHitByMOs = true\n', '' if args.native_default else '            gib.GetsHitByMOs = true\n'))
executable = runtime / 'CortexCommand.app/Contents/MacOS/CortexCommand'
env = dict(os.environ)
results = {'wave_size': args.wave_size, 'native_default': args.native_default, 'warmup_ticks': args.warmup_ticks, 'runs': []}
rows = []
for run in range(1, args.runs + 1):
    particles = {}
    for variant in (['baseline', 'changed'] if run % 2 else ['changed', 'baseline']):
        shutil.copy2(output / ('game-' + variant), executable)
        samples = []
        with (output / (prefix + '-' + variant + '-' + str(run) + '.log')).open('w') as log:
            proc = subprocess.Popen([str(executable), '-cout'], cwd=runtime, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in proc.stdout:
                log.write(line)
                if line.startswith('PERF_PROFILE,'):
                    sample = [int(value) for value in line.strip().split(',')[1:]]
                    samples.append(sample)
                    rows.append([prefix, run, variant] + sample)
                if 'PERF_CAPTURE_DONE' in line:
                    proc.terminate()
                    break
            proc.wait()
        assert len(samples) == 240
        particles[variant] = [sample[3] for sample in samples]
        measured = samples[args.warmup_ticks:]
        durations = sorted(sample[1] for sample in measured)
        results['runs'].append({'run': run, 'variant': variant, 'mean_sim_us': statistics.mean(durations),
            'p95_sim_us': durations[math.ceil(0.95 * len(durations)) - 1], 'max_sim_us': max(durations),
            'mean_travel_us': statistics.mean(sample[4] for sample in measured)})
    assert particles['baseline'] == particles['changed']
stress.write_text(original)
results['particle_sequences_match'] = True
with (output / (prefix + '-game-results.csv')).open('w') as stream:
    writer = csv.writer(stream)
    writer.writerow(['scenario', 'run', 'variant', 'tick', 'sim_us', 'movable_us', 'particles', 'travel_us', 'color_us', 'clear_us', 'gridwait_us'])
    writer.writerows(rows)
(output / (prefix + '-game-summary.json')).write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
