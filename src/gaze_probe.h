#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "gaze_sample.h"
#include "xr_math.h"

namespace gaze {

// The gaze probe (a test-only option in the host app,
// --gaze-probe): five dots on the menu panel, lit one at a time for 3 s, three
// rounds; then 3 s or so with the eyes closed (the controllers buzz to open
// them) and 60 s more of dots with both controllers put down. While a dot is
// lit, the angle between the gaze ray and the ray from the gaze's origin to
// the dot is gathered from 0.8 s after it lit (the eyes' jump and settle)
// to its end. Every second a log line counts the frames with the action
// active and the ray valid and tracked, the new samples (by their sample
// time) and the lag from sample to display; at the end and at exit, the
// accuracy per dot and overall (mean, p95, worst), the bias, and the rest.
// Pure: the host app feeds it a frame at a time and draws its card.
class GazeProbe {
public:
    struct Dot {
        const char* name;
        float yaw_deg;    // left positive, from the panel's centre as seen at the start
        float pitch_deg;  // up positive
    };
    static constexpr std::array<Dot, 5> kDots = {{
        {"centre", 0.0f, 0.0f},
        {"left", 10.0f, 0.0f},
        {"right", -10.0f, 0.0f},
        {"up", 0.0f, 8.0f},
        {"down", 0.0f, -8.0f},
    }};

    // Where a dot is drawn on the menu panel, in texture px (1600 x 900 at
    // 1 mm per px, kMenuDefaultDistance away: 10 degrees is 264 px, 8
    // degrees 211 px from the centre). The angles are measured in the
    // scene from where the panel really is (panel_point), so the pixels
    // only need to put the dots roughly in the right place.
    struct Pixel {
        float x = 0, y = 0;
    };
    static Pixel dot_pixel(size_t i);

    enum class Phase { Waiting, Intro, Rounds, Eyes, HandsDown, HandsFree, Done };
    static const char* phase_name(Phase p);

    // Seconds of the sequence. Waiting ends with input focus, or after
    // kWaitFocus without it (an unworn headset over ssh may never get it).
    static constexpr double kWaitFocus = 15.0;
    static constexpr double kIntro = 10.0;
    static constexpr double kDotSeconds = 3.0;
    static constexpr double kSettle = 0.8;
    static constexpr int kRounds = 3;
    static constexpr double kEyesWarn = 3.0;      // "next: close your eyes"
    static constexpr double kEyesClosed = 4.0;    // "close them now"; the buzz at its end
    static constexpr double kEyesOpen = 2.0;      // after the buzz
    static constexpr double kHandsDown = 8.0;     // "put both controllers down"
    static constexpr double kHandsFree = 60.0;    // dots, no controllers
    // The window inside "close them now" that counts as closed: after a
    // second to close them, before the buzz.
    static constexpr double kClosedFrom = 1.0, kClosedTo = 3.5;
    // A lost focus pauses the sequence, at most this long at a time.
    static constexpr double kMaxPause = 20.0;

    // One frame. `dots` are where the five dots are in the same space as the
    // gaze (panel_point of their pixels); `head` stands in for the gaze's
    // origin when the runtime gives no position.
    struct Frame {
        double now = 0;  // seconds, any steady clock
        bool focused = false;
        GazeSample gaze;
        std::array<Vec3, 5> dots{};
        Vec3 head;
    };
    struct Out {
        bool buzz = false;  // the controllers should buzz now (open your eyes)
    };

    // What the card on the panel shows.
    struct Card {
        int lit = -1;  // the dot lit, or -1
        float lit_progress = 0;  // how far the lit dot's time has run, 0 to 1
        std::string title;
        std::vector<std::string> lines;
        std::string status;  // a small line at the bottom: is any gaze coming
        bool done = false;
    };

    // `tsv`: a file for one row per frame (angles to the lit dot only, no
    // direction), or empty for none. `log`: where the log lines go.
    explicit GazeProbe(std::function<void(const std::string&)> log = {}, const std::string& tsv = "");
    ~GazeProbe();
    GazeProbe(const GazeProbe&) = delete;
    GazeProbe& operator=(const GazeProbe&) = delete;

    Out update(const Frame& f);
    Card card() const;
    Phase phase() const { return phase_; }
    // The dot lit now, or -1.
    int lit() const { return lit_; }
    // Seconds into the current phase (the sequence's own clock, paused with focus).
    double phase_time() const { return phase_t_; }
    // The summary, logged once when the sequence ends and again at exit
    // (`when` says which); the statistics so far if it never ended.
    void log_summary(const char* when);

    // Statistics of a set of angles, degrees.
    struct Stats {
        size_t n = 0;
        double mean = 0, p50 = 0, p95 = 0, max = 0;
    };
    static Stats stats(std::vector<float> v);
    // The angle between `dir` and the ray from `origin` to `target` (radians),
    // and its sideways and upward parts (positive: the gaze right of / above
    // the target, in the frame where `up` is up).
    struct Error {
        float angle = 0, x = 0, y = 0;
    };
    static Error error(Vec3 origin, Vec3 dir, Vec3 target, Vec3 up);

    // The accuracy gathered in the three rounds (all dots), and with the
    // controllers down: what the go/no-go table reads.
    Stats rounds_stats() const;
    Stats hands_free_stats() const;

private:
    struct Group {
        std::array<std::vector<float>, 5> err;  // degrees, per dot
        std::array<double, 5> sum_x{}, sum_y{};
        std::array<size_t, 5> n_xy{};
    };
    struct Second {
        int frames = 0, focused = 0, active = 0, valid = 0, tracked = 0, positioned = 0;
        int fresh = 0, no_time = 0, changed = 0;
        double lag_sum = 0, lag_max = -1e9, lag_min = 1e9;
        int lag_n = 0;
        double err_sum = 0;
        int err_n = 0;
    };
    struct Totals {
        int64_t frames = 0, focused = 0, active = 0, valid = 0, tracked = 0, fresh = 0, no_time = 0, changed = 0;
        std::vector<float> lag_ms;
        double seconds = 0;
    };

    void enter(Phase p, double now);
    void advance(double dt, Out& out);
    void gather(const Frame& f);
    void log_second();
    void write_row(const Frame& f, bool fresh, double lag_ms, const Error* e, bool settled);
    void log(const std::string& line) const;
    std::string stats_line(const Stats& s) const;

    std::function<void(const std::string&)> log_;
    std::FILE* tsv_ = nullptr;

    Phase phase_ = Phase::Waiting;
    double phase_t_ = 0;       // seconds into the phase
    double seq_t_ = 0;         // seconds since the intro began
    double last_now_ = -1;
    double started_ = -1;      // first frame
    bool ever_focused_ = false;
    double paused_for_ = 0;
    bool pause_logged_ = false;
    int lit_ = -1;
    int round_ = 0;            // 1..kRounds in Rounds
    bool buzzed_ = false;

    Group rounds_, hands_free_;
    Second sec_;
    double sec_start_ = -1;
    Totals all_;
    // The closed-eyes window and after the buzz.
    int closed_frames_ = 0, closed_usable_ = 0;
    double reacquired_after_ = -1;  // seconds from the buzz to usable gaze again
    // Hands free: frames with focus, frames with usable gaze.
    int free_frames_ = 0, free_focused_ = 0, free_usable_ = 0;

    XrTime last_sample_time_ = 0;
    bool have_last_orientation_ = false;
    Quat last_orientation_;
    bool summary_done_ = false;
    bool was_usable_ = false;
};

}  // namespace gaze
