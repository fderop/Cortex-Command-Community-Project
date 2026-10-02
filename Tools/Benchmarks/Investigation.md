# Debris and explosion performance investigation

Physics dominates the tested explosion workloads.
Visible object count alone does not explain their cost.
The engine moves each body's collision atoms through pixels, then checks terrain and candidate object sprites.
Dense contacts repeat this work and trigger collision bookkeeping.

The stock debris scenario averages 4,349 live particles after warmup.
Travel takes 5.83 ms and color drawing takes 1.25 ms of its 8.47 ms mean simulation time.
The forced-collision scenario averages only 631 particles, because debris destroys other debris.
Despite that lower count, travel takes 8.22 ms of its 9.13 ms mean simulation time.
Its maximum simulation tick takes 130 ms.

These measurements explain why some explosions stall despite modest visible object counts.
They also explain why faster sprite drawing alone cannot remove the worst collision hitches.
The stock scenario retains the preset's collision flags.
The harder scenario explicitly enables debris-to-debris hits.

## Accepted changes

Four small changes remove repeated calculations, allocations, or image work.
The combined test reduces mean simulation time by 7.00% for stock debris and 6.16% for collidable debris.
All three pairs improve in both scenarios.
The collidable p95 decreases from 56.49 to 49.45 ms, but large hitches remain.

[CombinedGame.md](CombinedGame.md) contains the raw data, test method, reproduction commands, and four draft PR links.
These changes retain the collision cell layout and the tested collision order, callback behavior, and sprite pixels.
The tests include actual engine routines and seeded gameplay, rather than arithmetic substitutes alone.
All executors used GPT-6.1 Sol and worked sequentially.

## Other experiments

| Candidate | Finding | Reviewable experiment |
| --- | --- | --- |
| Reusable contact storage | Corrected PMR results range from 0.64% faster to 0.12% slower in dense cases. No reliable gain. | [Contact pool report](ContactPool.md) |
| Reset movement segment length | Changes physics in 7 of 15 cases and makes dense cases 58.5–62.6% slower. Rejected. | [Segment report](https://github.com/fderop/Cortex-Command-Community-Project/blob/perf/reset-segment-steps/Tools/Benchmarks/MovementSegments.md) |
| Cull offscreen objects | Saves 87–92% of drawing time in the spread-out fixture, but a camera pan removes all newly visible sprite pixels. Rejected. | [Culling report](https://github.com/fderop/Cortex-Command-Community-Project/blob/perf/cull-hidden-sprites/Tools/Benchmarks/Culling.md) |
| GPU sprite rotation | Completed GPU draws benefit larger sprites. Tiny debris, cold uploads, raster differences, and mixed CPU/GPU ordering need further work. | [GPU report](https://github.com/fderop/Cortex-Command-Community-Project/blob/perf/gpu-sprite-experiment/Tools/Benchmarks/GPUSprites.md) |
| Parallel physics | Independent movement scales across workers. Native collisions produce different results when their serial order changes. | [Parallel report](https://github.com/fderop/Cortex-Command-Community-Project/blob/perf/parallel-physics-experiment/Tools/Benchmarks/ParallelPhysics.md) |
| Rust rewrite | The complete query adapter is about 4% slower than equivalent C++. Packed batches retain only a 1–2% advantage after preparation. | [Rust report](https://github.com/fderop/Cortex-Command-Community-Project/blob/perf/rust-kernel-experiment/Tools/Benchmarks/RustKernel.md) |

The Rust experiment matches 29,491,200 collision queries exactly.
It compares GCC C++, Clang C++, and Rust with equivalent floating-point operations.
The measured result does not justify a production Rust dependency or a physics rewrite.
Larger gains from direct pixel access and batching also apply to C++.

Rejected candidates remain test-only branches.
Their timings do not justify shipping the associated behavior changes.

## Relevant code paths

`MovableMan::Travel` processes actors, items, and particles in sequence.
Each body applies forces, prepares travel, moves, and completes travel.
`AtomGroup::Travel` repeatedly sets up segments and steps the body's atoms through the scene.
`SceneMan::GetMOIDPixel` searches spatial-grid candidates and performs sprite pixel checks.
Cached matrix coefficients reduce repeated transform work in these checks.

Collision setters previously constructed callback arguments even when the corresponding callback was absent.
The accepted callback change checks the existing function map before constructing these arguments.
The original dispatcher still controls initialization and enabled scripts.

`MOSRotating::Draw` uses Allegro CPU rasterization for the normal color layer.
The accepted sprite cache avoids rebuilding identical horizontal flips of shared images.
Modified sprites retain the existing scratch path.
The transparent path already uses GPU drawing.

The engine draws the normal color layer before the renderer's final camera update.
Culling with the earlier camera position can therefore remove newly visible objects.
A safe viewport or GPU redesign must also preserve attachments, particle ordering, sprite lifetimes, and scene wrapping.

Collision responses can write another body's collision state and impulse queue.
Terrain changes, Lua callbacks, and random state create further shared dependencies.
The engine already threads some work, including actor queries, scripted updates, and collision-index preparation.
Parallelizing the remaining movement loop requires more than replacing its iteration with worker tasks.

## Limits

Tests use one Apple M3 Pro and a GCC release build.
The test scenes use stock assets, with explicit collision flags in the harder scenario.
Results do not establish performance for every mod or map.
Simulation timings are not complete frame timings or FPS measurements.
Exact routine checks and matching particle counts provide different levels of evidence.
Counts do not prove complete world-state equality.

The original checkout and its local build-support changes remain untouched.
The fork contains independent draft PR branches, rejected experiment branches, and the combined test branch.
