#!/usr/bin/env python3
"""Build both fixtures with existing release engine objects."""
import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('build_directory', type=pathlib.Path)
parser.add_argument('baseline_objects', type=pathlib.Path)
parser.add_argument('output_directory', type=pathlib.Path)
args = parser.parse_args()
build = args.build_directory.resolve()
baseline = args.baseline_objects.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
source = pathlib.Path(__file__).resolve().parent
repo = source.parent.parent
commands = json.loads((build / 'compile_commands.json').read_text())
engine_compile = shlex.split(next(x['command'] for x in commands if x['file'].endswith('MOSprite.cpp')))
engine_compile = engine_compile[:engine_compile.index('-MD')]
engine_compile = [arg for arg in engine_compile if arg not in {'-ICortexCommand.p', '-fpch-preprocess'}]
index = engine_compile.index('-include')
del engine_compile[index:index + 2]
engine_compile = [('-I' + str(repo / arg[4:])) if arg.startswith('-I../Source') else
                  ('-I' + str(repo / arg[2:])) if arg.startswith('-ISource') else arg for arg in engine_compile]
fixture_object = output / 'fixture.o'
subprocess.run(engine_compile + ['-c', str(source / 'RustKernelFixture.cpp'), '-o', str(fixture_object)], cwd=build, check=True)
for compiler, name in [('g++-15', 'cpp'), ('clang++', 'clang')]:
    subprocess.run([compiler, '-O3', '-std=c++20', '-mcpu=apple-m3', '-ffp-contract=off',
                    '-DKERNEL_PREFIX=' + name, '-c', str(source / 'RustKernel.cpp'), '-o', str(output / (name + '.o'))], check=True)
subprocess.run([str(pathlib.Path.home() / '.cargo/bin/rustc'), '-C', 'opt-level=3', '-C', 'target-cpu=native',
                '-C', 'panic=abort', '--crate-type', 'staticlib', str(source / 'RustKernel.rs'),
                '-o', str(output / 'librustkernel.a')], check=True)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'CortexCommand'], text=True).splitlines()[-1])
link[link.index('CortexCommand.p/Source_Main.cpp.o')] = str(fixture_object)
link += [str(output / 'cpp.o'), str(output / 'clang.o'), str(output / 'librustkernel.a')]
for variant in ['baseline', 'cached']:
    command = link.copy()
    command[command.index('-o') + 1] = str(output / variant)
    if variant == 'baseline':
        for name in ['Matrix', 'MOSprite']:
            idx = next(i for i, arg in enumerate(command) if arg.endswith('_' + name + '.cpp.o'))
            command[idx] = str(baseline / ('baseline-' + name + '.cpp.o'))
    subprocess.run(command, cwd=build, check=True)
