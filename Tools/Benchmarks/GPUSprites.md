# GPU sprite raster experiment

GPU rasterization helps larger sprites in this experiment. Small debris still costs less on the CPU before the scene upload.
This commit adds a benchmark and measured results. It changes no production renderer code.

The experiment uses an Apple M3 Pro, GCC 15.2, and an `-O3` release build.
The baseline is `41e6b7010817936e09d2929330b7519852a1fcca`.
The OpenGL renderer reports `Apple M3 Pro` and `4.1 Metal - 90.5`.

## Completed frame costs

Each row contains 512 ordered sprite draws into a 512×512 target.
The table gives median milliseconds from 14 samples across two separate processes.
Each sample contains eight frames. Each GPU frame ends with `glFinish()`.
The experiment rotates the measurement order between samples.

| Sprite | Unique textures | CPU raster | GPU raster + finish | CPU raster + upload + display + finish |
|---|---:|---:|---:|---:|
| Pattern, 12×12 | 1 | 0.205 | 0.392 | 0.542 |
| Pattern, 24×24 | 8 | 0.738 | 0.803 | 1.114 |
| Pattern, 64×64 | 8 | 3.872 | 1.180 | 4.562 |
| Pattern, 128×128 | 8 | 11.373 | 1.398 | 12.050 |
| Pattern, 24×24 | 512 | 0.734 | 0.944 | 1.095 |
| Pattern, 64×64 | 512 | 3.848 | 1.217 | 4.529 |
| WeaponGibA, 14×8 | 1 | 0.168 | 0.345 | 0.506 |
| RocketAHullGibBigA, 31×69 | 1 | 2.378 | 0.598 | 3.067 |

The CPU raster column includes the bitmap clear and actual `MOSRotating::Draw(g_DrawColor)` calls.
These calls include the existing flip bitmap, Allegro rasterization, and drawing registration.
The GPU column includes the framebuffer clear, texture cache lookup, quad preparation, submission, and GPU completion.
The engine's `DrawTexturePro` still computes four vertex positions on the CPU. The GPU performs the texture rasterization.

The last column adds the real `BigTexture::Update` path, a display draw, and GPU completion to CPU rasterization.
The upload covers the complete 512×512 target through the engine's PBO path.
Both GPU paths display palette indices as grayscale. Both use index zero as transparency.
The experiment excludes the shared palette conversion and final scene composition.

The warm GPU path saves 0.161 ms against the upload pipeline for small WeaponGibA sprites.
It costs 0.177 ms more than their isolated CPU rasterization.
Thus, the upload saving matters more than the raster saving for this small sprite.
The larger hull saves 2.469 ms against this isolated upload pipeline.
These differences describe this fixture. They do not measure game FPS.

## Cold textures

The cold GPU measurement includes texture creation, source upload, the first draw, and GPU completion.
The context and shader are already warm. Thus, this measurement excludes context startup and shader compilation.

| Sprite | Unique textures | Cold CPU raster, run 1 / run 2 | Cold GPU, run 1 / run 2 |
|---|---:|---:|---:|
| Pattern, 24×24 | 512 | 0.873 / 0.870 | 4.194 / 4.309 |
| Pattern, 64×64 | 512 | 3.969 / 3.929 | 5.106 / 5.198 |
| WeaponGibA, 14×8 | 1 | 0.263 / 0.260 | 0.700 / 0.755 |
| RocketAHullGibBigA, 31×69 | 1 | 2.612 / 2.575 | 0.804 / 0.793 |

Many unique textures make the first GPU frame slower than CPU rasterization.
Texture reuse is essential to the warm result.

## Pixel observations

Each process records 41 pixel comparisons through actual `glReadPixels` calls.
Readback occurs outside the timing intervals. The experiment does not measure a GPU-to-CPU replacement pipeline.
Both processes produce identical pixel hashes and difference counts.
The comparisons cover horizontal flips, asymmetric pivots, four angles, three scales, overlapping draws, and real sprite data.

Unscaled sprites at zero rotation match CPU pixels exactly for both flip states.
The half-turn cases at unit scale also match exactly.
All eight CPU upload and display comparisons match CPU pixels exactly.
The ordered overlap case differs from Allegro at four pixels.

GPU output does not reproduce every Allegro pixel at other scales and angles.
For example, an unrotated half-scale sprite differs at 124 pixels despite an exact match with the float oracle.
Allegro uses fixed-point scan conversion. The GPU uses floating-point vertices and texture sampling.
This sampling difference is separate from an incorrect rotation sign, pivot, scale, or flip.

The independent float oracle uses inverse transforms at pixel centers.
The `nonboundary_oracle_gpu_diff` field excludes samples within 0.001 source pixels of an integer boundary.
All 24 individual transform cases and the overlap case have zero differences outside that boundary range.
The batches retain up to 18 differences outside that range among 262,144 target pixels.
The renderer reports four raster subpixel bits.
These residual differences are consistent with finite raster precision. This experiment does not prove their cause.

The benchmark reports differences as observations. A zero exit code does not certify exact pixel compatibility.
The existing GLAD startup message reports `ERROR 1280 in glGetIntegerv`.
Both final runs report `final_gl_error=0`.

## Integration limits

The current opaque path draws into the scene-wide CPU MOColor bitmap.
`g_DrawTrans` already uses GPU draws, but it follows a different path.
A direct GPU call during the opaque draw targets the wrong framebuffer and breaks scene composition.
The experiment therefore provides no safe production call replacement.

A GPU color layer must preserve the order of sprites, CPU particles, wounds, and attachments.
It must preserve scene wrapping and the camera update that occurs after color drawing.
GPU-only sprite batches in this fixture do not exercise those interactions.
The CPU bitmap also supports material and collision-related passes that this fixture leaves on the CPU.

CPU consumers need valid palette indices with the same transparency rules.
A hybrid path can require readback and additional uploads between ordered CPU and GPU draws.
This experiment excludes those costs. Its completed GPU costs describe an isolated GPU-only batch.
Sprite edits, content reloads, bitmap destruction, and texture cache lifetimes also need explicit integration work.

Existing whole-game profiles place color drawing near 1.3 ms in stock 2000-object gib waves.
The forced-collidable profile places color drawing near 0.16 ms, while travel and simulation dominate.
These stage times bound possible color-drawing savings before GPU overhead.
The larger synthetic sprites do not establish an equivalent whole-game gain.

The measured benefit supports a future GPU color-layer design for larger sprites.
It does not justify a renderer rewrite for small debris alone.

## Reproduction

Use an existing release build with `compile_commands.json` and its matching runtime data.
The runner replaces `Main.cpp` with the benchmark and links the other engine objects.
For a build with experimental objects, pass pristine object replacements.

```sh
python3 Tools/Benchmarks/GPUSprites.py ../matrix/build ../artifacts/gpu \
  --replace-object Matrix=../artifacts/matrix/baseline-Matrix.cpp.o \
  --replace-object MOSprite=../artifacts/matrix/baseline-MOSprite.cpp.o
cd ../runtime
DYLD_LIBRARY_PATH=../matrix/external/lib/macos ../artifacts/gpu/GPUSprites > ../artifacts/gpu/new-run.txt 2>&1
```

The runner uses the benchmark worktree's source headers and removes the build's precompiled header flags.
The fixture subclasses add no instance fields because pooled `Entity::operator new` ignores derived sizes.
The benchmark creates and deletes a private temporary terrain fixture under `Mods`.
It changes no runtime gameplay settings.

The complete outputs are `GPUSprites-run1.txt` and `GPUSprites-run2.txt`.
`GPUSprites-results.json` contains sample ranges, medians, cold costs, and pixel observations.
