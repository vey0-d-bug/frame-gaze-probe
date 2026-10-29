// The gaze probe's card (--gaze-probe): the
// five dots over the whole panel, the lit one bright with a ring that runs
// down with its time, the probe's words at the top (or in the middle once
// it is done) and its status line at the bottom. Plain black, like a
// test card, so the eyes have nothing else to land on.

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "hud/hud.h"
#include "hud/placement.h"

namespace gaze {

void Hud::draw_gaze_card() {
    const GazeProbe::Card& c = *gaze_card_;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = static_cast<float>(kPanelWidthPx), h = static_cast<float>(kPanelHeightPx);
    const ImU32 black = IM_COL32(0, 0, 0, 255), white = IM_COL32(255, 255, 255, 255), dim = IM_COL32(96, 96, 96, 255),
                grey = IM_COL32(190, 190, 190, 255), ring = IM_COL32(255, 200, 60, 255);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), black);

    for (size_t i = 0; i < GazeProbe::kDots.size(); ++i) {
        const GazeProbe::Pixel p = GazeProbe::dot_pixel(i);
        const ImVec2 at(p.x, p.y);
        if (static_cast<int>(i) == c.lit) {
            dl->AddCircleFilled(at, 16.0f, white, 48);
            dl->AddCircleFilled(at, 3.0f, black, 16);  // a point to fixate
            // The ring runs down clockwise from the top as the dot's time goes.
            const float left = std::clamp(1.0f - c.lit_progress, 0.0f, 1.0f);
            if (left > 0.01f) {
                const float pi = 3.14159265f;
                dl->PathArcTo(at, 30.0f, -pi * 0.5f, -pi * 0.5f + 2.0f * pi * left, 48);
                dl->PathStroke(ring, 0, 4.0f);
            }
        } else {
            dl->AddCircle(at, 10.0f, dim, 32, 2.0f);
        }
    }

    // The words: at the top while the dots are in use, in the middle once done.
    const float title_px = 40.0f, line_px = c.done ? 36.0f : 30.0f;
    float y = c.done ? h * 0.5f - (static_cast<float>(c.lines.size()) * line_px * 1.35f + title_px) * 0.5f : 24.0f;
    const float x = 48.0f;
    dl->AddText(nullptr, title_px, ImVec2(x, y), grey, c.title.c_str());
    y += title_px * 1.4f;
    for (const std::string& line : c.lines) {
        dl->AddText(nullptr, line_px, ImVec2(x, y), white, line.c_str());
        y += line_px * 1.35f;
    }
    if (!c.status.empty()) dl->AddText(nullptr, 30.0f, ImVec2(x, h - 30.0f - 24.0f), grey, c.status.c_str());
}

}  // namespace gaze
