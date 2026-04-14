#pragma once

#include "markdown.h"

#include <filesystem>
#include <string>

namespace mark2haru {

struct RenderOptions {
    double page_width_pt = 595.276;
    double page_height_pt = 841.89;
    double margin_left_pt = 56.7;
    double margin_right_pt = 56.7;
    double margin_top_pt = 56.7;
    double margin_bottom_pt = 56.7;
    double body_size_pt = 10.5;
    double line_spacing = 1.35;
    std::filesystem::path font_root_dir;
};

struct RenderResult {
    bool ok = false;
    std::string error;
};

RenderResult render_markdown_to_pdf(const std::string& markdown,
                                    const std::string& output_path,
                                    const RenderOptions& options = {});

} // namespace mark2haru
