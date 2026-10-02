The segment reset experiment is rejected. Production code stays at `41e6b7010817936e09d2929330b7519852a1fcca`.

The candidate sets `stepsOnSeg = 0` before each `SetupSeg` loop in `AtomGroup::Travel`.
The runner creates this candidate from the baseline source in the output directory.
It does not edit the repository source.

The fixture calls the real engine collision routines. It links all engine objects and replaces `Main.cpp` with the fixture.
The baseline and candidate use identical objects except for `AtomGroup.cpp`.
Both variants use the original `Matrix.cpp` and `MOSprite.cpp` objects.
The measurement used GCC 15.2, a release build, and an Apple M3 Pro.

These CPU measurements and traces replace the invalid results from the original fixture.
The original debris fixture added a field beyond the fixed allocation size inherited from `MOSRotating`.
The corrected fixture stores reset positions outside the entity and requires equal fixture and base sizes at compilation.
The other heap fixtures add no fields.
The full-game capture uses actual engine entities and remains valid.

The correctness comparison hashes collision order, impulses, contact radii, impulse factors, target impulses, and final movement state.
Each mode uses 256 calls for this comparison.
Seven of fifteen modes differ: dense-1, dense-8, dense-32, terrain, spin-dense, rebounds, and deceleration.
Air, high rotation in air, high rotation against terrain, PushTravel, and isolated zero-step and one-step movement match.

The trace build records the scheduled steps and the longest current atom trajectory.
Trace measurements are separate from performance measurements.
In the first dense-1 call, the first collision leaves 10 milliseconds of travel time.
The next segment needs five steps. The baseline retains twelve scheduled steps.
Its next collision occurs at step index two and consumes `2 / 12` of the segment time.
The candidate uses five scheduled steps. Its next collision occurs at step index zero and consumes no time.
Repeated collisions then reach the existing eleven-collision limit.

| First dense-1 call | Baseline | Candidate |
| --- | ---: | ---: |
| Final horizontal position, pixels | 128.565247 | 126.250000 |
| Final horizontal velocity, meters/second | 16.948967 | 7.651761 |
| Remaining travel time, seconds | 0 | 0.010000 |

The one-step halt condition also changes behavior.
Across 256 dense-32 calls, the baseline never activates this condition. The candidate activates it 220 times.
The fast corridor case exercises repeated terrain responses. Its final position and velocity also differ.
The zero-length final rotation segment matches, although the candidate removes its extra step loop.

Five alternating baseline/candidate process pairs supply the performance measurements.
Each process records seven samples of 1,024 calls per mode, after warmup.
The table gives medians across 35 samples per variant.
A negative reduction means that the candidate takes more time.

| Mode | Baseline ms | Candidate ms | Time reduction | Behavior matches |
| --- | ---: | ---: | ---: | --- |
| dense-1 | 111.216 | 180.803 | -62.57% | No |
| dense-8 | 140.873 | 223.310 | -58.52% | No |
| dense-32 | 206.325 | 327.585 | -58.77% | No |
| terrain | 32.854 | 27.924 | 15.01% | No |
| spin-air | 49.007 | 47.136 | 3.82% | Yes |
| spin-dense | 134.837 | 55.985 | 58.48% | No |
| spin-terrain | 25.786 | 26.249 | -1.80% | Yes |
| rebounds | 74.559 | 33.239 | 55.42% | No |
| deceleration | 24.292 | 21.652 | 10.87% | No |
| spin-zero-tail | 15.828 | 14.284 | 9.75% | Yes |

One full-game process pair used stock debris and actual frag grenade explosions.
The activity creates 400 collidable gibs and a grenade explosion every 60 ticks.
The capture seeds the random generator with `731 + tick` and records 240 simulation ticks.
Particle counts differ on 239 ticks. The first difference occurs at tick 1: 487 particles versus 484.
After the first 60 ticks, mean movable update time increases from 2.392 milliseconds to 2.692 milliseconds.
These times describe different workloads. They do not establish a speed difference with equivalent physics.
The capture measures engine update time, with normal drawing cadence. It does not measure FPS.

The JSON files contain the complete measurements and correctness results.
The trace text files show the first call for every mode and the aggregate segment counters.
The game CSV contains every captured tick.

To reproduce the fixture, use a compiled release build with `compile_commands.json` and Ninja.
If the build contains other optimizations, replace their objects with baseline objects.
On macOS, set `DYLD_LIBRARY_PATH` to the FMOD library directory before the commands.

```sh
python3 Tools/Benchmarks/MovementSegments.py BUILD OUTPUT \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --runs 5 --calls 1024

python3 Tools/Benchmarks/MovementSegments.py BUILD TRACE_OUTPUT \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --trace --runs 1 --calls 64
```

Use `--replace-object Source_System_Matrix.cpp.o PATH` and `--replace-object Source_Entities_MOSprite.cpp.o PATH` for the isolated comparison.
The candidate source and object remain in `OUTPUT` for reproduction.
The trace runner reports times, but the recorded trace results omit them.

For the full-game comparison, copy `PerformanceStress.rte` into the runtime's `Mods` directory.
Set these entries in `Userdata/Settings.ini`:

```ini
LaunchIntoActivity = 1
DefaultActivityType = GAScripted
DefaultActivityName = Debris Performance Stress
DefaultSceneName = Grasslands
```

Then compile the seeded capture and run both variants.
The capture compiler accepts additional `OBJECT_NAME=PATH` arguments for baseline object replacements.
The macOS runner copies each variant into the supplied runtime application.

```sh
python3 Tools/Benchmarks/CaptureMovementGame.py BUILD OUTPUT
python3 Tools/Benchmarks/RunMovementGame.py RUNTIME OUTPUT/game-baseline baseline.log
python3 Tools/Benchmarks/RunMovementGame.py RUNTIME OUTPUT/game-changed changed.log
```

A safe optimization needs to preserve the movement schedule and collision responses.
This reset requires a separate physics change before it can serve as an optimization.
