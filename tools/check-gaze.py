#!/usr/bin/env python3
"""The gaze probe's numbers.

    check-gaze.py LOG_OR_FOLDER [--unworn] [--tsv FILE]

Reads a --gaze-probe run's log (or the newest log with a gaze probe summary
under a folder) and the <log name>-gaze.tsv beside it, and prints what a
go/no-go table needs: whether the extension, the flag, the binding and the profile were
there; how often the action was active and the ray valid; the tracker's
sample rate and the lag from the eye sample to the frame's predicted display
time; the accuracy to the lit dot over the three rounds (mean, p95, worst,
per dot with its bias) and with the controllers down; the eyes-closed
behaviour. The angles in the TSV are recomputed so the log's statistics have
a second source. --unworn says what run P0 expects (nothing active) instead
of judging.

Nothing in the log or the TSV is a gaze direction: the rows carry angles to
the dot that was lit, and this script keeps it that way.
"""

import argparse
import glob
import math
import os
import re
import sys

PLAN_ROWS = [
    ("small targets", 1.5, 40.0,
     "Good enough for dwell on small (about 2.6-degree) targets"),
    ("gaze layout", 2.5, 80.0,
     "Good enough for a gaze layout: every dwell target at least 4 degrees"),
]
FALLBACK = "Eye gaze usable only as an optional source; keep head gaze as the default"
NO_GAZE = "Never active while worn: fall back to head gaze"


def find_log(path):
    if os.path.isfile(path):
        return path
    best = None
    for p in glob.glob(os.path.join(path, "**", "*.log"), recursive=True):
        try:
            with open(p, errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        if "gaze probe" in text:
            m = os.path.getmtime(p)
            if best is None or m > best[0]:
                best = (m, p)
    return best[1] if best else None


def last(pattern, text, flags=0):
    found = re.findall(pattern, text, flags)
    return found[-1] if found else None


def stats(values):
    v = sorted(values)
    if not v:
        return None
    def rank(p):
        i = math.ceil(p / 100.0 * len(v))
        return v[min(max(i, 1), len(v)) - 1]
    return {"n": len(v), "mean": sum(v) / len(v), "p50": rank(50), "p95": rank(95), "max": v[-1]}


def fmt_stats(s):
    if not s:
        return "no samples"
    return f"{s['n']} samples, mean {s['mean']:.2f} deg, p50 {s['p50']:.2f}, p95 {s['p95']:.2f}, worst {s['max']:.2f}"


def read_tsv(path):
    rows = []
    with open(path) as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != len(header):
                continue
            rows.append(dict(zip(header, parts)))
    return rows


def tsv_numbers(rows):
    out = {}
    def err_group(phase):
        per_dot = {}
        for r in rows:
            if r["phase"] != phase or r["settled"] != "1" or r["new"] != "1" or not r["err_deg"]:
                continue
            per_dot.setdefault(r["dot"], []).append(float(r["err_deg"]))
        return per_dot
    out["rounds"] = err_group("rounds")
    out["hands-free"] = err_group("hands-free")
    lags = [float(r["lag_ms"]) for r in rows if r["lag_ms"]]
    out["lag"] = stats(lags)
    # The rate: new samples in each whole second of the sequence's clock in
    # which the ray was valid most of the time.
    per_second = {}
    for r in rows:
        sec = int(float(r["t"]))
        d = per_second.setdefault(sec, [0, 0, 0])
        d[0] += 1
        active = r["active"] == "1"
        valid = (int(r["flags"], 16) & 0x1) != 0
        if active and valid:
            d[1] += 1
            if r["new"] == "1":
                d[2] += 1
    good = [d for d in per_second.values() if d[1] * 2 >= d[0]]
    out["rate"] = (sum(d[2] for d in good) / len(good)) if good else None
    out["seconds_with_gaze"] = len(good)
    out["frames"] = len(rows)
    out["active"] = sum(1 for r in rows if r["active"] == "1")
    out["valid"] = sum(1 for r in rows if r["active"] == "1" and (int(r["flags"], 16) & 0x1))
    out["focused"] = sum(1 for r in rows if r["focused"] == "1")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="the run's log, or a folder to search for the newest gaze probe log")
    ap.add_argument("--unworn", action="store_true", help="run P0: say what it expects instead of judging")
    ap.add_argument("--tsv", help="the rows file (default: <log name>-gaze.tsv beside the log)")
    a = ap.parse_args()

    log_path = find_log(a.path)
    if not log_path:
        print(f"no log with a gaze probe under {a.path}")
        return 2
    with open(log_path, errors="replace") as f:
        log = f.read()
    print(f"log: {log_path}")
    first = log.splitlines()[0] if log else ""
    print(f"build: {first.split('] ', 1)[-1] if '] ' in first else first}")
    for key, pat in [("launch", r"\] I (launch: .*)"), ("runtime", r"\] I (OpenXR runtime: .*|runtime: .*)"),
                     ("arch", r"\] I (.*aarch64.*)")]:
        m = last(pat, log)
        if m:
            print(f"{key}: {m.strip()}")

    print("\n== The extension ==")
    ext = last(r"gaze: (XR_EXT_eye_gaze_interaction v\d+ offered, enabled.*|XR_EXT_eye_gaze_interaction not offered.*)", log)
    print("extension:", ext or "no line (was --gaze-probe given?)")
    flag = last(r"gaze: supportsEyeGazeInteraction (true|false)", log)
    print("supportsEyeGazeInteraction:", flag or "not read")
    bound = last(r"gaze: pose action (bound|NOT bound) on (\S+)", log)
    print("binding:", f"{bound[0]} on {bound[1]}" if bound else "no line")
    profiles = re.findall(r"gaze: /user/eyes_ext profile: (\S+)", log)
    print("profile for /user/eyes_ext:", " -> ".join(profiles) if profiles else "never logged")
    for pat, what in [(r"gaze: reading the gaze pose failed: .*", "read error"),
                      (r"gaze: cannot create the gaze action: .*", "action error"),
                      (r"gaze: no action space for the gaze pose: .*", "space error")]:
        m = last(pat, log)
        if m:
            print(f"{what}: {m}")
    actives = re.findall(r"\[([0-9:.]+)\] I gaze: action (active|inactive)( \(no input focus\))?", log)
    print("action state changes:", ", ".join(f"{t} {s}{n}" for t, s, n in actives) if actives else "none logged")
    flags = re.findall(r"gaze: location flags (0x[0-9a-f]+ \([^)]*\))", log)
    print("location flag changes:", "; ".join(flags[-6:]) if flags else "none logged")
    if "the runtime gives sample times" in log:
        print("sample times: the runtime fills XrEyeGazeSampleTimeEXT")
    else:
        print("sample times: never seen (lag cannot be measured from the runtime)")
    focus = re.findall(r"gaze probe: (input focus lost in .*|input focus back after .*|no input focus after .*|still no input focus.*)", log)
    for f_ in focus:
        print("focus:", f_)

    print("\n== The probe's summary (from the log) ==")
    # The summary at the end of the sequence if it got there, else the one at exit.
    blocks = re.findall(r"gaze probe summary \((.*?), phase (\S+)\): (\d+) frames; input focus in ([0-9.]+) %, action active ([0-9.]+) %, "
                        r"ray valid ([0-9.]+) %, tracked ([0-9.]+) %; no sample time in ([0-9.]+) % of valid frames", log)
    if not blocks:
        print("no summary: the run ended before the probe logged one")
        summary = None
    else:
        summary = blocks[0] if any(b[0].startswith("at the end") for b in blocks) else blocks[-1]
        for b in blocks:
            if b[0].startswith("at the end"):
                summary = b
        when, phase, frames, focused, active, valid, tracked, no_time = summary
        print(f"summary {when} (phase {phase}): {frames} frames, focus {focused} %, active {active} %, ray valid {valid} %, "
              f"tracked {tracked} %, no sample time in {no_time} % of valid frames")
    rate = last(r"gaze probe summary: ([0-9.]+) new samples a second over ([0-9.]+) s with gaze \(pose changed ([0-9.]+) a second\)", log)
    print("rate:", f"{rate[0]} new samples a second over {rate[1]} s (pose changed {rate[2]} a second)" if rate else "no seconds with gaze")
    lag = last(r"gaze probe summary: lag from sample to display ([0-9.]+) ms mean, p50 ([0-9.]+), p95 ([0-9.]+), worst ([0-9.]+) \((\d+) samples\)", log)
    print("lag (predicted display time minus eye sample time; includes the frame's own lead of one to two frames):",
          f"mean {lag[0]} ms, p50 {lag[1]}, p95 {lag[2]}, worst {lag[3]} ({lag[4]} samples)" if lag else "not measured")
    rounds = last(r"gaze probe summary: rounds, all dots: (.*)", log)
    print("rounds, all dots:", rounds or "no line")
    for name in ("centre", "left", "right", "up", "down"):
        m = last(rf"gaze probe summary: rounds, dot {name}: (.*)", log)
        if m:
            print(f"  {name}: {m}")
    eyes = last(r"gaze probe summary: eyes closed: (.*)", log)
    print("eyes closed:", eyes or "no line")
    free = last(r"gaze probe summary: controllers down: (.*)", log)
    print("controllers down:", free or "no line")

    tsv_path = a.tsv
    if not tsv_path:
        base = log_path[:-4] if log_path.endswith(".log") else log_path
        tsv_path = base + "-gaze.tsv"
    tsv = None
    if os.path.isfile(tsv_path):
        print(f"\n== Recomputed from {os.path.basename(tsv_path)} ==")
        rows = read_tsv(tsv_path)
        tsv = tsv_numbers(rows)
        fr = tsv["frames"]
        pct = lambda n: (100.0 * n / fr) if fr else 0.0
        print(f"{fr} rows: focus {pct(tsv['focused']):.1f} %, active {pct(tsv['active']):.1f} %, ray valid {pct(tsv['valid']):.1f} %")
        print("rate:", f"{tsv['rate']:.1f} new samples a second over {tsv['seconds_with_gaze']} s with gaze" if tsv["rate"] is not None
              else "no seconds with gaze")
        print("lag:", (f"mean {tsv['lag']['mean']:.1f} ms, p50 {tsv['lag']['p50']:.1f}, p95 {tsv['lag']['p95']:.1f}, "
                       f"worst {tsv['lag']['max']:.1f} ({tsv['lag']['n']} samples)") if tsv["lag"] else "not measured")
        all_rounds = [e for v in tsv["rounds"].values() for e in v]
        print("rounds, all dots:", fmt_stats(stats(all_rounds)))
        for name in ("centre", "left", "right", "up", "down"):
            print(f"  {name}: {fmt_stats(stats(tsv['rounds'].get(name, [])))}")
        all_free = [e for v in tsv["hands-free"].values() for e in v]
        print("controllers down:", fmt_stats(stats(all_free)))
        log_p95 = re.search(r"p95 ([0-9.]+)", rounds or "")
        if log_p95 and all_rounds:
            diff = abs(float(log_p95.group(1)) - stats(all_rounds)["p95"])
            print("log and rows agree on the rounds' p95" if diff <= 0.05 else f"WARN: the log's p95 and the rows' differ by {diff:.2f} deg")
    else:
        print(f"\n(no rows file at {tsv_path})")

    print("\n== Verdict ==")
    if a.unworn:
        act = float(summary[4]) if summary else None
        print("Run P0, unworn with the sensor covered: nothing to judge. Expected: the extension enabled, the binding accepted, "
              "the action inactive, the location flags 0, no read error.")
        if act is not None:
            print("action active:", f"{act} % (expected 0)" if act > 0 else "0 %, as expected")
        return 0
    if not summary:
        print("No summary to judge.")
        return 1
    active_pct = float(summary[4])
    r = stats([e for v in (tsv or {}).get("rounds", {}).values() for e in v]) if tsv else None
    p95 = r["p95"] if r else (float(re.search(r"p95 ([0-9.]+)", rounds).group(1)) if rounds and "p95" in rounds else None)
    lag_p50 = tsv["lag"]["p50"] if tsv and tsv["lag"] else (float(lag[1]) if lag else None)
    if active_pct <= 0.0 or p95 is None:
        print(NO_GAZE)
        return 0
    print(f"p95 to the dot over the rounds: {p95:.2f} deg; lag p50: {lag_p50:.1f} ms" if lag_p50 is not None
          else f"p95 to the dot over the rounds: {p95:.2f} deg; lag not measured")
    for _name, max_p95, max_lag, what in PLAN_ROWS:
        if p95 <= max_p95 and (lag_p50 is None or lag_p50 <= max_lag):
            print(what)
            if lag_p50 is None:
                print("(no lag from the runtime: judged on accuracy alone)")
            return 0
    print(FALLBACK)
    return 0


if __name__ == "__main__":
    sys.exit(main())
