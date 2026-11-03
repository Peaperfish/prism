#include "prism/recipe.hpp"

#include "prism/atomic_file.hpp"
#include "prism/utf8_path.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

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
    explicit RecipeWriter(bool comments) : comments_(comments) {}

    // `key = value`, with `comment` after it when comments are on.
    void setting(std::string_view key, const std::string& value, const std::string& comment = {}) {
        text_ += std::string(key) + " = " + value;
        if (comments_ && !comment.empty()) {
            text_ += "  # " + comment;
        }
        text_ += "\n";
    }

    void section(std::string_view name) { text_ += "\n[" + std::string(name) + "]\n"; }

    void comment_line(std::string_view text) {
        if (comments_) {
            text_ += "# " + std::string(text) + "\n";
        }
    }

    // A stage's switch: written only when it is off.
    void switch_off_if(bool on) {
        if (!on) {
            setting("on", "false", "off: Prism skips this stage but keeps its values");
        }
    }

    [[nodiscard]] std::string take() { return std::move(text_); }

private:
    bool comments_;
    std::string text_;
};

std::string row_text(const Matrix3& m, int row) {
    return "[" + number_text(m[row * 3]) + ", " + number_text(m[row * 3 + 1]) + ", " +
           number_text(m[row * 3 + 2]) + "]";
}

// --- The comments' numbers ---
//
// Worked out with the engine's own maths, as the GUI's Explain area does (explanations.cpp), and
// formatted the same way, so a recipe and the window never disagree; a gui_core test checks that.

std::string printf_text(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

// Exposure multiplies the light by 2^stops.
std::string exposure_comment(float stops) {
    return printf_text("%.2f", static_cast<double>(std::exp2(stops))) + "x the light";
}

// Saturation scales each colour's distance from grey.
std::string saturation_comment(float amount) {
    if (amount == 0.0f) {
        return "black and white";
    }
    return printf_text("%.2f", static_cast<double>(amount)) + "x the colourfulness";
}

// Contrast's slope at middle grey is the amount: each stop there is (amount - 1) x 100% wider.
std::string contrast_comment(float amount) {
    const long percent = std::lround(std::fabs(static_cast<double>(amount) - 1.0) * 100.0);
    if (percent == 0) {
        return "no change near middle grey";
    }
    return "each stop near middle grey is " + std::to_string(percent) + "% " +
           (amount > 1.0f ? "wider" : "narrower");
}

// What 18% grey becomes through the curve (ToneCurve::map), as a whole percent.
std::string curve_comment(const RecipeCurve& curve) {
    if (curve.shape == CurveShape::none) {
        return "no tone curve";
    }
    const bool valid = curve.shape == CurveShape::gamma      ? is_positive(curve.gamma)
                       : curve.shape == CurveShape::reinhard ? is_positive(curve.white)
                                                             : true;
    if (!valid) {
        return {};  // parse_curve would refuse it; the recipe would not read back either
    }
    const ToneCurve tone = parse_curve(*curve_spec(curve));
    const long percent = std::lround(static_cast<double>(tone.map(contrast_pivot)) * 100.0);
    return "18% grey becomes " + std::to_string(percent) + "%";
}

}  // namespace

std::string write_recipe(const Recipe& recipe, const RecipeWriteOptions& options) {
    RecipeWriter w(options.comments);
    w.comment_line(
        "Prism recipe: edit settings in plain text. Prism rewrites these comments when it saves.");
    w.comment_line(
        "What each line means: https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md");
    w.setting("prism_recipe", std::to_string(recipe_format_version), "recipe format version");
    if (recipe.name) {
        w.setting("name", toml_string(*recipe.name));
    }
    if (recipe.description) {
        w.setting("description", toml_string(*recipe.description));
    }
    if (const auto& e = recipe.exposure) {
        w.section("exposure");
        w.setting("stops", number_text(e->stops, true), exposure_comment(e->stops));
        w.switch_off_if(e->on);
    }
    if (const auto& m = recipe.matrix) {
        w.section("matrix");
        const bool none = m->values == identity_matrix && !m->from;
        constexpr std::string_view rows[] = {"red", "green", "blue"};
        for (int row = 0; row < 3; ++row) {
            w.setting(rows[row], row_text(m->values, row),
                      none ? "identity: no colour matrix"
                           : "output " + std::string(rows[row]) + " from input R, G, B");
        }
        if (m->from) {
            w.setting("from", toml_string(*m->from),
                      "where the numbers came from: a label, never opened");
        }
        w.switch_off_if(m->on);
    }
    if (const auto& s = recipe.saturation) {
        w.section("saturation");
        w.setting("amount", number_text(s->amount), saturation_comment(s->amount));
        w.switch_off_if(s->on);
    }
    if (const auto& c = recipe.contrast) {
        w.section("contrast");
        w.setting("amount", number_text(c->amount), contrast_comment(c->amount));
        w.switch_off_if(c->on);
    }
    if (const auto& c = recipe.curve) {
        w.section("curve");
        w.setting("shape", toml_string(shape_name(c->shape)), curve_comment(*c));
        if (c->shape == CurveShape::gamma) {
            w.setting("gamma", number_text(c->gamma), "out = in^(1/gamma): above 1 brightens");
        } else if (c->shape == CurveShape::reinhard) {
            w.setting("white", number_text(c->white), "this scene value becomes pure white");
        }
        w.switch_off_if(c->on);
    }
    return w.take();
}

// --- Reading -------------------------------------------------------------------------------------

namespace {

using ValueType = RecipeValueType;
using KeySpec = RecipeKeySpec;

constexpr RecipeSectionSpec section_specs[] = {
    {"exposure", 1}, {"matrix", 1}, {"saturation", 1}, {"contrast", 1}, {"curve", 1},
};

constexpr std::string_view weights = "weights of input R, G, B";

// Every key of format version 1, at the top of the file and then in pipeline order.
constexpr RecipeKeySpec key_specs[] = {
    {"", "prism_recipe", ValueType::version, 1, true, {}, false, "format version", {}, {}},
    {"", "name", ValueType::text, 1, false, {}, false, "text", {}, "none"},
    {"", "description", ValueType::text, 1, false, {}, false, "text", {}, "none"},
    {"exposure", "stops", ValueType::number, 1, true, {}, false, "stops", {}, {}},
    {"exposure", "on", ValueType::boolean, 1, false, {}, false, "switch", {}, "true"},
    {"matrix", "red", ValueType::row, 1, true, {}, false, weights, {}, {}},
    {"matrix", "green", ValueType::row, 1, true, {}, false, weights, {}, {}},
    {"matrix", "blue", ValueType::row, 1, true, {}, false, weights, {}, {}},
    {"matrix", "from", ValueType::text, 1, false, {}, false, "file name", {}, "none"},
    {"matrix", "on", ValueType::boolean, 1, false, {}, false, "switch", {}, "true"},
    {"saturation", "amount", ValueType::number, 1, true, saturation_range, false, "factor", {}, {}},
    {"saturation", "on", ValueType::boolean, 1, false, {}, false, "switch", {}, "true"},
    {"contrast", "amount", ValueType::number, 1, true, contrast_range, false, "factor", {}, {}},
    {"contrast", "on", ValueType::boolean, 1, false, {}, false, "switch", {}, "true"},
    {"curve", "shape", ValueType::shape, 1, true, {}, false, "curve", {}, {}},
    {"curve", "gamma", ValueType::number, 1, false, {}, true, "exponent", gamma_default, {}},
    {"curve",
     "white",
     ValueType::number,
     1,
     false,
     {},
     true,
     "linear value",
     reinhard_white_default,
     {}},
    {"curve", "on", ValueType::boolean, 1, false, {}, false, "switch", {}, "true"},
};

constexpr RecipeFormat format_v1{recipe_format_version, section_specs, key_specs};

constexpr CurveShape all_shapes[] = {CurveShape::none, CurveShape::gamma, CurveShape::reinhard,
                                     CurveShape::filmic};

// An error found while reading one value; its line is added by the caller.
struct ValueError {
    std::string message;
};

// A value as written, before it is checked against what its key takes.
struct Value {
    enum class Kind { number, boolean, text, list, word };
    Kind kind = Kind::word;
    std::string raw;  // the value's text as it is in the file (for messages)
    float number = 0.0f;
    bool integer = false;  // a number written without a fraction or exponent
    bool boolean = false;
    std::string text;            // a quoted string's contents, unescaped
    std::vector<float> numbers;  // a list's numbers
};

bool is_space(char c) {
    return c == ' ' || c == '\t';
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// A TOML bare key's characters.
bool is_key_char(char c) {
    return is_letter(c) || is_digit(c) || c == '_' || c == '-';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Whether `s` is well-formed UTF-8: no stray continuation bytes, overlong forms, surrogates or
// code points above U+10FFFF.
bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
        const unsigned char b = byte(i);
        if (b < 0x80) {
            ++i;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) {
            length = 2;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            length = 3;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            length = 4;
            cp = b & 0x07;
        } else {
            return false;
        }
        if (i + length > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            if ((byte(i + k) & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (byte(i + k) & 0x3F);
        }
        constexpr std::uint32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < smallest[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The number of edits (insertions, deletions, substitutions and swaps of two neighbouring
// letters) between a and b, ignoring case: "amout" is 1 from "amount", "nmae" 1 from "name".
std::size_t edit_distance(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
    const std::size_t width = b.size() + 1;
    std::vector<std::size_t> d((a.size() + 1) * width);
    const auto at = [&](std::size_t i, std::size_t j) -> std::size_t& { return d[i * width + j]; };
    for (std::size_t i = 0; i <= a.size(); ++i) {
        at(i, 0) = i;
    }
    for (std::size_t j = 0; j <= b.size(); ++j) {
        at(0, j) = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = lower(a[i - 1]) == lower(b[j - 1]) ? 0 : 1;
            at(i, j) = std::min({at(i - 1, j) + 1, at(i, j - 1) + 1, at(i - 1, j - 1) + cost});
            if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) &&
                lower(a[i - 2]) == lower(b[j - 1])) {
                at(i, j) = std::min(at(i, j), at(i - 2, j - 2) + 1);
            }
        }
    }
    return at(a.size(), b.size());
}

// The candidate closest to `word`, if it is close enough to be a likely typing mistake: at most
// two edits, and fewer than half the candidate's length.
template <typename Range>
std::optional<std::string_view> closest(std::string_view word, const Range& candidates) {
    std::optional<std::string_view> best;
    std::size_t best_distance = 0;
    for (const std::string_view candidate : candidates) {
        const std::size_t d = edit_distance(word, candidate);
        if (d <= 2 && 2 * d < candidate.size() && (!best || d < best_distance)) {
            best = candidate;
            best_distance = d;
        }
    }
    return best;
}

// --- Numbers ---
//
// A number is first checked against TOML's grammar, so that what Prism accepts is exactly what
// other TOML tools accept (minus inf, nan, hexadecimal, octal and binary), and only then turned
// into a float, by std::strtof: straight to float, so no double rounding. std::from_chars for
// float would avoid strtof's dependence on the C locale, but it needs macOS 26 (Prism supports
// 13.3). Prism's own code never calls setlocale, so strtof reads a dot as the decimal point. (On
// Linux, SDL's X11 code sets LC_ALL from the environment for a moment while it sets up the
// keyboard or shows a message box, and then puts it back.)

// Digits with single underscores between them ("1_000"), from s[i]. Returns the end, or `i` if
// there is no digit there or an underscore is misplaced.
std::size_t scan_digits(std::string_view s, std::size_t i) {
    const std::size_t start = i;
    if (i >= s.size() || !is_digit(s[i])) {
        return start;
    }
    ++i;
    while (i < s.size()) {
        if (is_digit(s[i])) {
            ++i;
        } else if (s[i] == '_' && i + 1 < s.size() && is_digit(s[i + 1])) {
            i += 2;
        } else {
            break;
        }
    }
    return i;
}

// The length of the TOML integer or float at the start of `s`, or 0 if there is none. `integer`
// says which it was.
std::size_t scan_toml_number(std::string_view s, bool& integer) {
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        ++i;
    }
    const std::size_t int_start = i;
    if (i < s.size() && s[i] == '0') {
        ++i;  // a lone 0: no leading zeros
    } else {
        i = scan_digits(s, i);
        if (i == int_start) {
            return 0;
        }
    }
    integer = true;
    if (i < s.size() && s[i] == '.') {
        const std::size_t end = scan_digits(s, i + 1);
        if (end == i + 1) {
            return i;  // "5." has no fraction: the number is just "5"
        }
        i = end;
        integer = false;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) {
            ++j;
        }
        const std::size_t end = scan_digits(s, j);
        if (end != j) {
            i = end;
            integer = false;
        }
    }
    return i;
}

// A valid TOML number's text as a float. Underscores are dropped first; strtof reads the rest.
std::variant<float, ValueError> to_float(std::string_view token) {
    std::string digits;
    for (const char c : token) {
        if (c != '_') {
            digits += c;
        }
    }
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(digits.c_str(), &end);
    if (end != digits.c_str() + digits.size()) {
        // Only possible if the C locale had another decimal point (see above).
        return ValueError{"'" + std::string(token) + "' is not a number"};
    }
    if (std::isinf(value)) {
        return ValueError{"'" + std::string(token) +
                          "' is too large: a recipe's numbers must be below 3.4e38"};
    }
    if (errno == ERANGE && value == 0.0f) {
        return ValueError{"'" + std::string(token) +
                          "' is too close to 0 for Prism to hold: write 0 instead"};
    }
    return value;
}

std::string quote(std::string_view s) {
    return "'" + std::string(s) + "'";
}

// Why `token`, which is not a valid number, was probably meant as one but is written wrongly; or
// nothing if it does not look like a number at all.
std::optional<std::string> number_mistake(std::string_view token) {
    std::string_view body = token;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) {
        body.remove_prefix(1);
    }
    if (body == "inf" || body == "nan") {
        return quote(token) + " is not allowed: a recipe holds finite numbers only";
    }
    if (body.size() > 1 && body[0] == '0' &&
        (body[1] == 'x' || body[1] == 'X' || body[1] == 'o' || body[1] == 'b')) {
        return quote(token) + " is not a number Prism reads: write it in decimal";
    }
    if (body.size() > 1 && body[0] == '.' && is_digit(body[1])) {
        return quote(token) + " needs a digit before the dot, as in 0" + std::string(body);
    }
    bool integer = false;
    const std::size_t length = scan_toml_number(token, integer);
    if (length == 0) {
        return std::nullopt;
    }
    const std::string_view rest = token.substr(length);
    if (rest.size() > 1 && rest[0] == ',' && is_digit(rest[1])) {
        std::string dotted(token);
        dotted[length] = '.';
        return quote(token) + " is not a number: use a dot for decimals, as in " + dotted;
    }
    if (rest == ".") {
        return quote(token) + " needs a digit after the dot, as in " + std::string(token) + "0";
    }
    if (body.size() > 1 && body[0] == '0' && is_digit(body[1])) {
        return quote(token) + " is not a number: write it without leading zeros";
    }
    const std::string_view after = trim(rest);
    if (!after.empty() &&
        (is_letter(after[0]) || after[0] == '%' || static_cast<unsigned char>(after[0]) >= 0x80)) {
        return quote(token) + " is not a number: write the number alone, without a unit";
    }
    return std::nullopt;
}

// One item of a list, or a whole value that is not quoted, not a list and not true or false.
std::variant<Value, ValueError> read_bare(std::string_view token) {
    Value v;
    v.raw = std::string(token);
    bool integer = false;
    if (scan_toml_number(token, integer) == token.size() && !token.empty()) {
        auto number = to_float(token);
        if (const auto* error = std::get_if<ValueError>(&number)) {
            return *error;
        }
        v.kind = Value::Kind::number;
        v.number = std::get<float>(number);
        v.integer = integer;
        return v;
    }
    if (std::optional<std::string> mistake = number_mistake(token)) {
        return ValueError{*mistake};
    }
    if (std::all_of(token.begin(), token.end(), is_key_char)) {
        v.kind = Value::Kind::word;  // what it means depends on the key: see check_type
        return v;
    }
    return ValueError{quote(token) + " is not a value Prism can read"};
}

// A double-quoted string starting at line[pos] (the opening quote). On success `pos` is just past
// the closing quote.
std::variant<Value, ValueError> read_string(std::string_view line, std::size_t& pos) {
    if (line.substr(pos, 3) == "\"\"\"") {
        return ValueError{
            "text in triple quotes is not part of a recipe: write it on one line in "
            "double quotes"};
    }
    Value v;
    v.kind = Value::Kind::text;
    std::size_t i = pos + 1;
    while (true) {
        if (i >= line.size()) {
            return ValueError{
                "the text starting with \" is not closed: end it with \" on the "
                "same line"};
        }
        const char c = line[i];
        if (c == '"') {
            break;
        }
        if (c != '\\') {
            v.text += c;
            ++i;
            continue;
        }
        if (i + 1 >= line.size()) {
            return ValueError{
                "the text starting with \" is not closed: end it with \" on the "
                "same line"};
        }
        const char e = line[i + 1];
        i += 2;
        switch (e) {
            case 'b': v.text += '\b'; continue;
            case 't': v.text += '\t'; continue;
            case 'n': v.text += '\n'; continue;
            case 'f': v.text += '\f'; continue;
            case 'r': v.text += '\r'; continue;
            case '"': v.text += '"'; continue;
            case '\\': v.text += '\\'; continue;
            case 'u':
            case 'U': {
                const std::size_t digits = e == 'u' ? 4 : 8;
                const std::string_view hex = line.substr(i, digits);
                std::uint32_t cp = 0;
                const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), cp, 16);
                if (hex.size() != digits || ec != std::errc{} || end != hex.data() + hex.size() ||
                    cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    return ValueError{"'\\" + std::string(1, e) + std::string(hex) +
                                      "' is not a valid character escape"};
                }
                append_utf8(v.text, cp);
                i += digits;
                continue;
            }
            default:
                return ValueError{"unknown escape '\\" + std::string(1, e) +
                                  "' in text: write \\\\ for a backslash"};
        }
    }
    v.raw = std::string(line.substr(pos, i + 1 - pos));
    pos = i + 1;
    return v;
}

// Whether a list's text has a comma between two digits, as in [1,05, 0,03, -0,01]: written with
// commas for decimals. ([1,0,0] is a valid list too, so this is only a hint once a list is wrong.)
bool has_decimal_comma(std::string_view list) {
    for (std::size_t i = 1; i + 1 < list.size(); ++i) {
        if (list[i] == ',' && is_digit(list[i - 1]) && is_digit(list[i + 1])) {
            return true;
        }
    }
    return false;
}

std::string decimal_comma_hint(std::string_view list) {
    return quote(list) +
           " seems to use commas for decimals: write each number with a dot, and a "
           "comma between numbers, as in [1.05, 0.03, -0.01]";
}

// A list in square brackets starting at line[pos], on one line: numbers separated by commas, with
// an optional comma after the last. On success `pos` is just past the ].
std::variant<Value, ValueError> read_list(std::string_view line, std::size_t& pos) {
    const std::size_t close = line.find(']', pos);
    const std::size_t hash = line.find('#', pos);
    if (close == std::string_view::npos || (hash != std::string_view::npos && hash < close)) {
        return ValueError{"the list is not closed: end it with ] on the same line"};
    }
    Value v;
    v.kind = Value::Kind::list;
    v.raw = std::string(line.substr(pos, close + 1 - pos));
    const std::string_view inner = line.substr(pos + 1, close - pos - 1);
    pos = close + 1;
    if (trim(inner).empty()) {
        return v;
    }
    std::size_t start = 0;
    while (start <= inner.size()) {
        std::size_t comma = inner.find(',', start);
        const bool last = comma == std::string_view::npos;
        if (last) {
            comma = inner.size();
        }
        const std::string_view item = trim(inner.substr(start, comma - start));
        start = comma + 1;
        if (item.empty()) {
            if (last && !v.numbers.empty()) {
                break;  // a comma after the last number, which TOML allows
            }
            return ValueError{"the list has an empty item: remove the extra comma"};
        }
        if (item.find_first_of("\"[") != std::string_view::npos) {
            return ValueError{"the list must hold numbers only"};
        }
        auto read = read_bare(item);
        const Value* number = std::get_if<Value>(&read);
        if (number == nullptr || number->kind != Value::Kind::number) {
            if (has_decimal_comma(v.raw)) {
                return ValueError{decimal_comma_hint(v.raw)};
            }
            if (number == nullptr) {
                return std::get<ValueError>(read);
            }
            return ValueError{quote(item) + " is not a number"};
        }
        v.numbers.push_back(number->number);
        if (last) {
            break;
        }
    }
    return v;
}

// The value of `key` on this line, starting at line[pos] (just after "= " and any spaces), up to
// a comment or the end of the line.
std::variant<Value, ValueError> read_value(std::string_view key, std::string_view line,
                                           std::size_t pos) {
    std::variant<Value, ValueError> result;
    const char first = line[pos];
    if (first == '"') {
        result = read_string(line, pos);
    } else if (first == '\'') {
        return ValueError{"text needs double quotes, not single ones: " + std::string(key) +
                          " = \"...\""};
    } else if (first == '[') {
        result = read_list(line, pos);
    } else {
        // Everything up to a comment: so "1.5 stops" is seen whole, and reported as a unit.
        std::size_t end = line.find('#', pos);
        if (end == std::string_view::npos) {
            end = line.size();
        }
        const std::string_view token = trim(line.substr(pos, end - pos));
        if (token == "true" || token == "false") {
            Value v;
            v.kind = Value::Kind::boolean;
            v.boolean = token == "true";
            v.raw = std::string(token);
            return v;
        }
        // Several words: the first may be a value with something after it.
        const std::size_t space = token.find_first_of(" \t");
        if (space != std::string_view::npos) {
            if (std::optional<std::string> mistake = number_mistake(token)) {
                return ValueError{*mistake};
            }
            const std::string_view word = token.substr(0, space);
            auto head = read_bare(word);
            if (std::holds_alternative<ValueError>(head)) {
                return head;
            }
            if (std::get<Value>(head).kind == Value::Kind::word) {
                return ValueError{"text needs double quotes, as in " + std::string(key) + " = \"" +
                                  std::string(token) + "\""};
            }
            return ValueError{"unexpected " + quote(trim(token.substr(space))) +
                              " after the value of '" + std::string(key) + "'"};
        }
        return read_bare(token);
    }
    if (std::holds_alternative<ValueError>(result)) {
        return result;
    }
    while (pos < line.size() && is_space(line[pos])) {
        ++pos;
    }
    if (pos < line.size() && line[pos] != '#') {
        return ValueError{"unexpected " + quote(trim(line.substr(pos))) + " after the value of '" +
                          std::string(key) + "'"};
    }
    return result;
}

std::string in_section(std::string_view section) {
    return section.empty() ? std::string() : " in [" + std::string(section) + "]";
}

// The message for a value of the wrong kind for `key`, or nothing if it is the right kind.
std::optional<std::string> check_type(const KeySpec& spec, const Value& v) {
    const std::string key = "'" + std::string(spec.key) + "'";
    switch (spec.type) {
        case ValueType::version:
            if (v.kind == Value::Kind::number && v.integer) {
                return std::nullopt;
            }
            return "prism_recipe must be a whole number, the format version, as in prism_recipe "
                   "= 1 (it is " +
                   v.raw + ")";
        case ValueType::number:
            switch (v.kind) {
                case Value::Kind::number: return std::nullopt;
                case Value::Kind::text: return key + " needs a number, not text in quotes";
                case Value::Kind::boolean: return key + " needs a number, not true or false";
                case Value::Kind::list: return key + " needs one number, not a list";
                case Value::Kind::word: return quote(v.raw) + " is not a number";
            }
            break;
        case ValueType::boolean:
            if (v.kind == Value::Kind::boolean) {
                return std::nullopt;
            }
            return key + " must be true or false (it is " + v.raw + ")";
        case ValueType::text:
        case ValueType::shape:
            if (v.kind == Value::Kind::text) {
                return std::nullopt;
            }
            if (v.kind == Value::Kind::word) {
                return "text needs double quotes, as in " + std::string(spec.key) + " = \"" +
                       v.raw + "\"";
            }
            return key + " needs text in double quotes";
        case ValueType::row:
            if (v.kind == Value::Kind::list) {
                if (v.numbers.size() == 3) {
                    return std::nullopt;
                }
                if (has_decimal_comma(v.raw)) {
                    return decimal_comma_hint(v.raw);
                }
                return key + " needs 3 numbers, as in [1.0, 0.0, 0.0] (it has " +
                       std::to_string(v.numbers.size()) + ")";
            }
            return key + " needs 3 numbers in square brackets, as in [1.0, 0.0, 0.0]";
    }
    return std::nullopt;
}

std::optional<CurveShape> shape_from(std::string_view name) {
    for (const CurveShape shape : all_shapes) {
        if (shape_name(shape) == name) {
            return shape;
        }
    }
    return std::nullopt;
}

class RecipeParser {
public:
    explicit RecipeParser(const RecipeFormat& format)
        : format_(format), file_version_(format.latest) {}

    Recipe parse(std::string_view text) {
        if (text.size() > recipe_max_bytes) {
            fail(0, "the file is larger than 64 KB, too large to be a recipe");
            return {};
        }
        if (text.substr(0, 3) == "\xEF\xBB\xBF") {
            text.remove_prefix(3);
        }
        int number = 0;
        while (!text.empty() && !full() && !refused_) {
            const std::size_t newline = text.find('\n');
            std::string_view line = text.substr(0, newline);
            text =
                newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            read_line(++number, line);
        }
        if (!full() && !refused_) {
            check_sections();
        }
        if (!seen_version_ && issues_.empty()) {
            fail(0, "there are no settings: a recipe starts with prism_recipe = 1");
        }
        return std::move(recipe_);
    }

    [[nodiscard]] std::vector<RecipeIssue> take_issues() {
        std::stable_sort(
            issues_.begin(), issues_.end(),
            [](const RecipeIssue& a, const RecipeIssue& b) { return a.line < b.line; });
        return std::move(issues_);
    }

private:
    struct SectionState {
        int line = 0;
        std::vector<std::pair<std::string_view, int>> keys;  // each key given, with its line
        // A line in it had a mistake. Its missing keys are then not reported: the mistake may
        // well be the missing key, misspelt.
        bool had_error = false;
    };

    [[nodiscard]] bool full() const { return issues_.size() >= recipe_max_errors; }

    void fail(int line, std::string message) {
        if (current_) {
            sections_[*current_].second.had_error = true;
        }
        if (!full()) {
            issues_.push_back({line, std::move(message)});
        }
    }

    void read_line(int number, std::string_view line) {
        if (line.find('\0') != std::string_view::npos) {
            fail(number, "the line has a NUL byte: this does not look like a text file");
            return;
        }
        if (!valid_utf8(line)) {
            fail(number, "the line is not valid UTF-8 text");
            return;
        }
        for (const char c : line) {
            const auto byte = static_cast<unsigned char>(c);
            if ((byte < 0x20 && c != '\t') || byte == 0x7F) {
                char hex[8];
                std::snprintf(hex, sizeof hex, "%02X", static_cast<unsigned>(byte));
                fail(number, std::string("the line has a control character (byte 0x") + hex +
                                 "): write it in text as an escape such as \\t or \\n");
                return;
            }
        }
        const std::string_view content = trim(line);
        if (content.empty() || content.front() == '#') {
            return;
        }
        const bool first = !seen_setting_;
        seen_setting_ = true;
        if (content.front() == '[') {
            if (first) {
                fail(number, "a recipe starts with prism_recipe = 1, before any [section]");
            }
            read_header(number, content);
            return;
        }
        read_setting(number, content, first);
    }

    void read_header(int number, std::string_view content) {
        current_.reset();
        skip_section_ = true;  // until the header turns out fine
        if (content.substr(0, 2) == "[[") {
            fail(number, "lists of sections ([[...]]) are not part of a recipe");
            return;
        }
        const std::size_t close = content.find(']');
        if (close == std::string_view::npos) {
            fail(number, "the section name is missing its closing ]");
            return;
        }
        const std::string_view name = trim(content.substr(1, close - 1));
        const std::string_view after = trim(content.substr(close + 1));
        if (!after.empty() && after.front() != '#') {
            fail(number, "unexpected " + quote(after) + " after [" + std::string(name) + "]");
            return;
        }
        if (name.empty() || !std::all_of(name.begin(), name.end(), is_key_char)) {
            fail(number, "[" + std::string(name) +
                             "] is not a section name: write it without quotes, dots or spaces, "
                             "as in [exposure]");
            return;
        }
        const RecipeSectionSpec* known = nullptr;
        std::vector<std::string_view> names;
        for (const RecipeSectionSpec& spec : format_.sections) {
            names.push_back(spec.name);
            if (spec.name == name) {
                known = &spec;
            }
        }
        if (known == nullptr) {
            std::string message = "unknown section [" + std::string(name) + "]";
            if (const auto suggestion = closest(name, names)) {
                message += ". Did you mean [" + std::string(*suggestion) + "]?";
            }
            fail(number, message);
            return;
        }
        if (known->since > file_version_) {
            fail(number, "[" + std::string(name) + "]" + needs_version(known->since));
            return;
        }
        for (auto& [section, state] : sections_) {
            if (section == known->name) {
                fail(number, "[" + std::string(name) + "] appears twice (first on line " +
                                 std::to_string(state.line) + ")");
                return;
            }
        }
        current_ = sections_.size();
        sections_.push_back({known->name, SectionState{number, {}}});
        skip_section_ = false;
        start_section(known->name);
    }

    void read_setting(int number, std::string_view content, bool first) {
        std::size_t end = 0;
        while (end < content.size() && is_key_char(content[end])) {
            ++end;
        }
        const std::string_view key = content.substr(0, end);
        if (key.empty()) {
            if (content.front() == '"' || content.front() == '\'') {
                fail(number, "write the setting's name without quotes, as in stops = +1.0");
            } else {
                fail(number, quote(content) + " is not a setting: write it as key = value");
            }
            return;
        }
        std::size_t pos = end;
        while (pos < content.size() && is_space(content[pos])) {
            ++pos;
        }
        if (pos < content.size() && content[pos] == '.') {
            fail(number, "write the setting's name without dots: put it under its [section]");
            return;
        }
        if (pos >= content.size() || content[pos] != '=') {
            fail(number, quote(content) + " is not a setting: write it as key = value");
            return;
        }
        ++pos;
        while (pos < content.size() && is_space(content[pos])) {
            ++pos;
        }
        if (pos >= content.size() || content[pos] == '#') {
            fail(number, quote(key) + " has no value");
            return;
        }
        if (first && key != "prism_recipe") {
            fail(number, "a recipe starts with prism_recipe = 1, before any other setting");
        }
        if (skip_section_) {
            return;  // in a section that was already reported: its keys would only add noise
        }
        const std::string_view section = current_ ? sections_[*current_].first : "";
        const KeySpec* spec = find_spec(number, section, key);
        if (spec == nullptr) {
            return;
        }
        std::vector<std::pair<std::string_view, int>>& given =
            current_ ? sections_[*current_].second.keys : top_keys_;
        for (const auto& [name, line] : given) {
            if (name == spec->key) {
                fail(number, quote(key) + " is set twice" + in_section(section) +
                                 " (first on line " + std::to_string(line) + ")");
                return;
            }
        }
        given.emplace_back(spec->key, number);

        auto read = read_value(key, content, pos);
        if (const auto* error = std::get_if<ValueError>(&read)) {
            fail(number, error->message);
            return;
        }
        const Value& value = std::get<Value>(read);
        if (const std::optional<std::string> problem = check_type(*spec, value)) {
            fail(number, *problem);
            return;
        }
        store(number, *spec, value);
    }

    // The spec of `key` in `section`, or nullptr after reporting why there is none.
    const KeySpec* find_spec(int number, std::string_view section, std::string_view key) {
        std::vector<std::string_view> here;
        std::vector<std::string_view> elsewhere;  // sections that do have this key
        for (const KeySpec& spec : format_.keys) {
            if (spec.section == section) {
                if (spec.key == key) {
                    if (spec.since > file_version_) {
                        fail(number, quote(key) + needs_version(spec.since));
                        return nullptr;
                    }
                    return &spec;
                }
                here.push_back(spec.key);
            } else if (spec.key == key) {
                elsewhere.push_back(spec.section);
            }
        }
        if (!elsewhere.empty()) {
            if (elsewhere.front().empty()) {
                fail(number, quote(key) +
                                 " belongs at the top of the file, before the first "
                                 "[section]");
            } else if (key == "on") {
                fail(number,
                     "'on' belongs in the section of the stage it switches, such as "
                     "[exposure]");
            } else {
                std::string where;
                for (std::size_t i = 0; i < elsewhere.size(); ++i) {
                    where +=
                        (i == 0 ? "" : " or ") + std::string("[") + std::string(elsewhere[i]) + "]";
                }
                fail(number, quote(key) + " belongs in a section: put it under " + where);
            }
            return nullptr;
        }
        std::string message = "unknown setting " + quote(key) + in_section(section);
        if (const auto suggestion = closest(key, here)) {
            message += ". Did you mean " + quote(*suggestion) + "?";
        }
        fail(number, message);
        return nullptr;
    }

    void start_section(std::string_view name) {
        if (name == "exposure") {
            recipe_.exposure.emplace();
        } else if (name == "matrix") {
            recipe_.matrix.emplace();
        } else if (name == "saturation") {
            recipe_.saturation.emplace();
        } else if (name == "contrast") {
            recipe_.contrast.emplace();
        } else if (name == "curve") {
            recipe_.curve.emplace();
        }
    }

    void store(int number, const KeySpec& spec, const Value& v) {
        const std::string_view s = spec.section;
        const std::string_view k = spec.key;
        if (spec.type == ValueType::number) {
            const std::string where = "[" + std::string(s) + "] ";
            if (spec.range && !in_range(v.number, *spec.range)) {
                fail(number, where + range_message(k, v.number, *spec.range));
                return;
            }
            if (spec.positive && !is_positive(v.number)) {
                fail(number, where + positive_message(k, v.number));
                return;
            }
        }
        // Keys this code does not know (only in a test's format table) are checked as above and
        // otherwise dropped: there is nowhere to keep them.
        if (k == "on") {
            set_on(s, v.boolean);
        } else if (s.empty()) {
            if (k == "prism_recipe") {
                store_version(number, v);
            } else if (k == "name") {
                recipe_.name = v.text;
            } else if (k == "description") {
                recipe_.description = v.text;
            }
        } else if (s == "exposure" && k == "stops") {
            recipe_.exposure->stops = v.number;
        } else if (s == "matrix" && k == "from") {
            if (v.text.empty()) {
                fail(number, "'from' needs a file name, not empty text");
            } else if (v.text.find_first_of("/\\") != std::string::npos) {
                fail(number,
                     "'from' must be a file name only, without folders (it is " + v.raw + ")");
            } else {
                recipe_.matrix->from = v.text;
            }
        } else if (s == "matrix" && (k == "red" || k == "green" || k == "blue")) {
            const std::size_t row = k == "red" ? 0 : k == "green" ? 1 : 2;
            for (std::size_t i = 0; i < 3; ++i) {
                recipe_.matrix->values[row * 3 + i] = v.numbers[i];
            }
        } else if (s == "saturation" && k == "amount") {
            recipe_.saturation->amount = v.number;
        } else if (s == "contrast" && k == "amount") {
            recipe_.contrast->amount = v.number;
        } else if (s == "curve" && k == "shape") {
            store_shape(number, v);
        } else if (s == "curve" && k == "gamma") {
            recipe_.curve->gamma = v.number;
        } else if (s == "curve" && k == "white") {
            recipe_.curve->white = v.number;
        }
    }

    // " needs recipe format 2, but this file says prism_recipe = 1", for a key or section.
    std::string needs_version(int since) const {
        return " needs recipe format " + std::to_string(since) +
               ", but this file says prism_recipe = " + std::to_string(file_version_);
    }

    static std::string whole_number(float v) {
        return v < 1e9f ? std::to_string(static_cast<long>(v)) : number_text(v);
    }

    void store_version(int number, const Value& v) {
        seen_version_ = true;
        if (v.number < 1.0f) {
            fail(number, "prism_recipe must be 1 or more (it is " + v.raw + ")");
        } else if (v.number > static_cast<float>(format_.latest)) {
            // Refused as a whole: a newer format may mean anything, so nothing else is reported.
            issues_ = {{number, "this recipe needs recipe format " + whole_number(v.number) +
                                    ", but this Prism reads up to format " +
                                    std::to_string(format_.latest) + ": update Prism to read it"}};
            refused_ = true;
        } else {
            file_version_ = static_cast<int>(v.number);
        }
    }

    void store_shape(int number, const Value& v) {
        if (const std::optional<CurveShape> shape = shape_from(v.text)) {
            recipe_.curve->shape = *shape;
            return;
        }
        std::vector<std::string_view> names;
        for (const CurveShape shape : all_shapes) {
            names.push_back(shape_name(shape));
        }
        std::string message = "unknown curve shape " + v.raw;
        if (const auto suggestion = closest(v.text, names)) {
            message += ". Did you mean \"" + std::string(*suggestion) + "\"?";
        } else {
            message += ": use \"none\", \"gamma\", \"reinhard\" or \"filmic\"";
        }
        fail(number, message);
        shape_failed_ = true;
    }

    void set_on(std::string_view section, bool on) {
        if (section == "exposure") {
            recipe_.exposure->on = on;
        } else if (section == "matrix") {
            recipe_.matrix->on = on;
        } else if (section == "saturation") {
            recipe_.saturation->on = on;
        } else if (section == "contrast") {
            recipe_.contrast->on = on;
        } else if (section == "curve") {
            recipe_.curve->on = on;
        }
    }

    // Each section has its main key; a curve parameter belongs to its own shape.
    void check_sections() {
        for (const auto& [section, state] : sections_) {
            const auto line_of = [&](std::string_view key) {
                for (const auto& [name, line] : state.keys) {
                    if (name == key) {
                        return line;
                    }
                }
                return 0;
            };
            for (const KeySpec& spec : format_.keys) {
                if (spec.section == section && spec.required && spec.since <= file_version_ &&
                    line_of(spec.key) == 0 && !state.had_error) {
                    fail(state.line, "[" + std::string(section) + "] needs " + quote(spec.key));
                }
            }
            if (section == "curve" && line_of("shape") != 0 && !shape_failed_) {
                const CurveShape shape = recipe_.curve->shape;
                for (const auto& [key, wanted] :
                     {std::pair{std::string_view("gamma"), CurveShape::gamma},
                      std::pair{std::string_view("white"), CurveShape::reinhard}}) {
                    if (const int line = line_of(key); line != 0 && shape != wanted) {
                        fail(line, quote(key) + " only applies to shape = \"" +
                                       std::string(shape_name(wanted)) + "\" (this curve is \"" +
                                       std::string(shape_name(shape)) + "\")");
                    }
                }
            }
        }
    }

    const RecipeFormat& format_;
    int file_version_;  // the file's prism_recipe; until it is read, the latest
    bool refused_ = false;
    Recipe recipe_;
    std::vector<RecipeIssue> issues_;
    std::vector<std::pair<std::string_view, SectionState>> sections_;
    std::optional<std::size_t> current_;  // index into sections_; none = the top of the file
    std::vector<std::pair<std::string_view, int>> top_keys_;
    bool skip_section_ = false;
    bool seen_setting_ = false;
    bool seen_version_ = false;
    bool shape_failed_ = false;
};

}  // namespace

std::string format_recipe_issue(std::string_view source, const RecipeIssue& issue) {
    std::string text = "recipe";
    if (!source.empty()) {
        text += " '" + std::string(source) + "'";
    }
    if (issue.line > 0) {
        text += " line " + std::to_string(issue.line);
    }
    return text + ": " + issue.message;
}

namespace {

std::string format_all(std::string_view source, const std::vector<RecipeIssue>& issues) {
    std::string text;
    for (const RecipeIssue& issue : issues) {
        if (!text.empty()) {
            text += '\n';
        }
        text += format_recipe_issue(source, issue);
    }
    return text;
}

}  // namespace

RecipeError::RecipeError(std::string source, std::vector<RecipeIssue> issues)
    : std::runtime_error(format_all(source, issues)),
      source_(std::move(source)),
      issues_(std::move(issues)) {}

const RecipeFormat& recipe_format() noexcept {
    return format_v1;
}

std::string describe_values(const RecipeKeySpec& spec) {
    switch (spec.type) {
        case ValueType::version: return "whole number from 1";
        case ValueType::number: {
            if (spec.range) {
                std::ostringstream text;
                text << static_cast<float>(spec.range->min) << " to "
                     << static_cast<float>(spec.range->max);
                return text.str();
            }
            return spec.positive ? "above 0" : "any number";
        }
        case ValueType::boolean: return "true or false";
        case ValueType::text: return "one line";
        case ValueType::row: return "3 numbers";
        case ValueType::shape: {
            std::string text;
            for (const CurveShape shape : all_shapes) {
                text += (text.empty() ? "" : ", ") + std::string(shape_name(shape));
            }
            return text;
        }
    }
    return {};
}

std::string describe_default(const RecipeKeySpec& spec) {
    if (spec.required) {
        return "required";
    }
    if (spec.default_number) {
        char buffer[32];
        const std::to_chars_result r =
            std::to_chars(buffer, buffer + sizeof buffer, *spec.default_number);
        return std::string(buffer, r.ptr);
    }
    return std::string(spec.default_text);
}

Recipe parse_recipe(std::string_view text, std::string_view source, const RecipeFormat& format) {
    RecipeParser parser(format);
    Recipe recipe = parser.parse(text);
    std::vector<RecipeIssue> issues = parser.take_issues();
    if (!issues.empty()) {
        throw RecipeError(std::string(source), std::move(issues));
    }
    return recipe;
}

std::optional<std::string> curve_spec(const RecipeCurve& curve) {
    const auto shortest = [](float v) {
        char buffer[32];
        const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, v);
        return std::string(buffer, r.ptr);
    };
    switch (curve.shape) {
        case CurveShape::none: return std::nullopt;
        case CurveShape::gamma: return "gamma:" + shortest(curve.gamma);
        case CurveShape::reinhard: return "reinhard:" + shortest(curve.white);
        case CurveShape::filmic: return "filmic";
    }
    return std::nullopt;
}

ProcessSettings to_process_settings(const Recipe& recipe) {
    std::vector<std::string> problems;
    if (recipe.exposure && !std::isfinite(recipe.exposure->stops)) {
        problems.push_back("[exposure] stops must be a finite number");
    }
    if (recipe.matrix && !std::all_of(recipe.matrix->values.begin(), recipe.matrix->values.end(),
                                      [](float v) { return std::isfinite(v); })) {
        problems.push_back("[matrix] must hold finite numbers");
    }
    if (recipe.saturation && !in_range(recipe.saturation->amount, saturation_range)) {
        problems.push_back("[saturation] " +
                           range_message("amount", recipe.saturation->amount, saturation_range));
    }
    if (recipe.contrast && !in_range(recipe.contrast->amount, contrast_range)) {
        problems.push_back("[contrast] " +
                           range_message("amount", recipe.contrast->amount, contrast_range));
    }
    if (const auto& c = recipe.curve) {
        if (c->shape == CurveShape::gamma && !is_positive(c->gamma)) {
            problems.push_back("[curve] " + positive_message("gamma", c->gamma));
        }
        if (c->shape == CurveShape::reinhard && !is_positive(c->white)) {
            problems.push_back("[curve] " + positive_message("white", c->white));
        }
    }
    if (!problems.empty()) {
        std::string message = "invalid recipe:";
        for (const std::string& p : problems) {
            message += " " + p + ".";
        }
        throw std::invalid_argument(message);
    }

    ProcessSettings p;
    if (recipe.exposure && recipe.exposure->on) {
        p.exposure_stops = recipe.exposure->stops;
    }
    if (recipe.matrix && recipe.matrix->on) {
        p.matrix = recipe.matrix->values;
    }
    if (recipe.saturation && recipe.saturation->on) {
        p.saturation = recipe.saturation->amount;
    }
    if (recipe.contrast && recipe.contrast->on) {
        p.contrast = recipe.contrast->amount;
    }
    if (recipe.curve && recipe.curve->on) {
        if (const std::optional<std::string> spec = curve_spec(*recipe.curve)) {
            p.curve = parse_curve(*spec);
        }
    }
    return p;
}

ProcessSettings merge_recipe_and_flags(const Recipe& recipe, const ProcessSettings& flags) {
    ProcessSettings p = to_process_settings(recipe);
    if (flags.exposure_stops) {
        p.exposure_stops = flags.exposure_stops;
    }
    if (flags.matrix) {
        p.matrix = flags.matrix;
    }
    if (flags.saturation) {
        p.saturation = flags.saturation;
    }
    if (flags.contrast) {
        p.contrast = flags.contrast;
    }
    if (flags.curve) {
        p.curve = flags.curve;
    }
    return p;
}

Recipe load_recipe(const std::filesystem::path& path) {
    const std::string source = utf8(path);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        throw RecipeError(source, {{0, "this is a folder, not a recipe file"}});
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        const bool missing = !std::filesystem::exists(path, ec);
        throw RecipeError(source,
                          {{0, missing ? "the file does not exist" : "the file cannot be opened"}});
    }
    // One byte more than a recipe may have is enough to tell that a file is too large, without
    // reading all of a large file that was opened by mistake.
    std::string text(recipe_max_bytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad()) {
        throw RecipeError(source, {{0, "the file cannot be read"}});
    }
    text.resize(static_cast<std::size_t>(in.gcount()));
    return parse_recipe(text, source);
}

void save_recipe(const std::filesystem::path& path, const Recipe& recipe,
                 const RecipeWriteOptions& options) {
    const std::optional<AtomicWriteError> error =
        write_file_atomically(path, write_recipe(recipe, options));
    if (!error) {
        return;
    }
    std::string message = "cannot save recipe '" + utf8(path) + "': ";
    if (error->step == AtomicWriteError::Step::write) {
        message += "cannot write '" + utf8(error->file) + "'";
    } else {
        message += error->reason;
    }
    throw std::runtime_error(message);
}

}  // namespace prism
