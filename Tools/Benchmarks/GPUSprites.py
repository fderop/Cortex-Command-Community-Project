#!/usr/bin/env python3
"""Compile the GPU experiment against a release engine build."""
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
parser.add_argument('--replace-object', action='append', default=[], help='Name=/path/to/baseline/object.o')
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
repo = pathlib.Path(__file__).resolve().parents[2]
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('/MOSRotating.cpp')))
command = command[:command.index('-MD')]
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
command[1:1] = ['-I' + str(path) for path in [repo / 'Source', *(repo / 'Source').iterdir()] if path.is_dir()]
benchmark = output / 'GPUSprites.o'
command += ['-c', str(pathlib.Path(__file__).with_suffix('.cpp')), '-o', str(benchmark)]
subprocess.run(command, cwd=build, check=True)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
indices = {arg.rsplit('_', 1)[-1].removesuffix('.cpp.o'): i for i, arg in enumerate(link) if arg.endswith('.cpp.o')}
link[link.index('-o') + 1] = str(output / 'GPUSprites')
link[link.index('CortexCommand.p/Source_Main.cpp.o')] = str(benchmark)
for argument in args.replace_object:
    name, path = argument.split('=', 1)
    link[indices[name]] = str(pathlib.Path(path).resolve())
subprocess.run(link, cwd=build, check=True)
