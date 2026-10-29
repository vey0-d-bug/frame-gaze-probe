# The gaze probe, worn (run P1)

About three minutes worn. It answers whether the Frame's runtime hands a
standalone app an eye gaze pose (`XR_EXT_eye_gaze_interaction`), how close
that pose is to where the eyes look, how often it comes and how late. Five
dots light up in turn on the menu panel; the probe gathers the angle between
the gaze ray and each lit dot. Nothing it logs or writes is a gaze direction,
only angles to the dot that was lit. **Keep any PC streaming session closed
while it runs.** The proximity sensor stays uncovered (you wear it).

Run `unworn-suite.txt` first (automated, unworn, one run). Then copy the build
into the folder an existing Devkit title already launches, over ssh, and have
its start script pass `--gaze-probe` (for example from a `launch-args.txt`
beside it). From Git Bash on the PC:

```
tar -C <build folder> -cf - . | ssh steamos@<hostname> "tar -C ~/devkit-game/<title> -xf -"
```

```
ssh steamos@<hostname> "cd ~/devkit-game/<title> && chmod +x <binary> *.sh && echo '--gaze-probe' > launch-args.txt"
```

Then launch the title from the headset's library. The app opens under a
black card with five dots; the card tells you what to do and counts down. If
the headset shows a permission or calibration prompt for eye tracking,
answer it (allow) and note it: the probe's clock stops while the app has no
input focus and goes on when it comes back.

| # | Do this | You should see | If it fails, note |
|---|---|---|---|
| 1 | Put the headset on and keep your head still, facing the middle of the panel | The black card with five grey dots, an intro countdown, and at the bottom "Eye gaze: coming in" (or "none") | Which status line you saw, and whether any prompt appeared |
| 2 | Look at each dot as it lights (white, with a ring running down) and keep looking until the next lights; three rounds of five | The lit dot moves centre, left, right, up, down; the status stays "coming in" | Whether the dots were hard to see or the panel was not in front of you |
| 3 | When the card says so, close your eyes until the controllers buzz, then open them | A buzz in both hands after about four seconds | Whether you felt the buzz |
| 4 | Put both controllers down on a table or your lap when the card says so, and look at the dots again for a minute | The dots keep lighting; the status stays "coming in" with no controllers held | Whether the card said "No input focus" or the dots stopped |
| 5 | Read the numbers the card ends with (p95, mean, samples a second, lag) | "Done, thank you" and four lines of numbers | The numbers, if you can |
| 6 | Pick a controller up and quit from the Steam menu | The app closes cleanly within a couple of seconds | Whether it hung |

Afterwards copy the log and the `-gaze.tsv` rows file back, judge them with
`tools/check-gaze.py`, then remove `launch-args.txt`:

```
ssh steamos@<hostname> "rm ~/devkit-game/<title>/launch-args.txt"
```
