# Combined debris optimizations

Four changes reduce repeated work without changing the tested physics or sprite pixels:

| Change | Draft PR |
| --- | --- |
| Preserve cached matrix coefficients during copies | [285](https://github.com/cortex-command-community/Cortex-Command-Community-Project/pull/285) |
| Avoid argument allocations for absent collision callbacks | [286](https://github.com/cortex-command-community/Cortex-Command-Community-Project/pull/286) |
| Cache horizontal flips of shared sprite images | [287](https://github.com/cortex-command-community/Cortex-Command-Community-Project/pull/287) |
| Calculate each registered grid cell once | [288](https://github.com/cortex-command-community/Cortex-Command-Community-Project/pull/288) |

The integration branch combines these changes from baseline `41e6b7010817936e09d2929330b7519852a1fcca`.
The runner freshly compiles the six affected source files for both versions.
It also builds identical temporary timing instrumentation for both versions.
Both versions share all other engine objects and build flags.
The added `ContentFile` members are static, so instance layouts remain unchanged.

## Gameplay measurements

The test uses an Apple M3 Pro, GCC 15.2, release optimization with `-O3`, and a 960×540 window.
Each scenario creates 2,000 stock metal gibs and a frag grenade explosion every 60 simulation ticks.
The stock scenario retains the gib preset's collision flags.
The collidable scenario explicitly enables `GetsHitByMOs`.
Both scenarios use Grasslands and a fixed random seed for each simulation tick.

Three process pairs alternate baseline and changed execution order.
No other benchmark or compilation runs during these captures.
Each process captures 240 ticks.
The timing analysis excludes ticks 0–59.
The table combines 540 measured ticks per version and scenario.
Percentiles use the nearest-rank method.

| Scenario and metric | Baseline ms | Combined ms | Time reduction |
| --- | ---: | ---: | ---: |
| Stock simulation mean | 8.471 | 7.878 | 7.00% |
| Stock simulation p95 | 9.920 | 9.204 | 7.22% |
| Stock simulation maximum | 32.446 | 32.426 | 0.06% |
| Stock travel mean | 5.834 | 5.439 | 6.78% |
| Stock color drawing mean | 1.252 | 1.035 | 17.30% |
| Collidable simulation mean | 9.126 | 8.564 | 6.16% |
| Collidable simulation p95 | 56.488 | 49.447 | 12.46% |
| Collidable simulation maximum | 130.192 | 122.540 | 5.88% |
| Collidable travel mean | 8.225 | 7.675 | 6.69% |
| Collidable color drawing mean | 0.151 | 0.123 | 18.95% |

Every process pair improves mean simulation time.
Stock improvements range from 5.82% to 8.39%.
Collidable improvements range from 5.66% to 6.59%.
The mean particle counts are 4,349 for stock debris and 631 for collidable debris.
Collidable debris quickly destroys other debris, so these scenarios have different workloads.

All 1,440 paired particle counts match, including the excluded warmup ticks.
The combined executable also matches all 1,096 CPU sprite checks and six GPU readback checks from the sprite fixture.
The individual reports contain additional collision-state, callback, matrix, and grid checks.
Particle counts alone do not establish complete world-state equality.

These measurements cover simulation work, including its color drawing stage.
They do not measure complete frame time or establish an FPS improvement.
The changes reduce measured costs, but substantial explosion hitches remain.
Three pairs on one platform do not establish results for every map, mod, or computer.

The committed CSV files contain all 2,880 tick records.
The JSON files contain per-process statistics, aggregate statistics, and combined pixel-check results.
GPU checks emit an existing `GLAD: ERROR 1280 in glGetIntegerv!` startup diagnostic in both versions.
Visible pixel checks still pass.

## Reproduce

Build the integration branch in release mode with the normal engine dependencies.
Copy `Tools/Benchmarks/PerformanceStress.rte` into a private runtime's `Mods` directory.
Configure the runtime as described in `SpatialGrid.md`.
On macOS, expose the FMOD library:

```sh
export DYLD_LIBRARY_PATH="$PWD/external/lib/macos"
```

Run the collidable scenario:

```sh
python3 Tools/Benchmarks/CombinedGame.py build /tmp/combined-debris /path/to/runtime --runs 3
```

Reuse those executables for the stock scenario:

```sh
python3 Tools/Benchmarks/CombinedGame.py build /tmp/combined-debris /path/to/runtime --skip-build --runs 3 --native-default
```

The runner restores the original stress script after each successful capture suite.
`CortexCommand-playable` contains the combined changes without capture instrumentation or automatic exit.
Local macOS dependency support changes are outside the optimization commits.
