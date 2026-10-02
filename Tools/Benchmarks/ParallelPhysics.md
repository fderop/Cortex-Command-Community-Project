# Parallel physics feasibility

This experiment measures a parallel ceiling for independent bodies in free air.
It also demonstrates a native collision dependency with two serial schedules.
The change adds a fixture and a runner. It changes no production physics code.

## Workload and method

The fixture links the release engine objects from commit `41e6b7010817936e09d2929330b7519852a1fcca`.
The recorded environment uses an Apple M3 Pro and GCC 15.2 with `-O3`.
The Matrix and MOSprite objects come from the pristine baseline build.

Each batch calls the real `AtomGroup::Travel` for 2,000 `MOSRotating` bodies.
The bodies have either eight or 32 atoms, varied velocities, and varied angular velocities.
The travel time is 1/60 second. Each batch resets body position and velocity before travel.
This reset keeps the workload inside the scene and prevents accumulated boundary effects.
The timing includes this reset.

The serial case uses the caller thread. The parallel cases use two or four reused worker threads.
Each worker owns a fixed range of bodies.
The timing includes each batch dispatch and the wait for all workers.
It excludes worker construction, fixture construction, hashes, and output.
An empty dispatch measurement isolates the worker overhead.

Each sample has 240 batches. The timing excludes the first 60 batches.
Three runs each produce seven samples for every configuration.
The runner reverses the configuration order on alternate runs.
The result file preserves every sample.

## Results

These medians include 21 samples per configuration.
The [result file](Results/ParallelPhysics/summary.json) preserves all raw timings.
The same directory contains all 18 logs, including every body hash and collision trace.

| Atoms per body | Serial (ms/batch) | Two workers (ms/batch) | Speedup | Four workers (ms/batch) | Speedup |
|---|---:|---:|---:|---:|---:|
| 32 | 8.1721 | 4.0460 | 2.020x | 2.2118 | 3.695x |
| 8 | 2.2251 | 0.9766 | 2.278x | 0.4929 | 4.514x |

Empty dispatch costs 5.6–5.7 microseconds for two workers and 9.2–10.2 microseconds for four workers.
The eight-atom serial samples range from 1.596 to 2.796 ms per batch.
The 32-atom serial samples range from 7.665 to 8.708 ms per batch.
The 32-atom four-worker samples range from 2.143 to 2.394 ms per batch.
The eight-atom ratio exceeds four. The serial variability and platform cache effects can affect this ratio.
It does not establish better-than-linear scaling.

The runner compares a separate 240-batch hash for every body across all configurations and repeats.
The hash includes position, velocity, rotation, angular velocity, travel impulse, wrap state, hit state, atom positions, and queued impulses.
All 2,000 body hashes match for both atom counts.

The free-air fixture disables MO collisions and has an immutable air terrain bitmap.
It has no script, wound, attachment, or terrain mutation work.
The path changes body-owned state and atom-owned state. Its scratch containers are thread-local.
The fixture does not run a known shared-write path on multiple workers.

These times measure an independent travel ceiling.
They do not establish a safe parallel implementation for dense collisions or a whole-game speedup.
The fixture does not measure the cost of a production independence classifier.
The platform scheduler and background applications can change the measured speedup.

## Native collision counterexample

Two bodies start at x=120.25 and x=136.25, with x velocities of 30 and -10.
Each body has 16 atoms and an eight-pixel sprite.
The fixture calls `PreTravel`, native `AtomGroup::Travel`, base `PostTravel`, and `NewFrame` for each body.
It reverses only the order of these calls.
Both schedules use one thread.

The bodies inherit `MOSRotating::CollideAtPoint` without an override.
The native response queues impulses on the other body.
The native intersection response also changes the other body position.
The final native states differ as follows:

| Serial schedule | Left x | Left vx | Left queued impulses | Right x | Right vx | Right queued impulses |
|---|---:|---:|---:|---:|---:|---:|
| Left, then right | 127.289673 | 0.943800 | 6 | 136.539703 | -1.007275 | 1 |
| Right, then left | 124.438759 | 0.943800 | 0 | 132.916672 | -10.000000 | 1 |

The forward trace shows a right-body impulse of `(61.0180168, 0)` after the left body travels.
The right body then collides and queues six impulses on the left body.
The reverse trace has no queued impulse after the right body travels first.
The left body then queues one impulse on the right body.
All 18 executions produce these same schedule-dependent results.

The different positions, velocities, and impulse queues demonstrate a real schedule dependency.
A mutex can prevent simultaneous writes, but it cannot preserve the existing result unless it also preserves the required order.

## Production requirements

`MovableMan::Travel` processes actors, items, and particles in serial order.
Each object receives `ApplyForces`, `PreTravel`, `Travel`, `PostTravel`, and `NewFrame` in that order.
The engine already uses workers for actor rays, `ThreadedUpdate`, and asynchronous MOID/grid work.

The relevant dependencies include these paths:

- `AtomGroup::Travel` writes the other body hit state.
- `MOSRotating::CollideAtPoint` writes the other body impulse queue.
- `AtomGroup::ResolveMOSIntersection` changes positions of interacting bodies.
- Terrain collision paths can change pixels and create objects.
- Callbacks, Lua functions, attachments, and random-number consumers add state dependencies.

A production free-air path needs a conservative independence classifier and immutable scene data for the duration of each batch.
The classifier must account for swept atom positions, terrain, other bodies, attachments, callbacks, and existing asynchronous work.
The measured gain must include the classifier and synchronization costs.

Dense collision work needs explicit ownership of shared state and deterministic handling of contacts, impulses, terrain changes, and callbacks.
A snapshot and an ordered commit phase can define those operations.
Preservation of current behavior also requires preservation of dependent travel order.
That requirement can limit parallel work within a connected collision group.

This experiment supplies evidence for further architecture work. It does not justify direct parallel dispatch of the existing travel loop.

## Reproduction

Build a release engine with a compilation database.
Run the following command from the repository root.
Use pristine release objects for the two replacements.

```sh
DYLD_LIBRARY_PATH=/absolute/path/to/engine/external/lib/macos \
python3 Tools/Benchmarks/ParallelPhysics.py \
  /absolute/path/to/build /absolute/path/to/output \
  --replace-object Source_System_Matrix.cpp.o /absolute/path/to/baseline-Matrix.cpp.o \
  --replace-object Source_Entities_MOSprite.cpp.o /absolute/path/to/baseline-MOSprite.cpp.o \
  --runs 3 --bodies 2000
```

The runner compiles the fixture without the generated precompiled header.
It links the existing engine objects and replaces only the entry point and the specified objects.
It writes raw logs and `summary.json` to the output directory.
It requires equal body hashes, repeatable native collision traces, native impulses, and different results for the two collision schedules.
