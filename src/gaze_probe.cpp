#include "gaze_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <utility>

#include "hud/placement.h"

namespace gaze {
namespace {

constexpr double kRad = 180.0 / 3.14159265358979323846;
constexpr size_t kDotCount = GazeProbe::kDots.size();

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

double pct(int64_t part, int64_t whole) { return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0; }

}  // namespace

GazeProbe::Pixel GazeProbe::dot_pixel(size_t i) {
    const Dot& d = kDots[i % kDots.size()];
    const float r = kMenuDefaultDistance / kPanelMetresPerPx;  // px to the viewer
    const float yaw = d.yaw_deg / static_cast<float>(kRad), pitch = d.pitch_deg / static_cast<float>(kRad);
    // Left positive: smaller x. Up positive: smaller y.
    return {kPanelWidthPx * 0.5f - std::tan(yaw) * r, kPanelHeightPx * 0.5f - std::tan(pitch) * r};
}

const char* GazeProbe::phase_name(Phase p) {
    switch (p) {
        case Phase::Waiting: return "waiting";
        case Phase::Intro: return "intro";
        case Phase::Rounds: return "rounds";
        case Phase::Eyes: return "eyes";
        case Phase::HandsDown: return "hands-down";
        case Phase::HandsFree: return "hands-free";
        case Phase::Done: return "done";
    }
    return "?";
}

GazeProbe::GazeProbe(std::function<void(const std::string&)> log, const std::string& tsv) : log_(std::move(log)) {
    if (!tsv.empty()) {
        tsv_ = std::fopen(tsv.c_str(), "w");
        if (!tsv_) {
            this->log("gaze probe: cannot write " + tsv + "; the log's lines only");
        } else {
            // Angles to the lit dot only: never where the eyes look.
            std::fputs("t\tphase\tround\tdot\tsettled\tfocused\tactive\tflags\tnew\tlag_ms\terr_deg\terr_x_deg\terr_y_deg\n", tsv_);
            this->log("gaze probe: one row a frame in " + tsv + " (angles to the lit dot, no directions)");
        }
    }
}

GazeProbe::~GazeProbe() {
    if (tsv_) std::fclose(tsv_);
}

void GazeProbe::log(const std::string& line) const {
    if (log_) log_(line);
}

GazeProbe::Stats GazeProbe::stats(std::vector<float> v) {
    Stats s;
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (float x : v) sum += x;
    s.mean = sum / static_cast<double>(v.size());
    // Nearest rank: the smallest value with at least p % of them at or under it.
    auto rank = [&](double p) {
        size_t i = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
        return static_cast<double>(v[std::clamp<size_t>(i, 1, v.size()) - 1]);
    };
    s.p50 = rank(50);
    s.p95 = rank(95);
    s.max = v.back();
    return s;
}

GazeProbe::Error GazeProbe::error(Vec3 origin, Vec3 dir, Vec3 target, Vec3 up) {
    const Vec3 t = normalize(target - origin);
    const Vec3 g = normalize(dir);
    // The target's own frame: forward to it, right and up across it.
    const Vec3 r = normalize(cross(t, up), {1, 0, 0});
    const Vec3 u = cross(r, t);
    Error e;
    e.angle = std::acos(std::clamp(dot(g, t), -1.0f, 1.0f));
    e.x = std::atan2(dot(g, r), dot(g, t));
    e.y = std::atan2(dot(g, u), dot(g, t));
    return e;
}

void GazeProbe::enter(Phase p, double now) {
    phase_ = p;
    phase_t_ = 0;
    log(fmt("gaze probe: %s at %.1f s", phase_name(p), started_ >= 0 ? now - started_ : 0.0));
}

GazeProbe::Out GazeProbe::update(const Frame& f) {
    Out out;
    if (started_ < 0) {
        started_ = f.now;
        last_now_ = f.now;
        sec_start_ = f.now;
        log(fmt("gaze probe: %zu dots (centre, 10 deg left and right, 8 deg up and down), %.0f s each from %.1f s in, "
                "%d rounds; then the eyes closed and %.0f s with the controllers down",
                kDotCount, kDotSeconds, kSettle, kRounds, kHandsFree));
    }
    const double dt = std::max(0.0, f.now - last_now_);
    last_now_ = f.now;

    // The sequence's clock: it waits for focus at the start, and stops while
    // focus is lost (a prompt, the Steam menu), at most kMaxPause at a time.
    bool run = true;
    if (phase_ == Phase::Waiting) {
        phase_t_ += dt;
        if (f.focused || phase_t_ >= kWaitFocus) {
            if (!f.focused) log(fmt("gaze probe: no input focus after %.0f s; going on without it", kWaitFocus));
            enter(Phase::Intro, f.now);
        }
        run = false;
    } else if (phase_ != Phase::Done && ever_focused_ && !f.focused) {
        paused_for_ += dt;
        if (paused_for_ < kMaxPause) {
            run = false;
            if (!pause_logged_) log(fmt("gaze probe: input focus lost in %s at %.1f s; the sequence waits (at most %.0f s)",
                                        phase_name(phase_), phase_t_, kMaxPause));
            pause_logged_ = true;
        } else if (pause_logged_) {
            log(fmt("gaze probe: still no input focus after %.0f s; going on without it", kMaxPause));
            pause_logged_ = false;
        }
    } else if (f.focused) {
        if (pause_logged_) log(fmt("gaze probe: input focus back after %.1f s; going on", paused_for_));
        pause_logged_ = false;
        paused_for_ = 0;
    }
    if (f.focused) ever_focused_ = true;
    if (run) advance(dt, out);

    gather(f);
    if (f.now - sec_start_ >= 1.0) {
        log_second();
        sec_start_ = f.now;
    }
    return out;
}

void GazeProbe::advance(double dt, Out& out) {
    phase_t_ += dt;
    if (phase_ != Phase::Waiting) seq_t_ += dt;
    const double dot_cycle = kDotSeconds * static_cast<double>(kDotCount);
    switch (phase_) {
        case Phase::Waiting:
        case Phase::Done: lit_ = -1; return;
        case Phase::Intro:
            lit_ = -1;
            if (phase_t_ >= kIntro) enter(Phase::Rounds, last_now_);
            break;
        case Phase::Rounds:
            if (phase_t_ >= dot_cycle * kRounds) {
                lit_ = -1;
                enter(Phase::Eyes, last_now_);
            }
            break;
        case Phase::Eyes:
            lit_ = -1;
            if (!buzzed_ && phase_t_ >= kEyesWarn + kEyesClosed) {
                buzzed_ = true;
                out.buzz = true;
                log("gaze probe: buzz (open your eyes)");
            }
            if (phase_t_ >= kEyesWarn + kEyesClosed + kEyesOpen) enter(Phase::HandsDown, last_now_);
            break;
        case Phase::HandsDown:
            lit_ = -1;
            if (phase_t_ >= kHandsDown) enter(Phase::HandsFree, last_now_);
            break;
        case Phase::HandsFree:
            if (phase_t_ >= kHandsFree) {
                lit_ = -1;
                enter(Phase::Done, last_now_);
                log_summary("at the end of the sequence");
                return;
            }
            break;
    }
    if (phase_ == Phase::Rounds || phase_ == Phase::HandsFree) {
        const int step = static_cast<int>(phase_t_ / kDotSeconds);
        lit_ = step % static_cast<int>(kDotCount);
        round_ = step / static_cast<int>(kDotCount) + 1;
    }
}

void GazeProbe::gather(const Frame& f) {
    const GazeSample& g = f.gaze;
    const bool usable = g.usable();
    Second& s = sec_;
    ++s.frames;
    ++all_.frames;
    if (f.focused) ++s.focused, ++all_.focused;
    if (g.active) ++s.active, ++all_.active;
    if (usable) ++s.valid, ++all_.valid;
    if (usable && g.tracked()) ++s.tracked, ++all_.tracked;
    if (usable && g.position_valid()) ++s.positioned;

    bool changed = false;
    if (usable) {
        changed = !have_last_orientation_ || std::fabs(dot(g.pose.orientation, last_orientation_)) < 1.0f - 1e-7f;
        have_last_orientation_ = true;
        last_orientation_ = g.pose.orientation;
        if (changed) ++s.changed, ++all_.changed;
    }
    // A new sample: a sample time not seen before, or (without one) a pose
    // that moved.
    bool fresh = false;
    double lag_ms = 0;
    bool have_lag = false;
    if (usable) {
        if (g.sample_time != 0) {
            fresh = g.sample_time != last_sample_time_;
            last_sample_time_ = g.sample_time;
            if (fresh) {
                lag_ms = static_cast<double>(g.display_time - g.sample_time) * 1e-6;
                have_lag = true;
                s.lag_sum += lag_ms;
                s.lag_max = std::max(s.lag_max, lag_ms);
                s.lag_min = std::min(s.lag_min, lag_ms);
                ++s.lag_n;
                all_.lag_ms.push_back(static_cast<float>(lag_ms));
            }
        } else {
            ++s.no_time;
            ++all_.no_time;
            fresh = changed;
        }
        if (fresh) ++s.fresh, ++all_.fresh;
    }

    const bool dots = (phase_ == Phase::Rounds || phase_ == Phase::HandsFree) && lit_ >= 0;
    const double into_dot = dots ? phase_t_ - std::floor(phase_t_ / kDotSeconds) * kDotSeconds : 0.0;
    const bool settled = dots && into_dot >= kSettle;
    Error e;
    bool have_err = false;
    if (dots && usable) {
        const Vec3 origin = g.position_valid() ? g.pose.position : f.head;
        e = error(origin, g.pose.forward(), f.dots[static_cast<size_t>(lit_)], {0, 1, 0});
        have_err = true;
        if (settled && fresh) {
            Group& grp = phase_ == Phase::Rounds ? rounds_ : hands_free_;
            const size_t d = static_cast<size_t>(lit_);
            grp.err[d].push_back(static_cast<float>(e.angle * kRad));
            grp.sum_x[d] += e.x * kRad;
            grp.sum_y[d] += e.y * kRad;
            ++grp.n_xy[d];
            s.err_sum += e.angle * kRad;
            ++s.err_n;
        }
    }

    if (phase_ == Phase::Eyes) {
        const double closed_at = kEyesWarn;
        if (phase_t_ >= closed_at + kClosedFrom && phase_t_ <= closed_at + kClosedTo) {
            ++closed_frames_;
            if (usable) ++closed_usable_;
        }
        const double buzz_at = kEyesWarn + kEyesClosed;
        if (buzzed_ && reacquired_after_ < 0 && usable && g.tracked()) {
            reacquired_after_ = phase_t_ - buzz_at;
            log(fmt("gaze probe: gaze usable and tracked %.0f ms after the buzz", reacquired_after_ * 1000.0));
        }
    }
    if (phase_ == Phase::HandsFree) {
        ++free_frames_;
        if (f.focused) ++free_focused_;
        if (usable) ++free_usable_;
    }
    if (usable != was_usable_ && phase_ != Phase::Waiting) {
        log(fmt("gaze probe: gaze %s in %s at %.2f s", usable ? "usable" : "not usable", phase_name(phase_), phase_t_));
    }
    was_usable_ = usable;

    if (phase_ != Phase::Waiting) write_row(f, fresh, have_lag ? lag_ms : NAN, have_err ? &e : nullptr, settled);
}

void GazeProbe::write_row(const Frame& f, bool fresh, double lag_ms, const Error* e, bool settled) {
    if (!tsv_) return;
    const GazeSample& g = f.gaze;
    std::fprintf(tsv_, "%.4f\t%s\t%d\t%s\t%d\t%d\t%d\t0x%llx\t%d\t", seq_t_, phase_name(phase_),
                 phase_ == Phase::Rounds ? round_ : 0, lit_ >= 0 ? kDots[static_cast<size_t>(lit_)].name : "-",
                 settled ? 1 : 0, f.focused ? 1 : 0, g.active ? 1 : 0, static_cast<unsigned long long>(g.flags), fresh ? 1 : 0);
    if (std::isnan(lag_ms)) std::fputs("\t", tsv_);
    else std::fprintf(tsv_, "%.3f\t", lag_ms);
    if (e) std::fprintf(tsv_, "%.4f\t%.4f\t%.4f\n", e->angle * kRad, e->x * kRad, e->y * kRad);
    else std::fputs("\t\t\n", tsv_);
}

void GazeProbe::log_second() {
    const Second& s = sec_;
    if (s.frames == 0) return;
    // Samples a second while the gaze was usable most of the second: the
    // tracker's rate as the runtime hands it over.
    if (s.valid * 2 >= s.frames) all_.seconds += 1.0;
    std::string where = phase_name(phase_);
    if (lit_ >= 0) {
        where += phase_ == Phase::Rounds ? fmt(" %d/%d", round_, kRounds) : std::string();
        where += std::string(" dot ") + kDots[static_cast<size_t>(lit_)].name;
    }
    std::string line = fmt("gaze: [%s] %d frames: focus %d, active %d, ray valid %d, tracked %d, position %d; "
                           "new samples %d (no sample time in %d), pose changed in %d",
                           where.c_str(), s.frames, s.focused, s.active, s.valid, s.tracked, s.positioned, s.fresh, s.no_time,
                           s.changed);
    if (s.lag_n > 0) line += fmt("; lag %.1f ms avg (%.1f to %.1f)", s.lag_sum / s.lag_n, s.lag_min, s.lag_max);
    if (s.err_n > 0) line += fmt("; off the dot %.2f deg avg over %d samples", s.err_sum / s.err_n, s.err_n);
    log(line);
    sec_ = Second{};
}

std::string GazeProbe::stats_line(const Stats& s) const {
    if (s.n == 0) return "no samples";
    return fmt("%zu samples, mean %.2f deg, p50 %.2f, p95 %.2f, worst %.2f", s.n, s.mean, s.p50, s.p95, s.max);
}

GazeProbe::Stats GazeProbe::rounds_stats() const {
    std::vector<float> all;
    for (const auto& v : rounds_.err) all.insert(all.end(), v.begin(), v.end());
    return stats(std::move(all));
}

GazeProbe::Stats GazeProbe::hands_free_stats() const {
    std::vector<float> all;
    for (const auto& v : hands_free_.err) all.insert(all.end(), v.begin(), v.end());
    return stats(std::move(all));
}

void GazeProbe::log_summary(const char* when) {
    if (summary_done_ && std::string(when).rfind("at exit", 0) == 0) {
        log("gaze probe: the summary was logged at the end of the sequence");
        return;
    }
    if (std::string(when) == "at the end of the sequence") summary_done_ = true;
    const Totals& a = all_;
    log(fmt("gaze probe summary (%s, phase %s): %lld frames; input focus in %.1f %%, action active %.1f %%, ray valid %.1f %%, "
            "tracked %.1f %%; no sample time in %.1f %% of valid frames",
            when, phase_name(phase_), static_cast<long long>(a.frames), pct(a.focused, a.frames), pct(a.active, a.frames),
            pct(a.valid, a.frames), pct(a.tracked, a.frames), pct(a.no_time, a.valid)));
    if (a.seconds > 0) {
        log(fmt("gaze probe summary: %.0f new samples a second over %.0f s with gaze (pose changed %.0f a second)",
                static_cast<double>(a.fresh) / a.seconds, a.seconds, static_cast<double>(a.changed) / a.seconds));
    }
    if (!a.lag_ms.empty()) {
        Stats l = stats(a.lag_ms);
        log(fmt("gaze probe summary: lag from sample to display %.1f ms mean, p50 %.1f, p95 %.1f, worst %.1f (%zu samples)",
                l.mean, l.p50, l.p95, l.max, l.n));
    } else {
        log("gaze probe summary: no lag measured (no sample times)");
    }
    log("gaze probe summary: rounds, all dots: " + stats_line(rounds_stats()));
    for (size_t d = 0; d < kDotCount; ++d) {
        std::string line = std::string("gaze probe summary: rounds, dot ") + kDots[d].name + ": " + stats_line(stats(rounds_.err[d]));
        if (rounds_.n_xy[d] > 0) {
            const double n = static_cast<double>(rounds_.n_xy[d]);
            line += fmt(", bias %+.2f deg right, %+.2f deg up", rounds_.sum_x[d] / n, rounds_.sum_y[d] / n);
        }
        log(line);
    }
    log(fmt("gaze probe summary: eyes closed: gaze usable in %d of %d frames (%.1f to %.1f s after closing); %s", closed_usable_,
            closed_frames_, kClosedFrom, kClosedTo,
            reacquired_after_ >= 0 ? fmt("usable and tracked again %.0f ms after the buzz", reacquired_after_ * 1000.0).c_str()
                                   : "not usable and tracked again after the buzz"));
    log(fmt("gaze probe summary: controllers down: %d frames, focus in %.1f %%, gaze usable in %.1f %%; ", free_frames_,
            pct(free_focused_, free_frames_), pct(free_usable_, free_frames_)) +
        stats_line(hands_free_stats()));
}

GazeProbe::Card GazeProbe::card() const {
    Card c;
    c.lit = lit_;
    if (lit_ >= 0) c.lit_progress = static_cast<float>(std::fmod(phase_t_, kDotSeconds) / kDotSeconds);
    c.title = "Gaze probe";
    const double dot_cycle = kDotSeconds * static_cast<double>(kDotCount);
    auto left = [](double total, double t) { return static_cast<int>(std::ceil(std::max(0.0, total - t))); };
    switch (phase_) {
        case Phase::Waiting:
            c.lines = {"Waiting for the headset to give the app input focus...",
                       fmt("Starting anyway in %d s", left(kWaitFocus, phase_t_))};
            break;
        case Phase::Intro:
            c.lines = {"Look at each dot as it lights up, and keep looking",
                       "at it until the next one lights. Keep your head",
                       "still, facing the middle of this panel.",
                       fmt("Starting in %d s", left(kIntro, phase_t_))};
            break;
        case Phase::Rounds: {
            const int k = static_cast<int>(std::fmod(phase_t_, dot_cycle) / kDotSeconds) + 1;
            c.lines = {"Look at the lit dot", fmt("Round %d of %d, dot %d of %zu", round_, kRounds, k, kDotCount)};
            break;
        }
        case Phase::Eyes:
            if (phase_t_ < kEyesWarn) {
                c.lines = {"Next: close your eyes for a few seconds.", "Open them when the controllers buzz.",
                           fmt("In %d s", left(kEyesWarn, phase_t_))};
            } else if (phase_t_ < kEyesWarn + kEyesClosed) {
                c.lines = {"Close your eyes now.", "Open them when the controllers buzz."};
            } else {
                c.lines = {"Open your eyes."};
            }
            break;
        case Phase::HandsDown:
            c.lines = {"Put both controllers down now,", "on a table or your lap, and leave them there.",
                       fmt("Then look at the dots again. Starting in %d s", left(kHandsDown, phase_t_))};
            break;
        case Phase::HandsFree:
            c.lines = {"Controllers down: look at the lit dot", fmt("%d s to go", left(kHandsFree, phase_t_))};
            break;
        case Phase::Done: {
            c.done = true;
            const Stats r = rounds_stats(), h = hands_free_stats();
            c.lines.push_back("Done, thank you. You can pick the controllers up.");
            c.lines.push_back(r.n ? fmt("Off the dot, rounds: p95 %.1f deg, mean %.1f deg", r.p95, r.mean)
                                  : std::string("Off the dot, rounds: no gaze samples"));
            c.lines.push_back(h.n ? fmt("Off the dot, controllers down: p95 %.1f deg, mean %.1f deg", h.p95, h.mean)
                                  : std::string("Off the dot, controllers down: no gaze samples"));
            std::string rate = all_.seconds > 0 ? fmt("%.0f samples a second", static_cast<double>(all_.fresh) / all_.seconds)
                                                : std::string("no sample rate");
            if (!all_.lag_ms.empty()) rate += fmt(", lag %.0f ms", stats(all_.lag_ms).p50);
            c.lines.push_back(fmt("Gaze usable in %.0f %% of frames; ", pct(all_.valid, all_.frames)) + rate);
            c.lines.push_back("Quit from the Steam menu when you like.");
            break;
        }
    }
    if (ever_focused_ && pause_logged_) c.status = "No input focus: the probe waits";
    else c.status = was_usable_ ? "Eye gaze: coming in" : "Eye gaze: none (inactive or not valid)";
    return c;
}

}  // namespace gaze
