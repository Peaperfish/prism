#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/ranges.hpp"

namespace prism {

// A recipe: an edit written down as plain text (a .prism file), so it can be read, changed by
// hand, shared and diffed. docs/RECIPES.md describes the format for people; this header is the
// same thing for code.
//
// A recipe holds one optional section per edit stage. A stage that is present is set completely
// when the recipe is applied; a stage that is absent stays as it was (the partial rule). Each
// stage has an on/off switch, as in the GUI: a stage that is off keeps its values but is skipped.

// The recipe format version this Prism reads up to and writes. recipe_format() lists what each
// version has.
inline constexpr int recipe_format_version = 1;

struct RecipeExposure {
    float stops = 0.0f;  // any finite number
    bool on = true;
    bool operator==(const RecipeExposure&) const = default;
};

struct RecipeMatrix {
    // Row-major, as Matrix3: row i is output channel i (red, green, blue) as weights of the input
    // R, G and B.
    Matrix3 values = identity_matrix;
    // The file the numbers came from: a file name without folders, kept only as a label. Prism
    // never opens it.
    std::optional<std::string> from;
    bool on = true;
    bool operator==(const RecipeMatrix&) const = default;
};

struct RecipeSaturation {
    float amount = 1.0f;  // saturation_range, the same number as --saturation
    bool on = true;
    bool operator==(const RecipeSaturation&) const = default;
};

struct RecipeContrast {
    float amount = 1.0f;  // contrast_range, the same number as --contrast
    bool on = true;
    bool operator==(const RecipeContrast&) const = default;
};

enum class CurveShape { none, gamma, reinhard, filmic };

struct RecipeCurve {
    CurveShape shape = CurveShape::none;
    // Each is used, and written, only for its own shape; the other keeps its default.
    float gamma = gamma_default;           // above 0
    float white = reinhard_white_default;  // above 0
    bool on = true;
    bool operator==(const RecipeCurve&) const = default;
};

struct Recipe {
    std::optional<std::string> name;         // one line of UTF-8 text
    std::optional<std::string> description;  // one line of UTF-8 text

    // In pipeline order. Empty = not in the recipe: that stage is left as it is.
    std::optional<RecipeExposure> exposure;
    std::optional<RecipeMatrix> matrix;
    std::optional<RecipeSaturation> saturation;
    std::optional<RecipeContrast> contrast;
    std::optional<RecipeCurve> curve;

    // Floats compare by value (so -0 equals +0).
    bool operator==(const Recipe&) const = default;
};

struct RecipeWriteOptions {
    // A fixed two-line header (what the file is, and where the format is described) and one short
    // comment on each value's line saying what the number does, worked out with the engine's own
    // maths: "1.41x the light", "18% grey becomes 15%". Comments are ignored when reading and
    // written afresh on every save. Off only for tests and tools that want the bare values.
    bool comments = true;
};

// The recipe as text, in the format of docs/RECIPES.md: `prism_recipe = 1`, the name and
// description if there are any, then each section the recipe has, in pipeline order with a fixed
// key order. Numbers are written in the shortest form that reads back as exactly the same float,
// with ".0" added to whole numbers; exposure always has a sign, and -0 is written as 0. `on =
// false` is written only for a stage that is off. No aligned = signs, and nothing that changes
// from one save to the next (no date, no Prism version), so saved recipes diff cleanly. UTF-8
// without a byte-order mark, LF line ends.
[[nodiscard]] std::string write_recipe(const Recipe& recipe,
                                       const RecipeWriteOptions& options = {});

// --- Reading -------------------------------------------------------------------------------------
//
// Reading is strict, so that a typing mistake is reported instead of quietly ignored: an unknown
// key or section (with a suggestion when one is close), a key given twice, a value of the wrong
// kind or out of its range (refused, never clamped), units, decimal commas and the like are all
// errors. Up to recipe_max_errors are collected, each with its line, and if there is any error the
// whole recipe is refused: nothing of it is applied.

inline constexpr std::size_t recipe_max_errors = 10;
inline constexpr std::size_t recipe_max_bytes = 64 * 1024;

// One mistake in a recipe: the line it is on (1 = the first; 0 = the file as a whole) and what is
// wrong, in plain words, e.g. "unknown setting 'amout' in [saturation]. Did you mean 'amount'?"
struct RecipeIssue {
    int line = 0;
    std::string message;
    bool operator==(const RecipeIssue&) const = default;
};

// "recipe 'warm.prism' line 4: <message>", or "recipe 'warm.prism': <message>" for line 0. With
// an empty `source`, "recipe line 4: <message>".
[[nodiscard]] std::string format_recipe_issue(std::string_view source, const RecipeIssue& issue);

// Why a recipe was refused. what() is every issue formatted as above, one per line.
class RecipeError : public std::runtime_error {
public:
    RecipeError(std::string source, std::vector<RecipeIssue> issues);

    // The file name (or other source) the messages name.
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    // In line order; never empty.
    [[nodiscard]] const std::vector<RecipeIssue>& issues() const noexcept { return issues_; }

private:
    std::string source_;
    std::vector<RecipeIssue> issues_;
};

// --- Format versions -----------------------------------------------------------------------------
//
// Every section and key, with the format version that introduced it, as data: the parser reads
// this table, docs/RECIPES.md's key table is checked against it, and a test can hand the parser a
// table with a version 2 in it.
//
// A file says which version it is written in (prism_recipe = N). A file newer than the table's
// latest version is refused as a whole, without guessing at it; a key or section introduced after
// the file's own version is an error naming the version it needs. A key that is required (a
// section's main key) is only required in files of its version or later.

enum class RecipeValueType {
    version,  // a whole number: prism_recipe
    number,   // one finite number
    boolean,  // true or false
    text,     // one line of text in double quotes
    row,      // a list of 3 numbers: a matrix row
    shape,    // the name of a tone curve, in double quotes
};

struct RecipeSectionSpec {
    std::string_view name;
    int since = 1;  // the format version that introduced it
};

struct RecipeKeySpec {
    std::string_view section;  // "" = the top of the file, before any [section]
    std::string_view key;
    RecipeValueType type = RecipeValueType::number;
    int since = 1;
    bool required = false;  // a section's main key (and, at the top, prism_recipe)
    // For numbers: the range (none = any finite number), or only "above 0".
    std::optional<ValueRange> range;
    bool positive = false;
    // For people (docs/RECIPES.md): the unit, and the default when the key is left out: a number,
    // or else a word ("true", "none"; required keys have none).
    std::string_view unit;
    std::optional<float> default_number;
    std::string_view default_text;
};

struct RecipeFormat {
    int latest = recipe_format_version;
    std::span<const RecipeSectionSpec> sections;
    std::span<const RecipeKeySpec> keys;
};

// This Prism's format: every section and key up to recipe_format_version.
[[nodiscard]] const RecipeFormat& recipe_format() noexcept;

// What a key's row in docs/RECIPES.md says, from the table: its values ("0 to 10", "above 0",
// "3 numbers", ...) and its default ("required", "2.2", "true", ...).
[[nodiscard]] std::string describe_values(const RecipeKeySpec& spec);
[[nodiscard]] std::string describe_default(const RecipeKeySpec& spec);

// Reads a recipe from its text. `source` names it in messages (usually the file's name). A
// byte-order mark at the start and CRLF line ends are accepted. `format` is what this Prism
// reads; only tests pass another. Throws RecipeError.
[[nodiscard]] Recipe parse_recipe(std::string_view text, std::string_view source = {},
                                  const RecipeFormat& format = recipe_format());

// --- Running a recipe ----------------------------------------------------------------------------

// The curve's spec for prism::parse_curve and the CLI's --curve ("gamma:2.2", "reinhard:4",
// "filmic"), or nothing for shape none. Numbers in their shortest exact form.
[[nodiscard]] std::optional<std::string> curve_spec(const RecipeCurve& curve);

// What prism::process should do for this recipe on its own: each stage that is present and on,
// and nothing for the others (a stage left out of a recipe does nothing here, as with a CLI flag
// that is not given). The curve is prism::parse_curve(curve_spec(...)), exactly the curve --curve
// builds from the same numbers. Throws std::invalid_argument for values outside their ranges,
// which parse_recipe never returns.
[[nodiscard]] ProcessSettings to_process_settings(const Recipe& recipe);

// What the CLI runs for a recipe together with the flags given on the command line. `flags` holds
// one field per stage flag that was given (--exposure, --matrix, --saturation, --contrast,
// --curve) and nothing for the others. Flags win stage by stage: a flag replaces its whole stage
// (the curve's shape and parameter together) and runs it even where the recipe switches that
// stage off. Every other stage is as in to_process_settings(recipe): run if present and on, and
// otherwise nothing, since the command line starts from no adjustments. With an empty recipe the
// result is `flags` unchanged. Throws as to_process_settings does.
[[nodiscard]] ProcessSettings merge_recipe_and_flags(const Recipe& recipe,
                                                     const ProcessSettings& flags);

// --- Files ---------------------------------------------------------------------------------------

// Reads the recipe file at `path`. Throws RecipeError, whose messages name the file as given
// ("recipe 'looks/warm.prism' line 4: ..."), also when it is missing or cannot be read.
[[nodiscard]] Recipe load_recipe(const std::filesystem::path& path);

// Writes write_recipe(recipe) to `path`, atomically (prism/atomic_file.hpp): a crash or a full
// disk never leaves half a recipe behind, and an existing file is replaced in one step. Throws
// std::runtime_error naming the file if it cannot be written.
void save_recipe(const std::filesystem::path& path, const Recipe& recipe,
                 const RecipeWriteOptions& options = {});

}  // namespace prism
