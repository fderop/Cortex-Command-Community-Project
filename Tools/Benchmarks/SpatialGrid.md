# Spatial grid costs

The grid originally calculates each added cell ID once per included team and again for the used-cell set.
The change calculates that ID once and inserts the object into each included team's cell.
In-range cell coordinates also bypass the two modulo operations.

The cell size remains 20 pixels.
The change preserves wrapping, duplicate entries, team filters, and insertion order within each cell.
`SceneMan::GetMOIDPixel` and `MovableMan::GetMOIDPixel` retain their existing behavior.
In particular, a zero-scale candidate still returns `g_NoMOID` immediately.
Smaller cells can change that behavior, so this change retains the existing cell layout.

## Production routine fixture

`SpatialGrid.cpp` calls the actual grid, scene pixel query, and sprite hit-test routines.
The runner links the compiled engine objects and replaces the normal entry point with this fixture.
Each heap fixture contains no additional fields and requires the same size as its pooled base class.

An independent reference constructs the original cell vectors.
The fixture compares exact vectors, including duplicate entries and order, for each query.
It covers negative coordinates, multiple wraps, seams, nondivisible scene sizes, inactive teams, ignored teams, and both collision flags.
Multiple registrations, reset, flipped sprites, rotation, normal scale, small scale, large scale, and zero scale also receive checks.
The paired runner requires identical sprite query hashes and timing checksums.

The timing pass removes the oversized correctness rectangles first.
Dense queries use 256 sprites in a small region.
Sparse queries address empty cells.
The rebuild pass adds all 256 sprites or eight seam sprites after each reset.
Each rebuild sample contains 1171 resets.
Each query sample contains 300000 calls.
One initial sample precedes seven timed samples per process.
Three process pairs alternate execution order.

Results use an Apple M3 Pro, GCC 15.2, C++20, and release flags with `-O3`.
Both versions use pristine engine objects from `41e6b7010817936e09d2929330b7519852a1fcca`.
Local macOS dependency support changes remain outside this commit.
Times are medians across 21 samples.
`SpatialGrid-results.json` contains every sample and correctness hash.

| Routine | Baseline ms | Changed ms | Time reduction |
| --- | ---: | ---: | ---: |
| Dense grid lookup | 0.985 | 0.717 | 27.21% |
| Sparse grid lookup | 1.130 | 0.843 | 25.40% |
| Dense scene pixel query | 37.582 | 35.903 | 4.47% |
| Sparse scene pixel query | 1.537 | 1.559 | -1.43% |
| Seam scene pixel query | 9.283 | 8.631 | 7.02% |
| Dense grid rebuild | 16.322 | 8.901 | 45.47% |
| Sparse grid rebuild | 1.575 | 1.510 | 4.13% |

The sparse scene query regresses by 0.022 ms per 300000 calls.
The dense rebuild and grid lookup gains persist across all three process pairs.

## Full game capture

The workload creates 2000 stock metal gibs and an actual frag grenade explosion every 60 ticks.
The collidable scenario explicitly sets `GetsHitByMOs = true` on each gib.
The stock-default scenario retains the preset's `GetsHitByMOs = false` value.
Both scenarios retain `HitsMOs` from the preset.

Each process uses a fixed random seed per simulation tick and captures 240 ticks.
The timing analysis excludes ticks 0–59.
Three process pairs alternate execution order for each scenario.
Every paired particle sequence matches across all 240 ticks.
`SpatialGrid-game-results.csv` contains all 2880 tick records.
`SpatialGrid-game-summary.json` contains the per-process and combined statistics.
Percentiles use the nearest-rank method.

| Scenario and metric | Baseline ms | Changed ms |
| --- | ---: | ---: |
| Collidable simulation mean | 9.259 | 9.169 |
| Collidable simulation p95 | 57.324 | 55.749 |
| Collidable simulation maximum | 130.287 | 129.456 |
| Collidable travel mean | 8.365 | 8.260 |
| Stock-default simulation mean | 8.661 | 8.543 |
| Stock-default simulation p95 | 10.052 | 9.943 |
| Stock-default simulation maximum | 33.465 | 34.648 |
| Stock-default travel mean | 5.977 | 5.846 |

Combined mean simulation time decreases by 0.97% for collidable debris and 1.35% for stock-default debris.
One collidable pair becomes 0.19% slower, while the other two become 2.23% and 0.85% faster.
The stock-default pairs improve by 0.06%, 3.83%, and 0.07%.
These small gains overlap process variation.
The stock-default maximum increases by 1.183 ms.
The measurements support lower grid costs, but three pairs do not establish a general frame-rate or latency improvement.

## Reproduce

Create a release build with the normal engine dependencies.
On macOS, expose the FMOD library:

```sh
export DYLD_LIBRARY_PATH="$PWD/external/lib/macos"
```

Run the paired production fixture:

```sh
python3 Tools/Benchmarks/SpatialGrid.py build /tmp/spatial-grid \
  --baseline-ref 41e6b7010817936e09d2929330b7519852a1fcca \
  --runs 3 --calls 300000
```

For a build with other source changes, supply `--replace-object NAME PATH` to both helpers for each affected engine object.
The recorded runs replace `Source_System_Matrix.cpp.o` and `Source_Entities_MOSprite.cpp.o` with pristine baseline objects.

Copy `Tools/Benchmarks/PerformanceStress.rte` into a runtime's `Mods` directory.
Configure `UserData/Settings.ini` with these properties:

```ini
LaunchIntoActivity = 1
DefaultActivityType = GAScripted
DefaultActivityName = Debris Performance Stress
DefaultSceneName = Grasslands
```

Run both game scenarios:

```sh
python3 Tools/Benchmarks/SpatialGridGame.py build /tmp/spatial-grid /path/to/runtime \
  --runs 3 --wave-size 2000
python3 Tools/Benchmarks/SpatialGridGame.py build /tmp/spatial-grid /path/to/runtime \
  --runs 3 --wave-size 2000 --native-default
```

The helper compiles temporary timing copies of `Main.cpp` and `MovableMan.cpp`.
It leaves the repository sources unchanged and restores the original workload after all captures.
Each `PERF_PROFILE` row records simulation, MovableMan, travel, color draw, clear, and grid-wait times in microseconds.
These timings cover simulation work and do not measure complete frame time.
