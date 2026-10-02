#!/usr/bin/env python3
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description='Build the collision benchmark with existing engine objects.')
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output.resolve()
source = pathlib.Path(__file__).with_suffix('.cpp').resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
command = shlex.split(next(x['command'] for x in commands if x['file'].endswith('MOSprite.cpp')))
command = command[:command.index('-MD')]
# The benchmark includes StandardIncludes.h itself. Do not use the generated PCH.
command = [arg for arg in command if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
index = command.index('-include')
del command[index:index + 2]
object_file = str(output) + '.o'
command += ['-c', str(source), '-o', object_file]
subprocess.run(command, cwd=build, check=True)
link = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1]
command = shlex.split(link)
command[command.index('-o') + 1] = str(output)
command[command.index('CortexCommand.p/Source_Main.cpp.o')] = object_file
subprocess.run(command, cwd=build, check=True)
