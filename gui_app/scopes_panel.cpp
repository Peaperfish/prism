#include "scopes_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "imgui_internal.h"
#include "prism/gui/explanations.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/scopes.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace prism::gui_app {

namespace {

constexpr int N = gui::Scopes::size;

constexpr std::array<gui::ScopeChoice, 4> scope_order{
    gui::ScopeChoice::histogram,
    gui::ScopeChoice::parade,
    gui::ScopeChoice::waveform,
    gui::ScopeChoice::vectorscope,
};

// A press on these acts at once (or does nothing) and doesn't hold the active id: it isn't a control
// being used, so it doesn't stop J, T, I or the clip report (see active_control.hpp).
constexpr ImGuiButtonFlags press_only =
    ImGuiButtonFlags_PressedOnClick | static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_NoHoldingActiveId);

float px(float mock_px) {
    return std::round(mock(mock_px));
}

ImU32 u32(const gui::Rgba8& c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

ImU32 u32(const gui::Rgba& c) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}

ImTextureRef texture_ref(ImTextureID id) {
    return ImTextureRef(id);
}

void draw_tabs(const ScopesData& data, const ScopesCallbacks& cb, float width) {
    static constexpr std::array<const char*, 4> labels{"Histogram", "Parade", "Waveform", "Vector"};
    int current = 0;
    for (std::size_t i = 0; i < scope_order.size(); ++i) {
        if (scope_order[i] == data.scope) {
            current = static_cast<int>(i);
        }
    }
    std::string_view hovered_id;
    const bool chosen = segmented(
        "##scope", labels, &current,
        [&](int i) {
            const std::string_view id = gui::explain_id(scope_order[static_cast<std::size_t>(i)]);
            cb.explain(id);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                hovered_id = id;
            }
        },
        width);
    if (chosen) {
        cb.choose_scope(scope_order[static_cast<std::size_t>(current)]);
    }
    if (const gui::Explanation* e = gui::explanation_for(hovered_id)) {
        ImGui::SetTooltip("%s", std::string(e->summary).c_str());
    }
}

// The histogram's picture, with its 1 px edges drawn over it. A stretch where a channel has nothing
// has no edge, so no line runs along the floor.
void draw_histogram_plot(const ScopesData& data, ImDrawList* draw, ImVec2 box_min, ImVec2 box_max) {
    const float rounding = token_metrics().plot_rounding;
    if (data.plot_texture != 0) {
        draw->AddImageRounded(texture_ref(data.plot_texture), box_min, box_max, ImVec2(0, 0),
                              ImVec2(1, 1), IM_COL32_WHITE, rounding);
    }
    if (data.histogram == nullptr || data.histogram->pixel_count == 0) {
        return;
    }
    const gui::StageHistogram& h = *data.histogram;
    const gui::HistogramPaintSize size =
        scopes_histogram_size(box_max.x - box_min.x, data.pixel_density);
    const float density = std::max(data.pixel_density, 0.001f);
    const float left = box_min.x + static_cast<float>(size.inset_x) / density;
    const float right = box_max.x - static_cast<float>(size.inset_x) / density;
    const float top = box_min.y + static_cast<float>(size.inset_top) / density;
    const float bottom = box_max.y;
    const std::uint64_t full_count = gui::histogram_full_count(h);
    constexpr std::int64_t precision = 1 << 16;

    const auto edge = [&](const gui::StageHistogram::Counts& counts, ImU32 colour) {
        const auto at = [&](int bin) {
            const std::int64_t bar = gui::histogram_bar_height(
                counts[static_cast<std::size_t>(bin)], full_count, precision);
            return ImVec2(left + static_cast<float>(bin) / 255.0f * (right - left),
                          bottom - static_cast<float>(bar) / static_cast<float>(precision) * (bottom - top));
        };
        const auto has = [&](int bin) {
            return bin >= 0 && bin < gui::StageHistogram::bins &&
                   counts[static_cast<std::size_t>(bin)] != 0;
        };
        for (int bin = 0; bin < gui::StageHistogram::bins; ++bin) {
            if (has(bin - 1) || has(bin) || has(bin + 1)) {
                draw->PathLineTo(at(bin));
            } else {
                draw->PathStroke(colour, ImDrawFlags_None, hair());
            }
        }
        draw->PathStroke(colour, ImDrawFlags_None, hair());
    };
    edge(h.blue, IM_COL32(120, 160, 255, 230));   // .9
    edge(h.green, IM_COL32(110, 230, 130, 217));  // .85
    edge(h.red, IM_COL32(255, 110, 96, 217));     // .85
}

// Parade and waveform: the counts, with guide lines over them.
void draw_trace_plot(const ScopesData& data, ImDrawList* draw, ImVec2 box_min, ImVec2 box_max) {
    const gui::TokenMetrics& metrics = token_metrics();
    const Palette& colours = palette(data.theme);
    const float line = hair();
    const ImVec2 plot_min(box_min.x + metrics.plot_pad_x, box_min.y + metrics.plot_pad_top);
    const ImVec2 plot_max(box_max.x - metrics.plot_pad_x, box_max.y - metrics.plot_pad_top);
    const int panels = data.scope == gui::ScopeChoice::parade ? 3 : 1;
    const float gap = panels > 1 ? metrics.plot_pad_top : 0.0f;
    const float panel_w = (plot_max.x - plot_min.x - gap * static_cast<float>(panels - 1)) /
                          static_cast<float>(panels);
    for (int p = 0; p < panels; ++p) {
        const float x0 = plot_min.x + static_cast<float>(p) * (panel_w + gap);
        const float x1 = x0 + panel_w;
        if (data.plot_texture != 0) {
            draw->AddImage(texture_ref(data.plot_texture), ImVec2(x0, plot_min.y),
                           ImVec2(x1, plot_max.y),
                           ImVec2(static_cast<float>(p) / static_cast<float>(panels), 0.0f),
                           ImVec2(static_cast<float>(p + 1) / static_cast<float>(panels), 1.0f));
        }
        for (const float level : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float y = std::round(plot_max.y - level * (plot_max.y - plot_min.y));
            draw->AddLine(ImVec2(x0, y), ImVec2(x1, y), colours.scope_guide, line);
        }
    }
}

void draw_vector_plot(const ScopesData& data, ImDrawList* draw, ImVec2 box_min, ImVec2 box_max,
                      const ScopesCallbacks& cb) {
    const gui::TokenMetrics& metrics = token_metrics();
    const Palette& colours = palette(data.theme);
    const float line = hair();
    const float pad = metrics.plot_pad_top;
    const float side = std::min(box_max.x - box_min.x, box_max.y - box_min.y) - 2 * pad;
    const ImVec2 origin((box_min.x + box_max.x) * 0.5f - side * 0.5f,
                        (box_min.y + box_max.y) * 0.5f - side * 0.5f);
    if (data.plot_texture != 0) {
        draw->AddImage(texture_ref(data.plot_texture), origin,
                       ImVec2(origin.x + side, origin.y + side));
    }
    // Cell (cb, cr) is drawn centred at ((cb + 0.5) / 256, (255 - cr + 0.5) / 256) of the square, so
    // the neutral axis is cell 128's centre.
    const auto position = [&](gui::VectorscopeCell c) {
        return ImVec2(origin.x + (static_cast<float>(c.cb) + 0.5f) / N * side,
                      origin.y + (static_cast<float>(N - 1 - c.cr) + 0.5f) / N * side);
    };
    const ImVec2 centre = position({128, 128});
    const float radius = side * 0.5f;
    draw->AddCircle(centre, radius, colours.scope_guide, 64, line);
    draw->AddCircle(centre, radius * 0.5f, colours.scope_guide, 48, line);
    draw->AddLine(ImVec2(centre.x - radius, centre.y), ImVec2(centre.x + radius, centre.y),
                  colours.scope_guide, line);
    draw->AddLine(ImVec2(centre.x, centre.y - radius), ImVec2(centre.x, centre.y + radius),
                  colours.scope_guide, line);

    // Skin tones: the I-line, 123 degrees counter-clockwise from the +Cb axis.
    constexpr float pi = 3.14159265f;
    const float angle = 123.0f * pi / 180.0f;
    const ImVec2 skin_end(centre.x + std::cos(angle) * radius, centre.y - std::sin(angle) * radius);
    draw->AddLine(centre, skin_end, colours.scope_skin, metrics.plot_line);

    // Targets at 75% saturation (inside the ring, like colour bars): R, Mg, B, Cy, G, Yl.
    struct Target {
        const char* name;
        std::uint8_t r, g, b;
        ImU32 colour;
    };
    const std::array<Target, 6> targets{{{"R", 191, 0, 0, colours.histogram_red},
                                         {"Mg", 191, 0, 191, colours.scope_magenta},
                                         {"B", 0, 0, 191, colours.histogram_blue},
                                         {"Cy", 0, 191, 191, colours.scope_cyan},
                                         {"G", 0, 191, 0, colours.histogram_green},
                                         {"Yl", 191, 191, 0, colours.scope_yellow}}};
    const float half = metrics.scope_target_half;
    ImGui::PushFont(nullptr, token_type().tiny);
    for (const Target& t : targets) {
        const ImVec2 p = position(gui::vectorscope_cell(t.r, t.g, t.b));
        draw->AddRect(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), t.colour, 0.0f,
                      0, metrics.plot_line);
        // The label goes on the outer side of the target, away from the centre.
        const ImVec2 size = ImGui::CalcTextSize(t.name);
        const ImVec2 out(p.x - centre.x, p.y - centre.y);
        const float len = std::max(1.0f, std::sqrt(out.x * out.x + out.y * out.y));
        const ImVec2 at(p.x + out.x / len * (half + metrics.scope_label_gap) - (out.x < 0 ? size.x : 0.0f),
                        p.y + out.y / len * (half + metrics.scope_label_gap) - size.y * 0.5f);
        draw->AddText(at, t.colour, t.name);
    }
    ImGui::PopFont();

    if (ImGui::IsItemHovered()) {
        const ImVec2 m = ImGui::GetMousePos();
        const ImVec2 d(skin_end.x - centre.x, skin_end.y - centre.y);
        const float len2 = std::max(d.x * d.x + d.y * d.y, 1.0f);
        const float t = std::clamp(((m.x - centre.x) * d.x + (m.y - centre.y) * d.y) / len2, 0.0f, 1.0f);
        const float dx = m.x - (centre.x + d.x * t);
        const float dy = m.y - (centre.y + d.y * t);
        if (dx * dx + dy * dy <= metrics.scope_hit * metrics.scope_hit) {
            cb.hover(gui::explain::scope_skin);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Skin tones usually sit near this line.");
            }
        }
    }
}

// The line under the plot: blue dot and "0.1% below black" at the left, "0.0% above white" and
// orange dot at the right. Without data it is the same empty band.
void draw_readout(const ScopesData& data, ImVec2 at, float width, float height,
                  const ScopesCallbacks& cb) {
    ImGui::SetCursorScreenPos(at);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_NoTabStop, true);
    ImGui::InvisibleButton("##readout", ImVec2(width, height), press_only);
    ImGui::PopItemFlag();
    if (data.histogram == nullptr || data.histogram->pixel_count == 0) {
        return;
    }
    const gui::StageHistogram& h = *data.histogram;
    const gui::ScopeReadout readout = gui::scope_readout(h);
    const gui::HistogramPaintColours colours = gui::histogram_paint_colours(data.theme);
    const gui::TokenMetrics& m = token_metrics();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::PushFont(nullptr, token_type().tiny);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float y = std::round(at.y + (height - ImGui::GetFontSize()) * 0.5f);
    const float cy = std::round(at.y + height * 0.5f);
    const float dot = 2 * m.dot_radius + m.dot_gap;

    const float left_text = at.x + dot;
    const float left_end = left_text + ImGui::CalcTextSize(readout.below_black.c_str()).x;
    draw->AddCircleFilled(ImVec2(at.x + m.dot_radius, cy), m.dot_radius, u32(colours.shadows_lit));
    draw->AddText(ImVec2(left_text, y), text, readout.below_black.c_str());

    const float right_edge = at.x + width;
    const float right_start = right_edge - dot - ImGui::CalcTextSize(readout.above_white.c_str()).x;
    draw->AddText(ImVec2(right_start, y), text, readout.above_white.c_str());
    draw->AddCircleFilled(ImVec2(right_edge - m.dot_radius, cy), m.dot_radius,
                          u32(colours.highlights_lit));
    ImGui::PopFont();

    if (!ImGui::IsItemHovered()) {
        return;
    }
    // Each end explains its own count, and the rest of the line the sentence the tooltip holds.
    const float mouse = ImGui::GetMousePos().x;
    const bool on_below = mouse <= left_end;
    const bool on_above = mouse >= right_start;
    cb.hover(on_below   ? gui::explain::chip_under
             : on_above ? gui::explain::chip_over
                        : gui::explain::clip_summary);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
        (on_below || on_above || !data.clip_sentence.empty())) {
        ImGui::BeginTooltip();
        if (!data.clip_sentence.empty()) {
            ImGui::PushTextWrapPos(m.tooltip_wrap);
            ImGui::TextUnformatted(data.clip_sentence.c_str());
            ImGui::PopTextWrapPos();
        }
        if (on_below || on_above) {
            const auto percent = [&](const gui::ChannelClips& c) {
                return gui::pixel_percent(on_below ? c.under_black : c.over_white, h.pixel_count);
            };
            ImGui::TextDisabled("%s  R %.2f%%   G %.2f%%   B %.2f%%",
                                on_below ? "Below black:" : "Above white:", percent(h.red_clips),
                                percent(h.green_clips), percent(h.blue_clips));
        }
        ImGui::EndTooltip();
    }
}

}  // namespace

float scopes_plot_height() {
    return std::round(token_metrics().plot_height);
}

gui::HistogramPaintSize scopes_histogram_size(float width, float pixel_density) {
    const float density = std::max(pixel_density, 0.001f);
    return gui::histogram_paint_size(static_cast<int>(std::lround(width * density)),
                                     static_cast<int>(std::lround(scopes_plot_height() * density)),
                                     ImGui::GetStyle().FontScaleDpi * density);
}

ScopesRects draw_scopes(const ScopesData& data, const ScopesCallbacks& cb) {
    ScopesRects rects;
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);

    // The header. A long title is shortened: it can't push the action off the panel.
    const char* const action =
        data.clip_overlay_on ? "Hide clipping (J)###clip_overlay" : "Show clipping (J)###clip_overlay";
    ImGui::PushFont(nullptr, token_type().section);
    const float action_width = ImGui::CalcTextSize(action, nullptr, true).x;
    ImGui::PopFont();
    ImGui::PushFont(fonts().semibold, token_type().section);
    const std::string full_title = data.detail.empty() ? "Scopes" : "Scopes (" + data.detail + ")";
    const std::string title = gui::ellipsize_middle(
        full_title, std::max(0.0f, width - action_width - px(8.0f)), [](std::string_view s) {
            return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        });
    ImGui::PopFont();
    bool title_hovered = false;
    const bool toggled = section_text(title.c_str(), action, &title_hovered, press_only);
    rects.action_min = ImGui::GetItemRectMin();
    rects.action_max = ImGui::GetItemRectMax();
    cb.describe(gui::explain::clip_overlay);
    if (title_hovered) {
        cb.hover(gui::explain_id(data.scope));
    }
    if (toggled) {
        cb.toggle_clip_overlay();
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // The tabs.
    ImGui::SetCursorScreenPos(origin);
    draw_tabs(data, cb, width);
    rects.tabs_min = origin;
    rects.tabs_max = ImVec2(origin.x + width, origin.y + segmented_height());

    // The plot box.
    const float height = scopes_plot_height();
    rects.plot_min = ImVec2(origin.x, rects.tabs_max.y + px(8.0f));
    rects.plot_max = ImVec2(origin.x + width, rects.plot_min.y + height);
    ImGui::SetCursorScreenPos(rects.plot_min);
    ImGui::InvisibleButton("##scope_plot", ImVec2(width, height), press_only);
    cb.explain(gui::explain_id(data.scope));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(rects.plot_min, rects.plot_max, u32(token_colours().plot),
                        token_metrics().plot_rounding);
    draw->PushClipRect(rects.plot_min, rects.plot_max, true);
    switch (data.scope) {
        case gui::ScopeChoice::histogram:
            draw_histogram_plot(data, draw, rects.plot_min, rects.plot_max);
            break;
        case gui::ScopeChoice::parade:
        case gui::ScopeChoice::waveform:
            draw_trace_plot(data, draw, rects.plot_min, rects.plot_max);
            break;
        case gui::ScopeChoice::vectorscope:
            draw_vector_plot(data, draw, rects.plot_min, rects.plot_max, cb);
            break;
    }
    draw->PopClipRect();

    // The readout: its height doesn't ask what is in it.
    ImGui::PushFont(nullptr, token_type().tiny);
    const float line = std::ceil(ImGui::GetFontSize());
    ImGui::PopFont();
    rects.readout_min = ImVec2(origin.x, rects.plot_max.y + px(6.0f));
    rects.readout_max = ImVec2(origin.x + width, rects.readout_min.y + line);
    draw_readout(data, rects.readout_min, width, line, cb);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, rects.readout_max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return rects;
}

}  // namespace prism::gui_app
