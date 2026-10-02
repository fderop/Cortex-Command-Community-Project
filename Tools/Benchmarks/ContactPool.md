# Rejected contact storage pool

`ContactPool.patch` preserves the tested candidate for `AtomGroup::Travel`.
It replaces the contact map and nested vectors with standard PMR containers and a thread-local `unsynchronized_pool_resource`.
The routine still clears the map before each call and uses the existing insertion and iteration operations.
`PushTravel` remains unchanged.
The accepted callback optimization does not include this candidate.

## Corrected results

`ContactPool-results.json` contains every sample from three alternating process pairs.
Each process records seven samples per fixture, with 4096 calls per sample.
The benchmark calls the compiled production routines through `CollisionCallbacks.cpp`.
Reset positions use external storage, and a size assertion prevents allocation through an undersized inherited entity pool.
All paired physics hashes and response counts match.
Allocation counts come from separate passes and match across all three runs.

Measurements use an Apple M3 Pro, GCC 15.2, C++20, and `-O3`.
Both links use the original `MovableObject.cpp`, `Matrix.cpp`, and `MOSprite.cpp` versions from baseline `41e6b7010817936e09d2929330b7519852a1fcca`.
Times are medians across 21 samples.

| Fixture | Baseline ms | PMR ms | Time reduction | Baseline allocations | PMR allocations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Dense, one body | 443.401 | 440.546 | 0.64% | 3,190,671 | 2,999,573 |
| Dense, eight bodies | 562.280 | 560.888 | 0.25% | 3,765,537 | 2,885,595 |
| Dense, 32 bodies | 822.813 | 823.772 | -0.12% | 4,719,757 | 2,699,957 |
| Terrain | 134.962 | 132.711 | 1.67% | 2,406,712 | 2,406,712 |
| Empty space | 58.366 | 57.145 | 2.09% | 0 | 0 |
| Push, terrain | 30.883 | 30.891 | -0.03% | 36,864 | 36,864 |
| Push, empty space | 17.924 | 17.822 | 0.57% | 0 | 0 |

The pool reduces dense allocation counts by 6–43%, but dense CPU differences remain smaller than the empty-space control difference.
The densest fixture becomes slightly slower.
These results do not support a useful CPU improvement, so the production patch excludes this candidate.
Original measurements from the undersized fixture remain invalid and are not included here.

## Source provenance

The recovered patch reconstructs the original saved candidate, with the resource named `hitMOStorage` in `Travel` only.
A rebuild uses the original release flags and maps the temporary source path to the original translation unit path.
Every Mach-O section has byte-identical contents and relocations between the recovered and saved objects.
This includes all three instruction sections, with 61,984 instruction bytes in total.
`ContactPool-provenance.json` records section hashes and both object hashes.

The complete object files differ, and their symbol tables differ.
This check establishes section contents and relocation parity, not complete container identity.
The corrected measurements above used the saved candidate object.
The reproduction runner completed a build-only check and produced the same section contents and relocations.
The source reconstruction required no additional timing runs.

## Reproduce

Create a release build with the normal engine dependencies.
Run the following command from the repository root:

```sh
python3 Tools/Benchmarks/ContactPool.py build /tmp/contact-pool \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --runs 3 --calls 4096
```

On macOS, expose the FMOD library before you run the command:

```sh
export DYLD_LIBRARY_PATH="$PWD/external/lib/macos"
```

The runner applies the patch to a temporary baseline source and compiles both versions with the same flags.
It also compiles the corrected fixture and the baseline callback implementation.
The repository's production files remain unchanged.
The output directory contains generated sources, objects, executables, raw logs, and `summary.json`.
The runner requires equal physics hashes and response counts.

For a build with other local optimizations, supply the pristine objects in both links:

```sh
python3 Tools/Benchmarks/ContactPool.py /path/to/build /tmp/contact-pool \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --replace-object Source_System_Matrix.cpp.o /path/to/baseline-Matrix.cpp.o \
  --replace-object Source_Entities_MOSprite.cpp.o /path/to/baseline-MOSprite.cpp.o \
  --runs 3 --calls 4096
```
