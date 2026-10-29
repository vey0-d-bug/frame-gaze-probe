#pragma once

#include "xr_math.h"

namespace gaze {

// The eye gaze pose for one frame (XR_EXT_eye_gaze_interaction, the input code's
// gaze action; only with --gaze-probe). The pose is in the space the hands
// are located in: its position the point between the eyes, its -Z where they
// look. Nothing of it is logged or kept: the probe keeps angles to the dot it
// shows, nothing else.
struct GazeSample {
    bool bound = false;   // the action exists and its binding was accepted
    bool active = false;  // xrGetActionStatePose's isActive
    XrSpaceLocationFlags flags = 0;
    Pose pose;
    XrTime sample_time = 0;   // XrEyeGazeSampleTimeEXT; 0: the runtime didn't say
    XrTime display_time = 0;  // the frame's predicted display time it was located at

    bool orientation_valid() const { return (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0; }
    bool position_valid() const { return (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0; }
    bool tracked() const { return (flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0; }
    // What counts as gaze here: the action active and the ray valid.
    bool usable() const { return active && orientation_valid(); }
};

}  // namespace gaze
