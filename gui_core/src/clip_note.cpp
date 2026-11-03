#include "prism/gui/clip_note.hpp"

#include <vector>

namespace prism::gui {

namespace {

struct Entry {
    ClipNoteKind kind;
    std::string text;
};

}  // namespace

ClipNote make_clip_note(const ClipReport& report) {
    ClipNote note;
    note.full = describe_clipping(report);
    const std::string gamut_sentence = describe_gamut(report);
    if (!gamut_sentence.empty()) {
        note.full += " " + gamut_sentence;
    }

    const std::uint64_t total = report.sample_count;
    std::vector<Entry> found;
    const auto stage_name = [](StageId id) { return std::string(stage_info(id).name); };

    // Highlights that were already at the file's ceiling: Decode loses them, so no edit can help.
    const StageClip& decode = report.at(StageId::decode);
    if (decode.over_fate == ClipFate::lost && decode.over.any > 0) {
        found.push_back({ClipNoteKind::highlights,
                         format_clip_percent(decode.over.any, total) + " clipped in the file"});
    }
    // What the edits lose: the first stage that cuts each kind off, with what it received.
    // (After Decode, so a photo clipped in the file and pushed further by an edit shows both.)
    for (std::size_t i = 1; i < report.stages.size(); ++i) {
        const StageClip& clip = report.stages[i];
        if (clip.over_fate == ClipFate::lost && clip.over.any > 0) {
            found.push_back({ClipNoteKind::highlights,
                             format_clip_percent(clip.over.any, total) + " highlights clipped at " +
                                 stage_name(clip.stage)});
            break;
        }
    }
    if (report.first_under_lost) {
        const StageClip& clip = report.at(*report.first_under_lost);
        if (clip.under.any > 0) {
            found.push_back({ClipNoteKind::shadows,
                             format_clip_percent(clip.under.any, total) + " shadows clipped at " +
                                 stage_name(clip.stage)});
        }
    }
    if (report.first_gamut_lost) {
        const StageClip& clip = report.at(*report.first_gamut_lost);
        if (clip.gamut.any > 0) {
            found.push_back({ClipNoteKind::gamut,
                             format_clip_percent(clip.gamut.any, total) +
                                 " too saturated, clipped at " + stage_name(clip.stage)});
        }
    }

    if (found.empty()) {
        return note;  // "No clipping"
    }
    note.kind = found.front().kind;
    note.text = found.front().text;
    if (found.size() > 1) {
        note.text += "  +" + std::to_string(found.size() - 1) + " more";
    }
    return note;
}

}  // namespace prism::gui
