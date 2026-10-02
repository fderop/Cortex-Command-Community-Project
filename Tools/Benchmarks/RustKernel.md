The Rust prototype does not justify a production rewrite of this sprite query. Rust increases the adapter time by approximately 4% against equivalent C++.

The prototype replaces the pixel kernel inside a real `MOSprite::HitTestAtPixel` fixture. It retains the engine scene manager, sprite objects, and Allegro bitmap.
The C++ adapter retains the existing scene-distance and validity operations. The kernel performs radius rejection, inverse rotation, flips, pixel floors, bounds checks, and mask comparison.

All language controls use the same C ABI, `repr(C)` layout, bitmap rows, coefficients, query order, and arithmetic. Each scalar call crosses that ABI.
Each batch call processes 65,536 queries through the same ABI. C++ and Rust use explicit fused multiply-add operations that match the native engine instructions.
The C++ controls disable other multiply-add contraction. The fixture compares actual boolean results rather than approximate coordinates.

The first correctness run rejected incorrect fixture state extraction at zero rotation. The final fixture computes cold coefficients directly and preserves the source matrix.
Native disassembly also confirmed fused arithmetic before the final measurements. No measurements from the initial fixture contribute to the results.

All six final runs matched 29,491,200 engine query results per run. The behavior hash was `5636519390183985600`, which matches the existing `CollisionTransforms` fixture.
The checks cover four rotation phases, cold and warm coefficients, both horizontal orientations, matrix flips, mask pixels, sprite bounds, and radius rejection.
Each language batch also matched all 65,536 scalar outputs. Every timed phase reported 3,291,520 hits for 4,194,304 queries.

The measurements used an Apple M3 Pro with macOS 26.5.2. The engine build used GCC 15.2.0, release mode, and `-O3`.
The kernel controls used GCC 15.2.0, Apple Clang 21.0.0, and Rust 1.92.0. All three used optimization level 3 and the native M3 CPU target.
The experiment used three alternating pairs of pristine and cached engine binaries. Each process rotated the language order and recorded seven samples after one warmup.

The table shows median times in milliseconds across 21 samples. Each sample contains 4,194,304 queries.

| Work | GCC C++ | Clang C++ | Rust |
| --- | ---: | ---: | ---: |
| Real query adapter, cached engine | 33.009 | 32.846 | 34.281 |
| Scalar kernel, prepared queries | 8.295 | 8.180 | 8.111 |
| Batch kernel, prepared queries | 7.453 | 7.564 | 7.231 |
| Query preparation and batch kernel | 21.769 | 21.867 | 21.485 |

The median paired Rust adapter time increases were 4.13% against GCC and 4.24% against Clang. The pristine binary showed similar increases of 4.46% and 4.52%.
Prepared Rust batches reduced kernel time by 2.93% against GCC and 4.35% against Clang. Query preparation reduced these advantages to 1.05% and 1.79%.
Prepared scalar Rust calls reduced time by 2.54% against GCC and 1.20% against Clang. These small compiler differences did not improve the complete adapter.

The original engine method took 65.629ms in the pristine binary and 39.951ms with the accepted matrix cache change.
The cache change is commit `420de0a39`. Its benefit comes from C++ matrix copies and cached coefficients.
The direct adapters also bypass Allegro bank callbacks and read CPU bitmap rows directly. These data and cache changes apply equally to C++ and Rust.
Therefore, the large reductions against the pristine engine do not measure a language benefit.

The fixture allocates sprites, query buffers, and output buffers before the timer starts. Timed kernels allocate nothing.
The preparation phase rebuilds sprite records and scene distances for every batch. It includes query preparation, output writes, and the output sum.
The prepared batch phase excludes preparation. Its smaller ABI call count does not represent the current engine query sequence.

The raw files retain every sample, including several scheduling outliers. For example, one cached GCC adapter sample reached 54.976ms.
The summary includes means, medians, complete ranges, and paired language changes. No sample was removed.

This fixture uses finite coordinates, a CPU bitmap with 8-bit pixels, and a scene without wrapping. It does not prove equivalence for other bitmap types.
The C++ controls also cross an external ABI, so they do not receive a production C++ inlining advantage.
The measurements do not establish frame-rate changes, complete physics costs, or a benefit from a full engine rewrite.

Keep the production engine in C++ for this query. Consider query preparation and bitmap access separately if future native profiles justify those changes.
The existing C++ cache change already provides a measured improvement without a Rust toolchain or a new ABI boundary.

To reproduce the experiment, use an existing release engine build with `compile_commands.json`. Apply the matrix cache change to that build.
Preserve the pristine `Matrix` and `MOSprite` objects from revision `41e6b7010817936e09d2929330b7519852a1fcca`.
Name those objects `baseline-Matrix.cpp.o` and `baseline-MOSprite.cpp.o` in the baseline object directory.
Run these commands from the repository root:

```sh
python3 Tools/Benchmarks/RustKernel.py BUILD_DIRECTORY BASELINE_OBJECT_DIRECTORY OUTPUT_DIRECTORY
python3 Tools/Benchmarks/RustKernelRun.py OUTPUT_DIRECTORY ALLEGRO_LIBRARY_DIRECTORY Tools/Benchmarks/RustKernelResults
```

The builder compiles the Rust static library and both C++ controls. It links two fixtures with the existing engine objects.
Use `--summarize-only` on the second command to process saved samples without another timing run.

The committed measurements used these local directories:

```sh
python3 Tools/Benchmarks/RustKernel.py ../matrix/build ../artifacts/matrix ../artifacts/rust
python3 Tools/Benchmarks/RustKernelRun.py ../artifacts/rust ../matrix/external/lib/macos Tools/Benchmarks/RustKernelResults
```

The experiment changes only benchmark files. It introduces no production source changes or production dependencies.
