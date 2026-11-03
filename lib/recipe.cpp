#include "prism/recipe.hpp"

#include <charconv>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace prism {

namespace {

// --- Writing -------------------------------------------------------------------------------------

// The shortest text that reads back as exactly `v` (std::to_chars guarantees that), with ".0" on
// whole numbers so they read as decimals, and "+" in front of positive numbers when `sign` is set.
// -0 is written as 0: it means the same as 0 everywhere in the pipeline.
std::string number_text(float v, bool sign = false) {
    if (v == 0.0f) {
        return sign ? "+0.0" : "0.0";
    }
    char buffer[32];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, v);
    if (r.ec != std::errc{}) {
        throw std::logic_error("to_chars failed");  // cannot happen: 32 chars fit any float
    }
    std::string text(buffer, r.ptr);
    // "1e+20" is a valid TOML float already; only plain digits need the ".0".
    if (text.find_first_of(".e") == std::string::npos) {
        text += ".0";
    }
    if (sign && v > 0.0f) {
        text.insert(0, "+");
    }
    return text;
}

// `text` as a TOML basic string: in double quotes, with quotes, backslashes and control
// characters escaped, so it always stays on one line. Other UTF-8 text is written as it is.
std::string toml_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\f': out += "\\f"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                    char escape[8];
                    std::snprintf(escape, sizeof escape, "\\u%04X", static_cast<unsigned>(c));
                    out += escape;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

std::string_view shape_name(CurveShape shape) {
    switch (shape) {
        case CurveShape::none: return "none";
        case CurveShape::gamma: return "gamma";
        case CurveShape::reinhard: return "reinhard";
        case CurveShape::filmic: return "filmic";
    }
    return "none";
}

class RecipeWriter {
public:
    void setting(std::string_view key, const std::string& value) {
        text_ += std::string(key) + " = " + value + "\n";
    }

    void section(std::string_view name) { text_ += "\n[" + std::string(name) + "]\n"; }

    // A stage's switch: written only when it is off.
    void switch_off_if(bool on) {
        if (!on) {
            setting("on", "false");
        }
    }

    [[nodiscard]] std::string take() { return std::move(text_); }

private:
    std::string text_;
};

std::string row_text(const Matrix3& m, int row) {
    return "[" + number_text(m[row * 3]) + ", " + number_text(m[row * 3 + 1]) + ", " +
           number_text(m[row * 3 + 2]) + "]";
}

}  // namespace

std::string write_recipe(const Recipe& recipe) {
    RecipeWriter w;
    w.setting("prism_recipe", std::to_string(recipe_format_version));
    if (recipe.name) {
        w.setting("name", toml_string(*recipe.name));
    }
    if (recipe.description) {
        w.setting("description", toml_string(*recipe.description));
    }
    if (const auto& e = recipe.exposure) {
        w.section("exposure");
        w.setting("stops", number_text(e->stops, true));
        w.switch_off_if(e->on);
    }
    if (const auto& m = recipe.matrix) {
        w.section("matrix");
        constexpr std::string_view rows[] = {"red", "green", "blue"};
        for (int row = 0; row < 3; ++row) {
            w.setting(rows[row], row_text(m->values, row));
        }
        if (m->from) {
            w.setting("from", toml_string(*m->from));
        }
        w.switch_off_if(m->on);
    }
    if (const auto& s = recipe.saturation) {
        w.section("saturation");
        w.setting("amount", number_text(s->amount));
        w.switch_off_if(s->on);
    }
    if (const auto& c = recipe.contrast) {
        w.section("contrast");
        w.setting("amount", number_text(c->amount));
        w.switch_off_if(c->on);
    }
    if (const auto& c = recipe.curve) {
        w.section("curve");
        w.setting("shape", toml_string(shape_name(c->shape)));
        if (c->shape == CurveShape::gamma) {
            w.setting("gamma", number_text(c->gamma));
        } else if (c->shape == CurveShape::reinhard) {
            w.setting("white", number_text(c->white));
        }
        w.switch_off_if(c->on);
    }
    return w.take();
}

}  // namespace prism
