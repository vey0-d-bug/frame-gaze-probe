// Checks for gaze_probe.h, the gaze probe's sequence and statistics: the
// angle to a dot and its sideways and upward parts, the nearest-rank p95, the
// sequence's phases and its buzz, waiting for focus and pausing without it,
// new samples by sample time (a tracker slower than the display), the lag,
// and a bias that shows up as the same angle on every dot.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "gaze_probe.h"
#include "xr_math.h"

using namespace gaze;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr float kDeg = 3.14159265f / 180.0f;

Quat look(float yaw_deg, float pitch_deg) { return axis_angle({0, 1, 0}, yaw_deg * kDeg) * axis_angle({1, 0, 0}, pitch_deg * kDeg); }

// The dots as the host app places them for a viewer at the origin facing -Z:
// 1.5 m away in their directions.
std::array<Vec3, 5> dots() {
    std::array<Vec3, 5> d{};
    for (size_t i = 0; i < d.size(); ++i) {
        d[i] = rotate(look(GazeProbe::kDots[i].yaw_deg, GazeProbe::kDots[i].pitch_deg), {0, 0, -1}) * 1.5f;
    }
    return d;
}

struct Run {
    std::vector<std::string> lines;
    GazeProbe probe{[this](const std::string& s) { lines.push_back(s); }};
    bool logged(const std::string& part) const {
        for (const auto& l : lines) {
            if (l.find(part) != std::string::npos) return true;
        }
        return false;
    }
};

void test_error_and_stats() {
    const Vec3 target{0, 0, -1.5f};
    GazeProbe::Error e = GazeProbe::error({0, 0, 0}, rotate(look(-1.0f, 0), {0, 0, -1}), target, {0, 1, 0});
    check(near(e.angle / kDeg, 1.0, 1e-3), "1 degree right of the dot is 1 degree off");
    check(near(e.x / kDeg, 1.0, 1e-3) && near(e.y / kDeg, 0.0, 1e-3), "and all of it to the right");
    e = GazeProbe::error({0, 0, 0}, rotate(look(0, 2.0f), {0, 0, -1}), target, {0, 1, 0});
    check(near(e.y / kDeg, 2.0, 1e-3) && near(e.x / kDeg, 0.0, 1e-3), "2 degrees up is all up");
    // From an origin off to the side, the angle is to the ray to the dot.
    e = GazeProbe::error({0.03f, 0, 0}, normalize(Vec3{-0.03f, 0, -1.5f}), target, {0, 1, 0});
    check(near(e.angle / kDeg, 0.0, 1e-3), "the ray from a moved origin to the dot is on it");

    std::vector<float> v;
    for (int i = 1; i <= 100; ++i) v.push_back(static_cast<float>(i));
    GazeProbe::Stats s = GazeProbe::stats(v);
    check(s.n == 100 && near(s.p95, 95, 1e-9) && near(s.p50, 50, 1e-9) && near(s.max, 100, 1e-9) && near(s.mean, 50.5, 1e-9),
          "nearest rank on 1..100: p50 50, p95 95, worst 100, mean 50.5");
    check(GazeProbe::stats({}).n == 0, "no samples, no statistics");
    check(near(GazeProbe::stats({3.0f}).p95, 3.0, 1e-9), "one sample is its own p95");
}

// A whole worn run: focus from the start, the eyes on the lit dot with a
// steady 0.5 degree bias to the right, a 60 Hz tracker under a 120 Hz
// display with 12 ms of lag, the eyes closed (no gaze) between the warning
// and the buzz plus 150 ms.
void test_worn_run() {
    Run r;
    const auto d = dots();
    const double dt = 1.0 / 120.0;
    int buzzes = 0;
    double buzz_at = -1;
    bool saw_dots[5] = {};
    XrTime sample = 0;
    double t = 0;
    for (int frame = 0; frame < 120 * 200 && r.probe.phase() != GazeProbe::Phase::Done; ++frame, t += dt) {
        GazeProbe::Frame f;
        f.now = t;
        f.focused = true;
        f.head = {0, 0, 0};
        f.dots = d;
        const XrTime display = static_cast<XrTime>(t * 1e9);
        if (frame % 2 == 0) sample = display - 12'000'000;  // a new sample every other frame
        const int lit = r.probe.lit();
        const bool closed = r.probe.phase() == GazeProbe::Phase::Eyes &&
                            r.probe.phase_time() >= GazeProbe::kEyesWarn &&
                            (buzz_at < 0 || t < buzz_at + 0.15);
        f.gaze.bound = true;
        f.gaze.active = true;
        f.gaze.display_time = display;
        f.gaze.sample_time = sample;
        if (!closed) {
            f.gaze.flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                           XR_SPACE_LOCATION_POSITION_VALID_BIT;
            const GazeProbe::Dot& dot = GazeProbe::kDots[static_cast<size_t>(lit >= 0 ? lit : 0)];
            f.gaze.pose.orientation = look(dot.yaw_deg - 0.5f, dot.pitch_deg);
        }
        if (lit >= 0) saw_dots[lit] = true;
        GazeProbe::Out out = r.probe.update(f);
        if (out.buzz) {
            ++buzzes;
            buzz_at = t;
        }
    }
    check(r.probe.phase() == GazeProbe::Phase::Done, "the sequence ends");
    const double expect = GazeProbe::kIntro + GazeProbe::kRounds * 5 * GazeProbe::kDotSeconds + GazeProbe::kEyesWarn +
                          GazeProbe::kEyesClosed + GazeProbe::kEyesOpen + GazeProbe::kHandsDown + GazeProbe::kHandsFree;
    std::printf("      ended at %.2f s (expected %.2f)\n", t, expect);
    check(near(t, expect, 0.05), "after intro, rounds, eyes, controllers down and hands free");
    check(buzzes == 1, "one buzz");
    check(saw_dots[0] && saw_dots[1] && saw_dots[2] && saw_dots[3] && saw_dots[4], "every dot lit");

    const GazeProbe::Stats s = r.probe.rounds_stats();
    std::printf("      rounds: %zu samples, mean %.3f, p95 %.3f, worst %.3f\n", s.n, s.mean, s.p95, s.max);
    // 3 rounds x 5 dots x (3 - 0.8) s x 60 samples a second.
    check(s.n >= 3 * 5 * 131 && s.n <= 3 * 5 * 133, "only settled new samples count (60 a second, 2.2 s a dot)");
    check(near(s.mean, 0.5, 0.01) && near(s.p95, 0.5, 0.01), "a 0.5 degree bias reads as 0.5 degrees off");
    check(r.logged("dot left: ") && r.logged("bias +0.50 deg right, +0.00 deg up"), "the bias is logged per dot, to the right");
    check(r.probe.hands_free_stats().n > 0, "the controllers-down dots are measured apart");
    check(r.logged("60 new samples a second"), "the tracker's 60 Hz is found under the 120 Hz display");
    check(r.logged("lag from sample to display 12.0 ms mean"), "the lag is display time minus sample time");
    check(r.logged("eyes closed: gaze usable in 0 of"), "no gaze while the eyes are closed");
    check(r.logged("usable and tracked again 1") || r.logged("usable and tracked again 2"), "the gaze is back ~150 ms after the buzz");
    const GazeProbe::Card c = r.probe.card();
    check(c.done && c.lit < 0 && c.lines.size() >= 3 && c.lines[1].find("p95 0.5 deg") != std::string::npos,
          "the card ends with the numbers");
    for (const auto& l : r.lines) {
        // Never a direction: the probe's lines carry angles to the dot only.
        if (l.find("yaw") != std::string::npos || l.find("direction") != std::string::npos) {
            check(false, ("a line names a direction: " + l).c_str());
        }
    }
}

// Unworn over ssh: no focus ever. The probe starts after kWaitFocus and runs
// through on its own, with nothing measured.
void test_no_focus() {
    Run r;
    double t = 0;
    const double dt = 1.0 / 72.0;
    for (int i = 0; i < 72 * 200 && r.probe.phase() != GazeProbe::Phase::Done; ++i, t += dt) {
        GazeProbe::Frame f;
        f.now = t;
        f.dots = dots();
        r.probe.update(f);
        if (i == 72 * 5) check(r.probe.phase() == GazeProbe::Phase::Waiting, "waits for focus at first");
    }
    check(r.probe.phase() == GazeProbe::Phase::Done, "without focus it runs through after the wait");
    check(r.logged("no input focus after 15 s"), "and says it went on without focus");
    check(r.probe.rounds_stats().n == 0, "nothing measured without gaze");
    check(r.logged("action active 0.0 %"), "the summary says the action was never active");
}

// Focus lost for 2 s in the rounds: the sequence waits, then goes on.
void test_pause() {
    Run r;
    double t = 0;
    const double dt = 1.0 / 120.0;
    double paused_at = -1;
    double phase_t_at_pause = 0, phase_t_after = 0;
    for (int i = 0; i < 120 * 40; ++i, t += dt) {
        GazeProbe::Frame f;
        f.now = t;
        f.dots = dots();
        const bool in_rounds = r.probe.phase() == GazeProbe::Phase::Rounds && r.probe.phase_time() > 5.0;
        if (in_rounds && paused_at < 0) {
            paused_at = t;
            phase_t_at_pause = r.probe.phase_time();
        }
        f.focused = !(paused_at >= 0 && t < paused_at + 2.0);
        r.probe.update(f);
        if (paused_at >= 0 && t >= paused_at + 1.9 && t < paused_at + 1.92) phase_t_after = r.probe.phase_time();
    }
    check(paused_at >= 0, "reached the rounds");
    check(near(phase_t_after, phase_t_at_pause, 0.02), "the sequence's clock stops while focus is lost");
    check(r.logged("input focus lost in rounds") && r.logged("input focus back after 2.0 s"), "both logged");
}

}  // namespace

int main() {
    test_error_and_stats();
    test_worn_run();
    test_no_focus();
    test_pause();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all gaze probe checks passed\n");
    return 0;
}
