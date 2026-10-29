# Frame gaze probe

A probe that measures how well the Steam Frame's eye tracking can drive an app, through OpenXR's `XR_EXT_eye_gaze_interaction`. It was built as a test-only option (`--gaze-probe`) in an OpenXR app and ran on the headset on 2026-10-29. This repository is the probe pulled out of that app: it is source to read and lift, not a standalone build.

## What the probe does

It enables `XR_EXT_eye_gaze_interaction` when offered, binds the gaze pose on `/interaction_profiles/ext/eye_gaze_interaction`, and locates the gaze space at every frame's predicted display time with `XrEyeGazeSampleTimeEXT` chained. On a black card at 1.5 m it lights five dots (centre, 10 degrees left and right, 8 up and down) for 3 s each over three rounds, then asks for the eyes closed for a few seconds, then runs 60 s of dots with both controllers put down. From 0.8 s after a dot lights, it records the angle between the gaze ray and the ray to the dot. It logs only angles to the lit dot, never a gaze direction or hit point.

## What it measured (worn run P1, 2026-10-29)

**The eyes flow.** The extension enabled, `supportsEyeGazeInteraction` true, no permission or calibration prompt. `/user/eyes_ext` took the eye gaze profile as soon as focus arrived (1.5 s after start), and the action was active in 98.9 % of the 16025 frames with focus. About 90 new samples a second under the 120 Hz display. Lag from sample to the frame's predicted display time: 45.5 ms p50, 49 ms p95, 66 ms worst, so the tracker runs about 30 to 37 ms behind the eye. With both controllers put down for 60 s, focus and gaze stayed at 100 %.

**Accuracy**, three rounds of five dots at 1.5 m, 2967 settled samples, in degrees:

| Reading | Mean | p50 | p95 |
|---|---|---|---|
| Raw, as the runtime gives it | 4.03 | 3.70 | 5.63 |
| After one fixed offset (2.93 left, 2.20 up) | 1.65 | 1.40 | 2.75 |
| After a five-dot fit (offset and gain per axis), fitted on round 1, tested on rounds 2 and 3 | 1.48 | 1.08 | 2.67 |
| The floor: each dot's own mean removed | 1.19 | 0.86 | 2.12 |

Almost all of the raw error is one offset, the same on every dot and round. The offset wanders about a degree between rounds. After the fit, 87 % of samples sit within 2 degrees of the aim and 73 % within 1.3 degrees. Sample-to-sample movement is 0.33 degrees p50 and 0.76 p95. In the first half second on a new dot the error is about twice what it settles to.

**Eyes closed** for 4 s: the pose stayed valid and tracked throughout (closed eyes are not reported as a loss; the ray holds its last spot) and was tracked again 8 ms after the buzz. With focus on the Steam menu, the action stayed active but no new sample times came. **Unworn** (run P0 over ssh), the session never reached FOCUSED, so the action was never active: an unworn test cannot read the eyes.

**What it means.** Raw gaze misses even 4.7-degree buttons. A quick five-dot calibration plus targets of at least 4 degrees is workable; 2.6-degree targets are not reliable by eye. Caveats: one wearer, one run, no runtime calibration, and dots placed on a menu quad.

## Files

- `src/gaze_probe.{h,cpp}`, `src/gaze_sample.h`: the probe sequence, sample gathering and summary. `src/xr_math.h` is the small vector maths it uses.
- `src/hud/gaze_card.cpp`: the dot card drawn on the menu panel.
- `tests/gaze_probe_test.cpp`: the angle maths, nearest-rank p95, and a whole simulated worn run.
- `tools/check-gaze.py`: reads the log and the per-frame rows file and prints the go/no-go row. `tools/unworn-suite.txt`, `log-checks.txt`, `worn-run.md`: the unworn test run and the worn run's steps.
- `docs/results.md`: the full write-up of both runs.
