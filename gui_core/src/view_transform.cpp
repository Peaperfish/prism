#include "prism/gui/view_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::gui {

namespace {

constexpr std::array<float, 11> zoom_stops{0.25f, 0.33f, 0.5f, 0.67f, 1.0f, 1.5f,
                                           2.0f,  3.0f,  4.0f, 6.0f,  8.0f};

// Closer than this, two zooms are the same: far below what a 2560 px picture could show.
constexpr float same_zoom_tolerance = 1e-4f;

bool has_picture(const ViewGeometry& g) noexcept {
    return g.width > 0.0f && g.height > 0.0f && g.density > 0.0f && g.image_width > 0 &&
           g.image_height > 0;
}

// The centre on one axis: the middle of a picture smaller than the canvas, otherwise as near to
// `centre` as keeps the canvas covered.
float settled_centre(float centre, float canvas, float density, float zoom, int image) noexcept {
    const float half_view = canvas * density / zoom / 2.0f;  // in image pixels
    const float middle = static_cast<float>(image) / 2.0f;
    if (half_view >= middle) {
        return middle;
    }
    return std::clamp(centre, half_view, static_cast<float>(image) - half_view);
}

// To a whole device pixel.
float snapped(float screen, float density) noexcept {
    return std::round(screen * density) / density;
}

}  // namespace

float view_fit_zoom(const ViewGeometry& g) noexcept {
    if (!has_picture(g)) {
        return 1.0f;
    }
    return std::min(g.width * g.density / static_cast<float>(g.image_width),
                    g.height * g.density / static_cast<float>(g.image_height));
}

float view_zoom(const ViewState& s, const ViewGeometry& g) noexcept {
    return s.fit ? view_fit_zoom(g) : s.zoom;
}

ViewState view_settled(const ViewState& s, const ViewGeometry& g) noexcept {
    if (s.fit || !has_picture(g)) {
        return s;
    }
    const float fit = view_fit_zoom(g);
    if (!(s.zoom > fit) || view_same_zoom(s.zoom, fit)) {
        ViewState out = s;
        out.fit = true;
        return out;
    }
    ViewState out = s;
    out.zoom = std::min(s.zoom, std::max(view_zoom_max, fit));
    out.centre_x = settled_centre(s.centre_x, g.width, g.density, out.zoom, g.image_width);
    out.centre_y = settled_centre(s.centre_y, g.height, g.density, out.zoom, g.image_height);
    return out;
}

ViewPlacement view_placement(const ViewState& s, const ViewGeometry& g) noexcept {
    if (!has_picture(g)) {
        return {{g.x, g.y}, {0.0f, 0.0f}};
    }
    const ViewState settled = view_settled(s, g);
    const float zoom = view_zoom(settled, g);
    const float per_image_pixel = zoom / g.density;  // screen units
    const ViewPoint shown{static_cast<float>(g.image_width) * per_image_pixel,
                          static_cast<float>(g.image_height) * per_image_pixel};
    const float centre_x =
        settled.fit ? static_cast<float>(g.image_width) / 2.0f : settled.centre_x;
    const float centre_y =
        settled.fit ? static_cast<float>(g.image_height) / 2.0f : settled.centre_y;
    return {{snapped(g.x + g.width / 2.0f - centre_x * per_image_pixel, g.density),
             snapped(g.y + g.height / 2.0f - centre_y * per_image_pixel, g.density)},
            shown};
}

ViewRect view_canvas_rect(const ViewGeometry& g) noexcept {
    return {{g.x, g.y}, {g.x + g.width, g.y + g.height}};
}

ViewRect view_intersect(const ViewRect& r, const ViewRect& limit) noexcept {
    return {{std::max(r.min.x, limit.min.x), std::max(r.min.y, limit.min.y)},
            {std::min(r.max.x, limit.max.x), std::min(r.max.y, limit.max.y)}};
}

ViewRect view_visible_rect(const ViewState& s, const ViewGeometry& g) noexcept {
    const ViewPlacement p = view_placement(s, g);
    return view_intersect({p.at, {p.at.x + p.shown.x, p.at.y + p.shown.y}}, view_canvas_rect(g));
}

ViewPoint view_screen_to_image(const ViewState& s, const ViewGeometry& g,
                               ViewPoint screen) noexcept {
    if (!has_picture(g)) {
        return {};
    }
    const ViewPlacement p = view_placement(s, g);
    return {(screen.x - p.at.x) * static_cast<float>(g.image_width) / p.shown.x,
            (screen.y - p.at.y) * static_cast<float>(g.image_height) / p.shown.y};
}

ViewPoint view_image_to_screen(const ViewState& s, const ViewGeometry& g,
                               ViewPoint image) noexcept {
    if (!has_picture(g)) {
        return {g.x, g.y};
    }
    const ViewPlacement p = view_placement(s, g);
    return {p.at.x + image.x * p.shown.x / static_cast<float>(g.image_width),
            p.at.y + image.y * p.shown.y / static_cast<float>(g.image_height)};
}

std::optional<ViewPixel> view_pixel_at(const ViewState& s, const ViewGeometry& g,
                                       ViewPoint screen) noexcept {
    if (!has_picture(g)) {
        return std::nullopt;
    }
    const ViewRect canvas = view_canvas_rect(g);
    if (screen.x < canvas.min.x || screen.x >= canvas.max.x || screen.y < canvas.min.y ||
        screen.y >= canvas.max.y) {
        return std::nullopt;
    }
    const ViewPoint image = view_screen_to_image(s, g, screen);
    const int x = static_cast<int>(std::floor(image.x));
    const int y = static_cast<int>(std::floor(image.y));
    if (x < 0 || y < 0 || x >= g.image_width || y >= g.image_height) {
        return std::nullopt;
    }
    return ViewPixel{x, y};
}

ViewState view_zoomed_to(const ViewState& s, const ViewGeometry& g, float zoom,
                         ViewPoint anchor) noexcept {
    if (!has_picture(g)) {
        return s;
    }
    const ViewState from = view_settled(s, g);
    const float old_zoom = view_zoom(from, g);
    const float old_x = from.fit ? static_cast<float>(g.image_width) / 2.0f : from.centre_x;
    const float old_y = from.fit ? static_cast<float>(g.image_height) / 2.0f : from.centre_y;
    const float to_anchor_x = (anchor.x - (g.x + g.width / 2.0f)) * g.density;  // device pixels
    const float to_anchor_y = (anchor.y - (g.y + g.height / 2.0f)) * g.density;
    // The anchor's image point, which must stay under the anchor at the new zoom.
    const float image_x = old_x + to_anchor_x / old_zoom;
    const float image_y = old_y + to_anchor_y / old_zoom;
    ViewState to;
    to.fit = false;
    to.zoom = std::min(zoom, std::max(view_zoom_max, view_fit_zoom(g)));
    if (!(to.zoom > 0.0f)) {
        to.fit = true;
        return to;
    }
    to.centre_x = image_x - to_anchor_x / to.zoom;
    to.centre_y = image_y - to_anchor_y / to.zoom;
    return view_settled(to, g);
}

ViewState view_zoomed_to(const ViewState& s, const ViewGeometry& g, float zoom) noexcept {
    return view_zoomed_to(s, g, zoom, {g.x + g.width / 2.0f, g.y + g.height / 2.0f});
}

ViewState view_toggled(const ViewState& s, const ViewGeometry& g, ViewPoint anchor) noexcept {
    const ViewState from = view_settled(s, g);
    if (!from.fit && view_same_zoom(from.zoom, 1.0f)) {
        ViewState fit = from;
        fit.fit = true;
        return fit;
    }
    return view_zoomed_to(from, g, 1.0f, anchor);
}

ViewState view_panned_by(const ViewState& s, const ViewGeometry& g, float dx, float dy) noexcept {
    if (!has_picture(g)) {
        return s;
    }
    const ViewState from = view_settled(s, g);
    if (from.fit) {
        return from;  // the whole picture is on the canvas: nowhere to go
    }
    ViewState to = from;
    to.centre_x -= dx * g.density / from.zoom;
    to.centre_y -= dy * g.density / from.zoom;
    return view_settled(to, g);
}

float view_step_zoom(float zoom, float fit_zoom, int direction) noexcept {
    if (direction > 0) {
        for (const float stop : zoom_stops) {
            if (stop > zoom && !view_same_zoom(stop, zoom)) {
                return stop;
            }
        }
        return std::max(zoom, view_zoom_max);
    }
    for (auto stop = zoom_stops.rbegin(); stop != zoom_stops.rend(); ++stop) {
        if (*stop < zoom && !view_same_zoom(*stop, zoom)) {
            return std::max(*stop, fit_zoom);
        }
    }
    return fit_zoom;
}

bool view_same_zoom(float a, float b) noexcept {
    return std::fabs(a - b) <= same_zoom_tolerance;
}

bool view_shows_pixels(float zoom) noexcept {
    return zoom > 2.0f && !view_same_zoom(zoom, 2.0f);
}

std::string view_zoom_label(float zoom, int preview_long_side, int file_long_side) {
    const std::string percent = std::to_string(std::lround(zoom * 100.0f)) + "%";
    return preview_long_side < file_long_side ? percent + " of preview" : percent;
}

std::optional<ViewPoint> view_chip_position(const ViewRect& canvas, float width, float height,
                                            float margin,
                                            std::span<const ViewRect> taken) noexcept {
    const float left = canvas.max.x - margin - width;
    if (left < canvas.min.x + margin) {
        return std::nullopt;
    }
    float top = canvas.max.y - margin - height;
    bool moved = true;
    while (moved) {
        moved = false;
        if (top < canvas.min.y + margin) {
            return std::nullopt;
        }
        for (const ViewRect& r : taken) {
            // A margin's clearance from whatever is there. Each move goes strictly upwards, so this
            // ends.
            const bool across = left < r.max.x + margin && left + width > r.min.x - margin;
            const bool down = top < r.max.y + margin && top + height > r.min.y - margin;
            if (!r.empty() && across && down) {
                top = r.min.y - margin - height;
                moved = true;
            }
        }
    }
    return ViewPoint{left, top};
}

}  // namespace prism::gui
