The collision benchmark calls the production `MOSprite::HitTestAtPixel` function through 512 overlapping debris fixtures.
Each fixture uses a 64×64 bitmap with opaque and transparent pixels.
The benchmark measures rotations with valid cached coefficients and rotations with invalidated coefficients separately.
Each case includes seven samples after one warmup sample. Each sample contains 8,388,608 queries.

The behavior checks cover 29,491,200 queries with different rotations, flips, and fractional positions.
The benchmark also prints a hash of the float bits from the production Matrix operators.
Four concurrent readers run the same queries and compare hit counts.
The benchmark makes sure that the source matrices retain their cache state.
An error returns a nonzero exit code. Baseline and changed builds must produce the same hashes and hit counts.

Build the engine with Meson and Ninja before you compile the benchmark.
The runner uses GCC-compatible compile commands and the existing engine objects.
Run the following command from the repository root:

```sh
python3 Tools/Benchmarks/CollisionTransforms.py build /tmp/collision-transforms
```

On macOS, run the benchmark with the engine library directory:

```sh
DYLD_LIBRARY_PATH="$PWD/external/lib/macos" /tmp/collision-transforms > /tmp/collision-transforms.txt
```

On Linux, run the benchmark with the engine library directory:

```sh
LD_LIBRARY_PATH="$PWD/external/lib/linux/x86_64" /tmp/collision-transforms > /tmp/collision-transforms.txt
```

For a comparison, build the benchmark from each revision with the same release compiler settings.
Repeat each executable three times. Alternate the order of the executables.
Compare the median sample times for each cache state.

This benchmark measures production collision queries with controlled fixtures.
It does not measure gameplay frame times or the complete physics simulation.
The fixtures use a synthetic bitmap and a scene without terrain wrapping.
