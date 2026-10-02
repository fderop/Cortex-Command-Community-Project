#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description='Build seeded game captures from the FlippedSprites engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--replace-object', action='append', default=[])
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
repo = pathlib.Path(__file__).resolve().parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
movable = (repo / 'Source/Managers/MovableMan.cpp').read_text()
movable = 'namespace RTE { long long profileTravel=0, profileColor=0, profileClear=0; }\n' + movable
movable = movable.replace('void MovableMan::Update() {', 'void MovableMan::Update() {\n profileTravel=profileColor=profileClear=0;')
movable = movable.replace('g_SceneMan.ClearMOColorLayer();', '{ const auto started=g_TimerMan.GetAbsoluteTime(); g_SceneMan.ClearMOColorLayer(); profileClear=g_TimerMan.GetAbsoluteTime()-started; }')
movable = movable.replace('\tTravel();', '\t{ const auto started=g_TimerMan.GetAbsoluteTime(); Travel(); profileTravel=g_TimerMan.GetAbsoluteTime()-started; }')
movable = movable.replace('Draw(g_SceneMan.GetMOColorBitmap());', '{ const auto started=g_TimerMan.GetAbsoluteTime(); Draw(g_SceneMan.GetMOColorBitmap()); profileColor=g_TimerMan.GetAbsoluteTime()-started; }')
main = (repo / 'Source/Main.cpp').read_text()
main = main.replace('void RunGameLoop() {', '''namespace RTE { extern long long profileTravel, profileColor, profileClear; }
void RunGameLoop() {
    struct CaptureSample { long long simulation, movable; int particles; long long travel, color, clear, gridWait; };
    std::vector<CaptureSample> captureSamples;
''')
main = main.replace('g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_RandomGenerator.Seed(731 + captureSamples.size());
            const auto captureStart=g_TimerMan.GetAbsoluteTime();
            g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);''')
main = main.replace('g_MovableMan.CompleteQueuedMOIDDrawings();', '''const auto waitStart=g_TimerMan.GetAbsoluteTime();
            g_MovableMan.CompleteQueuedMOIDDrawings();
            const auto gridWait=g_TimerMan.GetAbsoluteTime()-waitStart;''')
main = main.replace('g_MovableMan.Update();', '''const auto movableStart=g_TimerMan.GetAbsoluteTime();
            g_MovableMan.Update();
            const auto movableEnd=g_TimerMan.GetAbsoluteTime();''')
main = main.replace('g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);
            captureSamples.push_back({g_TimerMan.GetAbsoluteTime()-captureStart, movableEnd-movableStart, g_MovableMan.GetParticleCount(), profileTravel, profileColor, profileClear, gridWait});''')
main = main.replace('g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);', '''g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);
        if (captureSamples.size() >= 240) {
            for (size_t i=0; i<captureSamples.size(); ++i) {
                const auto& v=captureSamples[i];
                std::printf("PERF_PROFILE,%zu,%lld,%lld,%d,%lld,%lld,%lld,%lld\\n", i, v.simulation, v.movable, v.particles, v.travel, v.color, v.clear, v.gridWait);
            }
            std::printf("PERF_CAPTURE_DONE\\n"); std::fflush(stdout); System::SetQuit(true);
        }''')
for name, source in [('Main', main), ('MovableMan', movable)]:
    path = output / (name + '-capture.cpp')
    path.write_text(source)
    command = shlex.split(next(c['command'] for c in commands if c['file'].endswith('/' + name + '.cpp')))
    command = command[:command.index('-MD')]
    command += ['-c', str(path), '-o', str(output / (name + '-capture.o'))]
    subprocess.run(command, cwd=build, check=True)
base_link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
for variant in ['baseline', 'changed']:
    replacements = dict(argument.split('=', 1) for argument in args.replace_object)
    replacements.update({name: str(output / (name + '-capture.o')) for name in ['Main', 'MovableMan']})
    replacements.update({name: str(output / (name + '-' + variant + '.o')) for name in ['MOSRotating', 'ContentFile']})
    link = base_link.copy()
    link[link.index('-o') + 1] = str(output / ('game-' + variant))
    for name, path in replacements.items():
        link[next(i for i, arg in enumerate(link) if arg.endswith('_' + name + '.cpp.o'))] = str(pathlib.Path(path).resolve())
    subprocess.run(link, cwd=build, check=True)
