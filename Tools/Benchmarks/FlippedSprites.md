The sprite benchmark calls the production `MOSRotating::Draw` function through 512 debris fixtures.
It links the existing release engine objects, with separate baseline and changed `MOSRotating` and `ContentFile` objects.
The fixtures load patterned BMPs through `ContentFile`.
Each sample contains 65,536 flipped color draws, with rotation and scene wrapping.
Each process includes one warmup sample and five recorded samples per workload.

The pixel checks cover 1,096 cases.
These cases include frame changes, flips, rotations, scales, scene seams, camera offsets, white flashes, material silhouettes, and door silhouettes.
They also cover scratch bitmap changes, repeated sprite edits, copies of edited sprites, reloads, changed source dimensions, and memory bitmap ownership transfer.
The GPU checks call the production transparent draw path in a real OpenGL context.
They read six framebuffer images and require visible pixels.

Baseline and changed builds produced the same CPU hash: `15073311523546579531`.
Both builds produced the same GPU hash: `15926731766284440575`.
All six workload pixel hashes also matched across all processes.
The GPU startup prints one `GLAD: ERROR 1280 in glGetIntegerv!` message in both builds.
The six draws produce visible pixels and matching framebuffer hashes.

The measurements used an Apple M3 Pro with GCC 15.2.0, C++20, `-O3`, and `NDEBUG`.
The baseline revision was `41e6b7010817936e09d2929330b7519852a1fcca`.
Three process pairs ran in this order: baseline/changed, changed/baseline, baseline/changed.
The table uses the median of 15 samples per workload.

| Sprite size | Unique assets | Baseline, ms | Changed, ms | CPU time reduction | Added bitmap storage |
| --- | ---: | ---: | ---: | ---: | ---: |
| 12×12 | 1 | 22.355 | 15.843 | 29.1% | 344 B |
| 24×24 | 8 | 90.951 | 61.750 | 32.1% | 6.8 KiB |
| 64×64 | 8 | 484.790 | 331.360 | 31.6% | 36.8 KiB |
| 128×128 | 8 | 1593.830 | 1028.640 | 35.5% | 136.8 KiB |
| 24×24 | 512 | 88.733 | 66.646 | 24.9% | 436 KiB |
| 64×64 | 512 | 491.085 | 346.401 | 29.5% | 2.30 MiB |

The added bitmap storage includes pixel data, row pointers, and the 104-byte `BITMAP` header on this machine.
It excludes allocator overhead and the map nodes and buckets.
The map keeps one entry per loaded bitmap, with a null value until the first cached flip.
The cache retains flipped bitmaps until reload, ownership transfer, or content cleanup.

Cold measurements cover the first 512 draws after the source files load.
With 512 unique assets, the 24×24 workload increased from 0.839 ms to 0.889 ms.
The 64×64 workload increased from 4.054 ms to 4.241 ms.
These costs include the first flip allocations.
The workloads with shared assets also improved on their first draw pass.

The full game ran three alternating pairs with 400 collidable stock gibs and a frag grenade explosion every 60 ticks.
The included activity uses Grasslands and the stock `Gib Metal Grey Small A` preset.
The capture seeds the random generator with `731 + tick` and stops after 240 simulation ticks.
The comparison excludes the first 60 ticks.
All six captures had identical particle counts at every tick, with a peak of 883 particles.

| Full game measurement | Baseline | Changed |
| --- | ---: | ---: |
| Median of color draw means | 129.394 µs | 102.106 µs |
| Median of simulation means | 2534.261 µs | 2497.439 µs |
| Median of simulation 95th percentiles | 8072 µs | 8177 µs |

Color drawing took 21.1% less CPU time.
The total simulation difference was about 1.5%, with variation between runs.
Travel still took about 2 ms per simulation tick.
These measurements support a rendering improvement, with limited evidence of a total simulation improvement.
Whole frame measurements had a timer outlier and do not support a frame rate claim.

Raw draw samples are in [draw.csv](FlippedSpritesResults/draw.csv).
Raw game samples are in [game.csv](FlippedSpritesResults/game.csv).
The results directory also contains the workload summaries and the GPU outputs.

Build the engine with Meson and Ninja before you compile the benchmark.
Run this command from the repository root:

```sh
python3 Tools/Benchmarks/FlippedSprites.py build /tmp/flipped-sprites
```

The runner builds the baseline sources from Git and the changed sources from the current working tree.
If the reused engine build contains other changes, pass `--replace-object Name=/absolute/path/to/pristine/object.o` for each affected object.
The recorded comparison used pristine `Matrix` and `MOSprite` objects.
The runner adds only static members to `ContentFile`; engine object layouts stay compatible.

On macOS, run the pixel checks and GPU checks with the engine library directory:

```sh
DYLD_LIBRARY_PATH="$PWD/external/lib/macos" /tmp/flipped-sprites/baseline --check
DYLD_LIBRARY_PATH="$PWD/external/lib/macos" /tmp/flipped-sprites/changed --check
DYLD_LIBRARY_PATH="$PWD/external/lib/macos" /tmp/flipped-sprites/baseline --gpu
DYLD_LIBRARY_PATH="$PWD/external/lib/macos" /tmp/flipped-sprites/changed --gpu
```

For CPU measurements, omit the final argument.
Repeat each executable three times and alternate the process order.
On Linux, use `LD_LIBRARY_PATH` with the engine library directory.
The fixtures disable the engine file path cache because they create new BMP files after startup.
The fixture size assertion protects the fixed object pool allocation used by `MOSRotating`.

For a full game capture, run this command with the same object replacement arguments:

```sh
python3 Tools/Benchmarks/CaptureSpriteGame.py build /tmp/flipped-sprites
```

Copy [PerformanceStress.rte](PerformanceStress.rte) into the runtime `Mods` directory.
In `Userdata/Settings.ini`, set `LaunchIntoActivity = 1`, `DefaultActivityType = GAScripted`, and `DefaultActivityName = Debris Performance Stress`.
Set `DefaultSceneName = Grasslands`.
Run each capture executable from the normal game runtime directory, with the normal libraries and data.
On macOS, copy the executable into the existing app bundle before you run it.
Capture the `-cout` output until `PERF_CAPTURE_DONE`, then stop the process.
Repeat three pairs and compare the `PERF_PROFILE` rows from ticks 60 through 239.
