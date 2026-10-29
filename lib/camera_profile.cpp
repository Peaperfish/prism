#include "prism/camera_profile.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace prism {

// --- 3x3 maths -----------------------------------------------------------------------------------

Mat3 multiply(const Mat3& a, const Mat3& b) noexcept {
    // (a * b)[i][j] is row i of a dotted with column j of b.
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
    return out;
}

Vec3 multiply(const Mat3& m, const Vec3& v) noexcept {
    Vec3 out{};
    for (int i = 0; i < 3; ++i) {
        out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    }
    return out;
}

Mat3 invert(const Mat3& m) {
    // Cofactor c[i][j] = (-1)^(i+j) * the determinant of m without row i and column j. Writing it
    // with indices (i+1)%3 and (i+2)%3 gets the sign right without a separate (-1)^(i+j).
    Mat3 c{};
    for (int i = 0; i < 3; ++i) {
        const int i1 = (i + 1) % 3;
        const int i2 = (i + 2) % 3;
        for (int j = 0; j < 3; ++j) {
            const int j1 = (j + 1) % 3;
            const int j2 = (j + 2) % 3;
            c[i][j] = m[i1][j1] * m[i2][j2] - m[i1][j2] * m[i2][j1];
        }
    }
    // Expanding along the first row: det = sum of m[0][j] * c[0][j].
    const double det = m[0][0] * c[0][0] + m[0][1] * c[0][1] + m[0][2] * c[0][2];

    // "Singular" has to be relative to the entries' size: scaling m by 1000 scales det by 1e9
    // without making m any closer to singular. Comparing against the largest entry cubed makes
    // the check independent of scale.
    double largest = 0;
    for (const auto& row : m) {
        for (double v : row) {
            if (!std::isfinite(v)) {
                throw std::domain_error("invert: matrix has a non-finite entry");
            }
            largest = std::max(largest, std::abs(v));
        }
    }
    if (!(std::abs(det) > 1e-12 * largest * largest * largest)) {
        throw std::domain_error("invert: matrix is singular (determinant " + std::to_string(det) +
                                ")");
    }
    // inverse = adjugate / det, where the adjugate is the transposed cofactor matrix.
    Mat3 inv{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            inv[i][j] = c[j][i] / det;
        }
    }
    return inv;
}

Mat3 normalize_rows(const Mat3& m) {
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        const double sum = m[i][0] + m[i][1] + m[i][2];
        if (!(std::abs(sum) > 1e-9)) {
            throw std::domain_error("normalize_rows: row " + std::to_string(i) +
                                    " sums to zero, so white cannot map to white");
        }
        for (int j = 0; j < 3; ++j) {
            out[i][j] = m[i][j] / sum;
        }
    }
    return out;
}

Matrix3 to_matrix3(const Mat3& m) noexcept {
    Matrix3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[static_cast<std::size_t>(i * 3 + j)] = static_cast<float>(m[i][j]);
        }
    }
    return out;
}

// --- Colour spaces -------------------------------------------------------------------------------

Vec3 xyz_of(Chromaticity c) noexcept {
    // x = X / (X + Y + Z) and y = Y / (X + Y + Z); with Y = 1 that gives X = x / y, Z = z / y.
    return {c.x / c.y, 1.0, (1.0 - c.x - c.y) / c.y};
}

Mat3 rgb_to_xyz(Chromaticity red, Chromaticity green, Chromaticity blue, Chromaticity white) {
    // Pure red, green and blue have the primaries' chromaticities but unknown brightness. Put
    // each primary's XYZ (at Y = 1) in a column, then find the per-column scale s that makes
    // RGB (1, 1, 1) land on the white point: P * s = white, so s = P^-1 * white.
    const Vec3 r = xyz_of(red);
    const Vec3 g = xyz_of(green);
    const Vec3 b = xyz_of(blue);
    const Mat3 p = {{{r[0], g[0], b[0]}, {r[1], g[1], b[1]}, {r[2], g[2], b[2]}}};
    const Vec3 s = multiply(invert(p), xyz_of(white));
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = p[i][j] * s[j];
        }
    }
    return out;
}

Mat3 srgb_to_xyz() {
    return rgb_to_xyz({0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}, white_d65);
}

Mat3 xyz_to_srgb() {
    return invert(srgb_to_xyz());
}

Mat3 rec2020_to_xyz() {
    return rgb_to_xyz({0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, white_d65);
}

Mat3 bradford(Chromaticity from, Chromaticity to) {
    // Chromatic adaptation models how the eye re-balances to a new light: convert XYZ to an
    // approximation of the cones' responses (Bradford's matrix), scale each response by
    // (new white / old white), and convert back. Bradford's sharpened cone space adapts more
    // accurately than scaling XYZ directly ("XYZ scaling").
    constexpr Mat3 cone = {{{0.8951, 0.2664, -0.1614},  //
                            {-0.7502, 1.7135, 0.0367},
                            {0.0389, -0.0685, 1.0296}}};
    const Vec3 src = multiply(cone, xyz_of(from));
    const Vec3 dst = multiply(cone, xyz_of(to));
    Mat3 scale{};
    for (int i = 0; i < 3; ++i) {
        scale[i][i] = dst[i] / src[i];
    }
    return multiply(invert(cone), multiply(scale, cone));
}

// --- Camera -> linear sRGB -----------------------------------------------------------------------

Mat3 camera_to_srgb_from_forward_matrix(const Mat3& forward) {
    // Right to left: camera -> XYZ (under D50, where DNG defines it) -> XYZ adapted to D65 ->
    // linear sRGB. The adaptation step is needed because sRGB's white is D65: without it, camera
    // white would arrive at D50's XYZ and come out of xyz_to_srgb a little yellow.
    return normalize_rows(multiply(xyz_to_srgb(), multiply(bradford(white_d50, white_d65), forward)));
}

Mat3 camera_to_srgb_from_color_matrix(const Mat3& color) {
    // ColorMatrix maps XYZ -> camera RGB *before* white balance, so invert(color) maps raw camera
    // RGB -> XYZ. But Prism's camera RGB is white-balanced: each channel was divided by the
    // camera's response to the light, `neutral` = color * white. To undo that, multiply each
    // camera channel by its neutral again *before* invert(color), i.e. on the right, as
    // diag(neutral). That scales the matrix's columns (one per input channel).
    //
    // Normalising rows instead would scale *output* channels, which is a different (and wrong)
    // correction: it would make white come out white, but tint every other colour.
    const Mat3 to_xyz = invert(color);
    const Vec3 neutral = multiply(color, xyz_of(white_d65));
    Mat3 balance{};
    for (int i = 0; i < 3; ++i) {
        if (!(neutral[i] > 0)) {
            throw std::domain_error("ColorMatrix gives a non-positive camera response to D65 white");
        }
        balance[i][i] = neutral[i];
    }
    // With the neutral folded in, camera (1, 1, 1) -> D65 white -> sRGB (1, 1, 1) already; the
    // normalisation only removes rounding error.
    return normalize_rows(multiply(xyz_to_srgb(), multiply(to_xyz, balance)));
}

Mat3 camera_to_srgb_from_rec2020(const Mat3& m) {
    // Camera -> Rec. 2020 -> XYZ -> sRGB. Both RGB spaces use D65 white, so no adaptation.
    return normalize_rows(multiply(xyz_to_srgb(), multiply(rec2020_to_xyz(), m)));
}

std::optional<double> illuminant_kelvin(int code) noexcept {
    // EXIF / DNG LightSource values, with the temperatures DNG readers commonly use for them.
    switch (code) {
        case 1: return 5500;   // daylight
        case 2: return 4200;   // fluorescent
        case 3: return 2850;   // tungsten
        case 4: return 5500;   // flash
        case 9: return 5500;   // fine weather
        case 10: return 6500;  // cloudy
        case 11: return 7500;  // shade
        case 12: return 6430;  // daylight fluorescent
        case 13: return 5000;  // day white fluorescent
        case 14: return 4150;  // cool white fluorescent
        case 15: return 3525;  // white fluorescent
        case 16: return 2925;  // warm white fluorescent
        case 17: return 2856;  // standard light A
        case 18: return 4874;  // standard light B
        case 19: return 6774;  // standard light C
        case 20: return 5503;  // D55
        case 21: return 6504;  // D65
        case 22: return 7504;  // D75
        case 23: return 5003;  // D50
        case 24: return 3200;  // ISO studio tungsten
        default: return std::nullopt;
    }
}

Mat3 camera_to_srgb_from_dng(const std::array<DngCalibration, 2>& calibrations) {
    const DngCalibration* best = nullptr;
    double best_distance = 0;
    for (const DngCalibration& c : calibrations) {
        if (!c.color_matrix && !c.forward_matrix) {
            continue;
        }
        // Mireds rather than kelvin: 2856 K -> 6504 K is a big step, 6504 K -> 10000 K a small one.
        // An unknown illuminant ranks last but is still used if it is the only one.
        const std::optional<double> k = illuminant_kelvin(c.illuminant);
        const double distance = k ? std::abs(1e6 / *k - 1e6 / 6504.0) : 1e9;
        if (best == nullptr || distance < best_distance) {
            best = &c;
            best_distance = distance;
        }
    }
    if (best == nullptr) {
        throw std::invalid_argument("DNG profile has no ColorMatrix or ForwardMatrix");
    }
    return best->forward_matrix ? camera_to_srgb_from_forward_matrix(*best->forward_matrix)
                                : camera_to_srgb_from_color_matrix(*best->color_matrix);
}

// --- A small JSON reader -------------------------------------------------------------------------
//
// Just enough JSON (RFC 8259) for profile files: objects, arrays, numbers, strings, true, false and
// null. Everything is parsed into a tree, then the profile fields are picked out of it.

namespace {

struct Json {
    enum class Type { null, boolean, number, string, array, object };
    Type type = Type::null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    // An object's members, as two parallel lists. (Not a vector of pair<string, Json>: a struct
    // may hold a std::vector of itself, but std::pair needs Json complete, and it isn't yet here.)
    std::vector<std::string> keys;
    std::vector<Json> values;

    const Json* find(std::string_view key) const {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) {
                return &values[i];
            }
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    Json parse_document() {
        Json value = parse_value(0);
        skip_space();
        if (pos_ != text_.size()) {
            fail("unexpected text after the JSON value");
        }
        return value;
    }

private:
    // Deeply nested input would otherwise recurse until the stack overflows.
    static constexpr int max_depth = 64;

    [[noreturn]] void fail(const std::string& why) const {
        throw std::runtime_error("JSON error at byte " + std::to_string(pos_) + ": " + why);
    }

    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) {
            fail(std::string("expected '") + c + "'");
        }
    }

    bool consume_word(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Json parse_value(int depth) {
        if (depth > max_depth) {
            fail("nested too deeply");
        }
        skip_space();
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
        }
        Json v;
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            v.type = Json::Type::object;
            if (!consume('}')) {
                do {
                    skip_space();
                    v.keys.push_back(parse_string());
                    expect(':');
                    v.values.push_back(parse_value(depth + 1));
                } while (consume(','));
                expect('}');
            }
        } else if (c == '[') {
            ++pos_;
            v.type = Json::Type::array;
            if (!consume(']')) {
                do {
                    v.array.push_back(parse_value(depth + 1));
                } while (consume(','));
                expect(']');
            }
        } else if (c == '"') {
            v.type = Json::Type::string;
            v.string = parse_string();
        } else if (consume_word("true")) {
            v.type = Json::Type::boolean;
            v.boolean = true;
        } else if (consume_word("false")) {
            v.type = Json::Type::boolean;
        } else if (consume_word("null")) {
            v.type = Json::Type::null;
        } else {
            v.type = Json::Type::number;
            v.number = parse_number();
        }
        return v;
    }

    double parse_number() {
        // Take the characters a JSON number can contain, then let strtod convert them and check
        // that it used all of them.
        const std::size_t start = pos_;
        while (pos_ < text_.size() &&
               std::string_view("+-0123456789.eE").find(text_[pos_]) != std::string_view::npos) {
            ++pos_;
        }
        const std::string token(text_.substr(start, pos_ - start));
        if (token.empty()) {
            fail("expected a value");
        }
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || errno == ERANGE || !std::isfinite(value)) {
            fail("'" + token + "' is not a valid number");
        }
        return value;
    }

    unsigned parse_hex4() {
        if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
        }
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            value <<= 4;
            if (h >= '0' && h <= '9') {
                value |= static_cast<unsigned>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                value |= static_cast<unsigned>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                value |= static_cast<unsigned>(h - 'A' + 10);
            } else {
                fail("bad \\u escape");
            }
        }
        return value;
    }

    static void append_utf8(std::string& out, unsigned cp) {
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

    std::string parse_string() {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            fail("expected a string");
        }
        ++pos_;
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) {
                fail("unterminated string");
            }
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                fail("unterminated string");
            }
            switch (const char e = text_[pos_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = parse_hex4();
                    // Characters beyond U+FFFF are written as a surrogate pair: \uD8xx\uDCxx.
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        if (!consume_word("\\u")) {
                            fail("unpaired surrogate in \\u escape");
                        }
                        const unsigned low = parse_hex4();
                        if (low < 0xDC00 || low >= 0xE000) {
                            fail("unpaired surrogate in \\u escape");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp < 0xE000) {
                        fail("unpaired surrogate in \\u escape");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail(std::string("bad escape '\\") + e + "'");
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

[[noreturn]] void profile_error(const std::string& why) {
    throw std::runtime_error("LightCraft profile: " + why);
}

const Json& require(const Json& object, std::string_view key, Json::Type type) {
    const Json* v = object.find(key);
    if (v == nullptr || v->type != type) {
        profile_error("missing or wrong type: \"" + std::string(key) + "\"");
    }
    return *v;
}

long long require_count(const Json& object, std::string_view key) {
    const double n = require(object, key, Json::Type::number).number;
    if (n < 0 || n > 1e15 || n != std::floor(n)) {
        profile_error("\"" + std::string(key) + "\" must be a non-negative whole number");
    }
    return static_cast<long long>(n);
}

}  // namespace

LightCraftProfile parse_lightcraft_profile(std::string_view json) {
    const Json root = JsonParser(json).parse_document();
    if (root.type != Json::Type::object) {
        profile_error("top level is not a JSON object");
    }
    LightCraftProfile p;
    const double version = require(root, "version", Json::Type::number).number;
    if (version != 1) {
        profile_error("unsupported version " + std::to_string(version) + " (expected 1)");
    }
    p.version = 1;
    p.model = require(root, "model", Json::Type::string).string;
    if (p.model.empty()) {
        profile_error("empty \"model\"");
    }
    p.files = require_count(root, "files");
    p.samples = require_count(root, "samples");

    const Json& rows = require(root, "matrix", Json::Type::array);
    if (rows.array.size() != 3) {
        profile_error("\"matrix\" must have 3 rows");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const Json& row = rows.array[i];
        if (row.type != Json::Type::array || row.array.size() != 3) {
            profile_error("\"matrix\" row " + std::to_string(i) + " must have 3 numbers");
        }
        for (std::size_t j = 0; j < 3; ++j) {
            if (row.array[j].type != Json::Type::number) {
                profile_error("\"matrix\" entries must be numbers");
            }
            p.matrix[i][j] = row.array[j].number;
        }
    }

    // Optional; null (or missing) means the profile has no table.
    if (const Json* table = root.find("hue_sat"); table != nullptr && table->type != Json::Type::null) {
        if (table->type != Json::Type::object) {
            profile_error("\"hue_sat\" must be an object or null");
        }
        p.has_hue_sat = true;
        p.hue_divisions = static_cast<int>(require_count(*table, "hue_divisions"));
        p.sat_divisions = static_cast<int>(require_count(*table, "sat_divisions"));
        p.val_divisions = static_cast<int>(require_count(*table, "val_divisions"));
    }
    return p;
}

LightCraftProfile load_lightcraft_profile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open profile '" + path + "'");
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        throw std::runtime_error("cannot read profile '" + path + "'");
    }
    try {
        return parse_lightcraft_profile(text);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

}  // namespace prism
