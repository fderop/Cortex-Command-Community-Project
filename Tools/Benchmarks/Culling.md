# Camera visibility experiment

The experiment rejects camera visibility culling at the current color-layer draw stage. No production code changes are part of this experiment.

If an object radius misses every current viewport, the candidate skips the entire object. It includes horizontal scene wrapping and all supplied split-screen views.
The candidate uses `GetRadius()`, a scale multiplier, and two pixels of margin. It calls the actual `MOSRotating::Draw` for visible objects.

## Correctness

The fixture draws real engine sprites into a 2048×512 scene bitmap. It then reads a 256×256 viewport from that bitmap.
The viewport read uses integer coordinates and horizontal wrapping. This test does not run the GPU scene compositor or an interactive camera.

| Case | Pixels that differ | Result |
| --- | ---: | --- |
| 54 stationary cases: flips, rotations, scales, and boundaries | 0 | Match |
| Stationary viewport across a scene seam | 0 | Match |
| Scene seam without wrapping in the visibility check | 412 | All sprite pixels missing |
| Camera pan by 64 pixels after the scene draw | 412 | All sprite pixels missing |
| Camera jump after the scene draw | 412 | All sprite pixels missing |
| Union of two stationary split-screen views | 0 | Match |
| Second screen omitted from the visibility check | 412 | All sprite pixels missing |
| Child attachment outside the body bound, before or after its parent | 412 | All child pixels missing |

The camera failures occur even with wrapping and all current views in the visibility check. A larger fixed margin cannot cover arbitrary camera jumps.

The attachment fixture uses the actual `AddAttachable` and parent draw sequence. Its cached parent radius remains 16.298 pixels despite a 140-pixel attachment offset.
The fixture records this result separately, including the child position. It does not establish the correctness of cached attachment bounds during normal simulation.
The explicit body-bound cases establish that an early return can skip visible child pixels. Wounds, glows, and post effects are outside this test.

The test expects each documented counterexample to differ. Zero fixture failures means that the counterexamples occurred as expected, not that culling is safe.

## Draw timings

The measurements use GCC 15.2, release `-O3`, and an Apple M3 Pro. Both paths use the same engine objects and sprite fixture.
The run order alternates across three pairs. Each sample draws 512 sprites 64 times after one warmup pass.
The table gives median wall time for 32,768 candidate object draws. These measurements do not establish an FPS improvement.

| Sprite size | Placement | Draws selected per pass | Baseline (ms) | Culled (ms) | Time reduction |
| --- | --- | ---: | ---: | ---: | ---: |
| 12×12 | All visible | 512 | 9.241 | 9.867 | -6.8% |
| 12×12 | Across the scene | 64 | 9.032 | 1.185 | 86.9% |
| 64×64 | All visible | 512 | 183.394 | 183.647 | -0.1% |
| 64×64 | Across the scene | 64 | 182.872 | 15.137 | 91.7% |
| 128×128 | All visible | 512 | 578.046 | 578.342 | -0.1% |
| 128×128 | Across the scene | 64 | 570.762 | 52.180 | 90.9% |

All timing cases produce equal stationary viewport hashes. The spread cases produce different scene bitmap hashes because culling deletes off-camera color pixels.
The smaller timing differences can contain measurement noise. The large savings describe the isolated draw cost for the artificial spread workload.

## Pipeline requirement

`Main.cpp` calls `FrameMan::Update`, then the activity update, then `MovableMan::Update`.
`MovableMan::Update` clears and draws the scene-wide color layer on a drawn simulation update.
`FrameMan::Draw` subsequently calls `CameraMan::Update` again, before `SceneMan::Draw` reads the color layer.
Thus, the viewport used for a visibility check can differ from the viewport that displays the color layer.

A production change must first establish the final camera views before color drawing. It must also preserve child draw work and the full color bitmap contract.
`SceneMan::GetMOColorBitmap` exposes this bitmap to engine code. `FrameMan::DrawWorldDump` also reads the bitmap for a world dump.
Moving or rebuilding the color layer at frame time changes the draw pipeline. That change exceeds the scope of this experiment.

## Reproduce

Use a release build with `compile_commands.json` and the matching runtime data. Run the following command from the repository root:

```sh
python3 Tools/Benchmarks/Culling.py /path/to/build /path/to/output /path/to/runtime
```

If the build contains other experiments, replace their objects with pristine objects from the same baseline:

```sh
python3 Tools/Benchmarks/Culling.py /path/to/build /path/to/output /path/to/runtime \
  --replace-object Source_System_Matrix.cpp.o /path/to/baseline-Matrix.cpp.o \
  --replace-object Source_Entities_MOSprite.cpp.o /path/to/baseline-MOSprite.cpp.o
```

The runner compiles the fixture and the repository copy of `MOSRotating.cpp`. It links them with the build objects.
The runner disables the precompiled header and uses the worktree headers. The runner expects the macOS library directory beside the build directory.
The fixture subclasses add no instance fields because the engine pool allocator uses the base class size.

The output directory contains raw logs, `summary.json`, and baseline/candidate pixel images under `pixels`.
`Culling.results.json` contains the recorded raw samples and pixel results. The runtime stress activity remains at its original configuration.
