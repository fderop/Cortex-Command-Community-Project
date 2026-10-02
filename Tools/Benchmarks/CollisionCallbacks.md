# Collision callback allocations

Collision events originally construct script argument vectors before the script dispatcher finds that no callback exists.
Terrain events also construct a callback-name string at each invocation.
The changed event methods find the callback first and construct arguments only for a nonempty function list.
Each method reuses a constant callback-name string.
Collision state assignments and the existing script dispatcher remain unchanged.
Disabled callbacks retain the existing dispatcher behavior.

## Fixtures

The benchmark links the compiled engine objects and replaces `Main.cpp` with a fixture program.
It calls the production `Travel`, `PushTravel`, atom stepping, spatial grid, pixel queries, and collision response routines.
The fixture override of `CollideAtPoint` calls the production implementation before it records the response.

Each moving debris object contains 64 atoms. A 520-by-260 scene uses the production grid cell size of 20 pixels.
Dense fixtures collide with one, eight, or 32 separate sprite bodies.
Additional fixtures cover terrain collisions and empty space through both travel routines.
The fixture resets object positions and clears target impulse queues between calls.

The correctness pass records the collision order, both bodies' impulses, contact radii, and impulse factors.
It also records final positions, velocities, rotations, total impulses, remaining travel time, and target impulse queues.
The runner requires identical hashes and response counts for both versions.
Allocation counts include all C++ heap requests during the real routines, including aligned requests.
Allocation measurement and correctness checks occur outside the timed samples.

## Run

Create a release build with the normal engine dependencies.
Run the following command from the repository root:

```sh
python3 Tools/Benchmarks/CollisionCallbacks.py build /tmp/contact-storage \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --runs 3 --calls 4096
```

On macOS, expose the FMOD library before you run the command:

```sh
export DYLD_LIBRARY_PATH="$PWD/external/lib/macos"
```

The runner compiles both `MovableObject.cpp` versions with the same release flags.
All other compiled engine objects remain identical between versions.
The runner alternates process order between pairs and collects seven samples per fixture in each process.
Each sample contains 4096 travel calls, after 768 initial calls and a separate allocation pass.
The output directory contains both executables, raw logs, and `summary.json` with every sample.

For an existing build with other local optimizations, replace those objects in both links:

```sh
python3 Tools/Benchmarks/CollisionCallbacks.py /path/to/build /tmp/contact-storage \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --replace-object Source_System_Matrix.cpp.o /path/to/baseline-Matrix.cpp.o \
  --replace-object Source_Entities_MOSprite.cpp.o /path/to/baseline-MOSprite.cpp.o \
  --runs 3 --calls 4096
```

## Results

Results use an Apple M3 Pro, GCC 15.2, C++20, and a release build with `-O3`.
The engine objects came from a complete release build.
Both versions use pristine `Matrix.cpp` and `MOSprite.cpp` objects from the stated baseline.
Local macOS dependency and runtime support changes remain outside this commit.

The recorded results appear in `CollisionCallbacks-results.json`.

Times are medians across 21 samples. Allocation counts cover one separate pass of 4096 calls and match across all three runs.

| Fixture | Baseline ms | Changed ms | Time reduction | Baseline allocations | Changed allocations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Dense, one body | 449.362 | 392.405 | 12.68% | 3,190,671 | 309,291 |
| Dense, eight bodies | 570.135 | 517.821 | 9.18% | 3,765,537 | 971,963 |
| Dense, 32 bodies | 839.132 | 783.234 | 6.66% | 4,719,757 | 2,028,381 |
| Terrain | 135.726 | 92.532 | 31.82% | 2,406,712 | 0 |
| Empty space | 58.809 | 59.868 | -1.80% | 0 | 0 |
| Push, terrain | 31.162 | 32.629 | -4.71% | 36,864 | 36,864 |
| Push, empty space | 18.051 | 18.951 | -4.99% | 0 | 0 |

Every paired process produces identical physics hashes and collision response counts.
The dense time reductions persist across all three pairs.
The empty-space and push fixtures take 1.8–5.0% more time with the changed executable.
Their allocation counts remain identical.
The changed executable also changes code layout, but these measurements do not isolate the cause of those timing increases.

These fixtures measure the real physics routines with synthetic geometry.
They do not measure a full game frame or prove a frame-rate increase.
These fixtures contain no collision scripts. Scripted debris still pays the existing callback costs.

## Full game checks

`PerformanceStress.rte` contains the full game fixtures.
The callback fixture uses 24 debris objects with collision callbacks and eight objects with a `Create` callback only.
It disables callbacks at tick 80 and enables them at tick 160.
The probes remain alive and retain their initial `Create` count.

Both engine versions produced these identical results:

```text
LUA_CHECK_ENABLED mo=77134 terrain=56712 creates=32
LUA_CHECK_DISABLED_PASS
LUA_CHECK_PASS mo=152236 terrain=109160 creates=32
```

The debris workload creates 400 collidable stock gibs and an actual frag grenade explosion every 60 ticks.
The capture uses a fixed simulation seed at each tick and stops after 240 ticks.
Every particle count matches between versions in all three pairs.
The timing analysis excludes the first 60 ticks of each process.
The CSV file contains all 1440 raw tick records.
The game summary contains the callback results and timing statistics.

| Full game metric | Baseline ms | Changed ms |
| --- | ---: | ---: |
| Simulation mean | 2.610 | 2.565 |
| Simulation p95 | 8.746 | 9.078 |
| MovableMan mean | 2.420 | 2.374 |
| MovableMan p95 | 8.670 | 9.005 |

Mean simulation time decreased by 0.73–2.90% across the three pairs, with a combined reduction of 1.72%.
The combined p95 increased by 3.80%.
These game timings include draws inside `MovableMan::Update`, so render cadence also affects the results.
Three pairs do not establish a general frame-rate or latency improvement.

## Reproduce the game capture

First, compile the paired engine objects with `CollisionCallbacks.py`.
Then build the capture executables:

```sh
python3 Tools/Benchmarks/CaptureCollisionGame.py build /tmp/contact-storage
```

The helper writes `game-baseline` and `game-changed` into the output directory.
It adds timing and seed code to a temporary copy of `Main.cpp`.
It does not change the repository's game loop.
It also accepts object replacements as additional `NAME=PATH` arguments.

Copy `Tools/Benchmarks/PerformanceStress.rte` into a game runtime's `Mods` directory.
Configure `UserData/Settings.ini` with these properties:

```ini
LaunchIntoActivity = 1
DefaultActivityType = GAScripted
DefaultActivityName = Debris Performance Stress
DefaultSceneName = Grasslands
```

Run each capture executable from that runtime with `-cout`.
Each `PERF_CAPTURE` row contains the tick index, simulation microseconds, MovableMan microseconds, and particle count.
The executable prints `PERF_CAPTURE_DONE` after the capture.
Alternate executable order for three pairs.
Exclude ticks 0–59 from timing statistics.
Use the nearest-rank method for p95.

For the callback check, set `ScriptPath` to `PerformanceStress.rte/CallbackCheck.lua` in the fixture's `Activities.ini`.
Run both executables and compare the `LUA_CHECK` lines.
