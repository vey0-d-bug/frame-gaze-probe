# Results: the eye gaze probe

Both headset runs of the probe, on a retail Steam Frame on 2026-10-29: P0
unworn over ssh, P1 worn and launched from the headset's library.

## What the probe is

A test-only `--gaze-probe` option in an OpenXR app. It enables
`XR_EXT_eye_gaze_interaction` when offered, logs `supportsEyeGazeInteraction`,
binds the gaze pose action on `/interaction_profiles/ext/eye_gaze_interaction`
in the existing action set, locates its space at every frame's predicted
display time with `XrEyeGazeSampleTimeEXT` chained (with or without input
focus), and runs a sequence on a black card over the menu panel at 1.5 m: a
10 s intro, five dots (centre, 10 degrees left and right, 8 degrees up and
down) lit 3 s each for three rounds, the eyes closed for a few seconds with a
buzz to open them, then 60 s of dots with both controllers put down. While a
dot is lit, from 0.8 s in, the angle between the gaze ray and the ray to the
dot is gathered per new sample (by sample time). The log gets a `gaze:` line
a second (frames with focus, the action active, the ray valid and tracked, a
position, new samples, the lag) and a `gaze probe summary` at the end of the
sequence and at exit (accuracy per dot and overall: mean, p50, p95, worst,
bias; rate; lag; the eyes-closed window; the controllers-down minute). A rows
file `<log name>-gaze.tsv` has one row a frame. Neither holds a gaze
direction or a hit point, only angles to the lit dot. `tools/check-gaze.py`
reads both and prints a go/no-go row.

Checks before the headset: `gaze_probe_test` (the angle and its parts, the
nearest-rank p95, a whole simulated worn run with a 60 Hz tracker under a 120
Hz display and a 0.5 degree bias, no focus, a 2 s focus loss), run under qemu
on the PC build.

## P0, unworn

One 75 s ssh run (`tools/unworn-suite.txt`), no file, sensor covered. Exit 0,
no error lines, no failures or warnings; aarch64, the headset's runtime
manifest, `SteamVR/OpenXR : cv`, 120 Hz with 0 % missed, 1.17 ms GPU.

What it showed:

- `XR_EXT_eye_gaze_interaction v2 offered, enabled`.
- `supportsEyeGazeInteraction true`.
- `pose action bound on /interaction_profiles/ext/eye_gaze_interaction` (the
  runtime accepted the one suggested binding).
- `/user/eyes_ext profile: none` at attach; no `INTERACTION_PROFILE_CHANGED`
  arrived in the run.
- The pose was read every frame (9003 frames) with no error; the action was
  never active and the location flags stayed 0; no sample time was filled.
- The session went IDLE, READY, SYNCHRONIZED, VISIBLE and stayed VISIBLE:
  **no input focus for the whole run**, so every action was inactive, not
  just the gaze. P0 cannot separate "nobody in front of the eye cameras" from
  "no focus"; that is P1's question.
- The probe waited its 15 s for focus, went on, ran the intro and the three
  rounds, and was in the eyes-closed phase when the 75 s ended; the summary
  at exit has every counter at 0, as expected.
- The rows file has 7202 rows (one a frame at 120 Hz from the intro on), all
  with the action inactive and no angles.
- The card capture at 20 s is the whole 1600 x 900 panel black with the five
  grey rings at the expected pixels (centre 800,450; left 535; right 1064; up
  y 239; down y 661), the title and the four intro lines legible top left
  clear of the top dot, and the status line "Eye gaze: none (inactive or not
  valid)" bottom left.

`check-gaze.py --unworn` on the folder: "nothing to judge ... action active:
0 %, as expected".

## P1, worn

The build was copied into a Devkit title over ssh with `--gaze-probe` in its
launch arguments (removed afterwards), launched from the headset's library
and worn for the whole sequence (steps in `tools/worn-run.md`). The log shows
a Steam launch, aarch64, the headset's runtime, exit 0, no error lines. No PC
streaming session was open, no eye-tracking prompt appeared, and the right
Menu press at 7 s (in the intro, before any dot) was the wearer recentring
the view. The `-gaze.tsv` rows file was copied back with scp.

The runtime hands the app a live eye gaze pose:

- Focus 1.5 s after start, no permission or calibration prompt in the log.
  `/user/eyes_ext` took `/interaction_profiles/ext/eye_gaze_interaction` in the
  same profile-changed burst as the controllers' profile.
- Action active in 98.9 % of the 16025 frames (every frame with focus), flags
  0xf: orientation and position valid and tracked. Sample times filled.
- **Rate: 90 new samples a second** (the located pose changed 119 times a
  second: the runtime interpolates between samples).
- **Lag, sample to predicted display: 45.4 ms mean, 45.5 p50, 49.1 p95,
  66.3 worst** over 11852 samples. The predicted display time runs one to
  two frames (8 to 17 ms at 120 Hz) ahead of the frame's own time, so the
  tracker's lag to "now" is about 30 to 37 ms.
- **Accuracy, three rounds of five dots (2967 settled samples): mean 4.03
  degrees, p50 3.70, p95 5.63, worst 40.9** (one glance away during the down
  dot of round 2). Per dot: centre p95 4.19, left 5.84, right 5.78, up 4.32,
  down 6.10. The bias is the same way on every dot: 1.8 to 3.5 degrees to the
  left and 0.7 to 3.1 up (overall 2.93 left, 2.20 up). With that one offset
  subtracted, the residual is **mean 1.65, p50 1.40, p95 2.75 degrees**
  (per dot p95 2.4 to 2.9, right 4.3); dropping the 15 samples over 15
  degrees changes the raw p95 only to 5.60 and the residual p95 to 2.70.
- **Controllers down for 60 s: focus 100 %, gaze 100 %**, 3956 samples,
  mean 4.08, p95 5.62 raw, p95 2.86 after the same offset.
- **Eyes closed** about 4 s: the pose stayed valid and tracked in all 300
  frames of the window (closed eyes are not reported as a loss), and was
  tracked 8 ms after the buzz. The buzz went out at 66.2 s.
- When focus went to the Steam menu at the end, the action stayed active and
  the pose kept changing, but no new sample times arrived.
- The log and the rows file agree (`check-gaze.py`: "log and rows agree on
  the rounds' p95"; an independent recount gave p95 5.6349, lag p50 45.566).

### Against the go/no-go rows

| Reading | p95 | Lag p50 | Row |
|---|---|---|---|
| Raw, as the tracker gives it | 5.63 deg | 45.5 ms to display | Eye gaze only as an optional source, head gaze the default |
| After a one-time offset correction | 2.75 deg | 45.5 ms to display | Just over the 2.5 line for a layout of 4-degree targets |

Caveats: one wearer, one run, no calibration offered by the runtime; the
offset may be the tracker's, the wearer's eyes, or in part the gaze origin the
runtime reports against where the panel really is (the dots were placed by
`panel_point` on the menu's quad; a second wearer or a run with the panel at
another distance would separate those).
