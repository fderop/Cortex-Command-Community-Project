from pathlib import Path
import json, shlex, subprocess, sys
repository = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()
artifacts = Path(sys.argv[2]).resolve()
artifacts.mkdir(parents=True, exist_ok=True)
source = (repository/'Source/Main.cpp').read_text()
source = source.replace('void RunGameLoop() {', '''void RunGameLoop() {
    struct CaptureSample { long long simulation; long long movable; int particles; };
    std::vector<CaptureSample> captureSamples;
    captureSamples.reserve(600);
''')
source = source.replace('g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_RandomGenerator.Seed(731 + captureSamples.size());
            const long long captureStart = g_TimerMan.GetAbsoluteTime();
            g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);''')
source = source.replace('g_MovableMan.Update();', '''const long long captureMovableStart = g_TimerMan.GetAbsoluteTime();
            g_MovableMan.Update();
            const long long captureMovableEnd = g_TimerMan.GetAbsoluteTime();''')
source = source.replace('g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);', '''g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);
            captureSamples.push_back({g_TimerMan.GetAbsoluteTime() - captureStart, captureMovableEnd - captureMovableStart, g_MovableMan.GetParticleCount()});''')
source = source.replace('g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);', '''g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);
        if (captureSamples.size() >= 240) {
            for (size_t sample = 0; sample < captureSamples.size(); ++sample) {
                const auto& value = captureSamples[sample];
                std::printf("PERF_CAPTURE,%zu,%lld,%lld,%d\\n", sample, value.simulation, value.movable, value.particles);
            }
            std::printf("PERF_CAPTURE_DONE\\n");
            std::fflush(stdout);
            System::SetQuit(true);
        }''')
(artifacts/'Main-dense.cpp').write_text(source)
commands=json.loads((build/'compile_commands.json').read_text())
command=shlex.split(next(c['command'] for c in commands if c['file'].endswith('/Main.cpp')))
command=command[:command.index('-MD')]
command=[arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command=['-I'+str(repository/arg[5:]) if arg.startswith('-I../Source') else arg for arg in command]
command[command.index('-include')+1]=str(repository/'Source/System/StandardIncludes.h')
command+=['-c',str(artifacts/'Main-dense.cpp'),'-o',str(artifacts/'Main-dense.o')]
subprocess.run(command,cwd=build,check=True)
link=shlex.split(subprocess.check_output(['ninja','-C',str(build),'-t','commands','CortexCommand'],text=True).splitlines()[-1])
link[link.index('CortexCommand.p/Source_Main.cpp.o')]=str(artifacts/'Main-dense.o')
for variant in ['baseline','changed']:
 command=link.copy()
 command[command.index('-o')+1]=str(artifacts/('game-'+variant))
 command[command.index('CortexCommand.p/Source_Entities_MovableObject.cpp.o')]=str(artifacts/('MovableObject-'+variant+'.o'))
 for argument in sys.argv[3:]:
  name,path=argument.split('=',1)
  command[command.index('CortexCommand.p/'+name)]=str(Path(path).resolve())
 subprocess.run(command,cwd=build,check=True)
