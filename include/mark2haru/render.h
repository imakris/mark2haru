#pragma once

#include <mark2haru/font_context.h>
#include <mark2haru/markdown.h>

#include <filesystem>
#include <string>

namespace mark2haru {

struct render_options_t {
    double page_width_pt = 595.276;
    double page_height_pt = 841.89;
    double margin_left_pt = 56.7;
    double margin_right_pt = 56.7;
    double margin_top_pt = 56.7;
    double margin_bottom_pt = 56.7;
    double body_size_pt = 10.5;
    double line_spacing = 1.35;
    std::filesystem::path font_root_dir;
    font_family_config_t font_family;
};

bool render_markdown_to_pdf(
    const std::string& markdown,
    const std::filesystem::path& output_path,
    const render_options_t& options = {});

} // namespace mark2haru
