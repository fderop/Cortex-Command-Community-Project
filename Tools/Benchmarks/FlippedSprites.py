#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description='Link baseline and changed Draw benchmarks with real engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--baseline-ref', default='41e6b7010817936e09d2929330b7519852a1fcca')
parser.add_argument('--replace-object', action='append', default=[], help='Name=/path/to/pristine/object.o')
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
repo = pathlib.Path(__file__).resolve().parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())

def compile_file(name, source, destination):
    command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('/' + name)))
    command = command[:command.index('-MD')]
    command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
    includes = ['-I' + str(path) for path in [repo / 'Source', *(repo / 'Source').iterdir()] if path.is_dir()]
    command[1:1] = includes
    command += ['-g', '-c', str(source), '-o', str(destination)]
    subprocess.run(command, cwd=build, check=True)

benchmark = output / 'benchmark.o'
compile_file('MOSRotating.cpp', pathlib.Path(__file__).with_suffix('.cpp'), benchmark)
base_link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
for variant in ['baseline', 'changed']:
    replacements = dict(argument.split('=', 1) for argument in args.replace_object)
    for name, folder in [('MOSRotating', 'Entities'), ('ContentFile', 'System')]:
        relative = 'Source/' + folder + '/' + name + '.cpp'
        source = repo / relative
        if variant == 'baseline':
            source = output / (name + '-baseline.cpp')
            source.write_bytes(subprocess.check_output(['git', 'show', args.baseline_ref + ':' + relative], cwd=repo))
        destination = output / (name + '-' + variant + '.o')
        compile_file(name + '.cpp', source, destination)
        replacements[name] = str(destination)
    link = base_link.copy()
    link[link.index('-o') + 1] = str(output / variant)
    link[link.index('CortexCommand.p/Source_Main.cpp.o')] = str(benchmark)
    for name, path in replacements.items():
        index = next(i for i, arg in enumerate(link) if arg.endswith('_' + name + '.cpp.o'))
        link[index] = str(pathlib.Path(path).resolve())
    subprocess.run(link, cwd=build, check=True)
