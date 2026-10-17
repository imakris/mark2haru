#include <mark2haru/render.h>

#include <mark2haru/font_context.h>
#include <mark2haru/markdown.h>
#include <mark2haru/pdf_writer.h>
#include <mark2haru/png_image.h>
#include <mark2haru/table_layout.h>

#include "io_helpers.h"
#include <mark2haru/text_layout.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

using text_layout::font_for;
using text_layout::Line;
using text_layout::Token;
using text_layout::total_height;
using text_layout::wrap_tokens;
using text_layout::wrap_runs;

// Standard "overloaded" idiom to combine a set of single-type lambdas
// into one callable that std::visit can dispatch on. Adding a new block
// alternative without a matching lambda fails to compile, which is the
// whole point of using std::visit here.
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct heading_metrics_t
{
    double size_factor;
    double space_before_factor;
    double space_after_factor;
};

constexpr heading_metrics_t k_heading_metrics_table[] = {
    { 1.00, 0.45, 0.35 }, // index 0 unused: level 0 / level >=7 fallback
    { 1.65, 0.95, 0.65 }, // h1
    { 1.35, 0.75, 0.55 }, // h2
    { 1.18, 0.60, 0.45 }, // h3
    { 1.08, 0.50, 0.40 }, // h4
    { 1.00, 0.45, 0.35 }, // h5
    { 0.92, 0.45, 0.35 }, // h6
};

const heading_metrics_t& heading_metrics(int level)
{
    if (level < 1 || level > 6) {
        return k_heading_metrics_table[0];
    }
    return k_heading_metrics_table[level];
}

std::string list_marker(const List_block& lb, size_t index)
{
    if (!lb.ordered) {
        // U+2022 BULLET
        return "\xe2\x80\xa2";
    }
    // Compute the marker number in 64-bit signed arithmetic so a very
    // large start_number plus a very large index doesn't overflow int.
    const std::int64_t value =
        static_cast<std::int64_t>(lb.start_number) +
        static_cast<std::int64_t>(index);
    return std::to_string(value) + ".";
}

template <class MeasureFn>
std::vector<Line> code_lines(
    const std::string& text,
    double             max_width_pt,
    double             size_pt,
    double             line_height_pt,
    MeasureFn&&        measure)
{
    std::vector<Line> lines;
    for (const auto& raw : split_lines(text)) {
        std::vector<Token> tokens;
        tokens.push_back({ raw, Inline_style::CODE, false });
        auto wrapped = wrap_tokens(tokens, max_width_pt, size_pt, line_height_pt, measure);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (lines.empty()) {
        lines.push_back({ {}, line_height_pt });
    }
    return lines;
}

} // namespace

bool render_markdown_to_pdf(
    const std::string&     markdown,
    const fs::path&        output_path,
    const Render_options&  options,
    std::string&           error)
{
    const auto blocks  = parse_markdown(markdown);
    auto       metrics = std::make_shared<Measurement_context>(options.font_family, options.font_root_dir);
    if (!metrics->loaded()) {
        error = metrics->error().empty()
            ? std::string("Failed to load fonts")
            : metrics->error();
        return false;
    }

    Pdf_writer writer(options.page_width_pt, options.page_height_pt, metrics);
    if (!writer.fonts_loaded()) {
        error = writer.font_error().empty()
            ? std::string("PDF writer reports fonts not loaded")
            : writer.font_error();
        return false;
    }

    constexpr color_t default_stroke    = { 0.2, 0.2, 0.2 };
    constexpr color_t code_block_fill   = { 0.96, 0.96, 0.96 };
    constexpr color_t table_header_fill = { 0.92, 0.92, 0.92 };
    constexpr double  border_line_width = 0.75;

    const double content_width = options.page_width_pt - options.margin_left_pt - options.margin_right_pt;
    const double content_bottom = options.page_height_pt - options.margin_bottom_pt;
    const double content_height = content_bottom - options.margin_top_pt;
    double       cursor_y      = options.margin_top_pt;
    bool         render_ok     = true;

    auto new_page = [&]() {
        writer.begin_page();
        cursor_y = options.margin_top_pt;
    };

    auto ensure_space = [&](double height_pt) {
        if (cursor_y + height_pt > content_bottom) {
            new_page();
        }
    };

    auto measure = [&](Pdf_font font, const std::string& text, double size_pt) {
        return metrics->measure_text_width(font, text, size_pt);
    };

    auto draw_line_at = [&](const Line& line, double size, double x, double y)
    {
        for (const auto& span : line.spans) {
            writer.draw_text(
                x + span.x_offset_pt, y, size, font_for(span.style), span.text);
        }
    };

    // Keep ordinary blocks together, but reserve only the first line for a
    // block that must span pages. Never move an oversized block to an empty
    // page and then draw the rest below that page's bottom margin.
    auto prepare_lines = [&](const std::vector<Line>& lines, double before, double after) {
        const double first_height = before + (lines.empty() ? 0.0 : lines.front().height_pt);
        if (first_height > content_height) {
            error = "Page content area is too short for a line of text";
            render_ok = false;
            return false;
        }
        const double height = before + total_height(lines) + after;
        ensure_space(height <= content_height ? height : first_height);
        cursor_y += before;
        return true;
    };

    auto draw_text_lines = [&](const std::vector<Line>& lines, double size, double x) {
        for (const auto& line : lines) {
            if (line.height_pt > content_height) {
                error = "Page content area is too short for a line of text";
                render_ok = false;
                return;
            }
            ensure_space(line.height_pt);
            draw_line_at(line, size, x, cursor_y);
            cursor_y += line.height_pt;
        }
    };

    auto on_paragraph = [&](const Paragraph_block& pb) {
        const auto lines = wrap_runs(pb.runs, content_width, options.body_size_pt,
            options.body_size_pt * options.line_spacing, measure);
        if (!prepare_lines(lines, 0.0, options.body_size_pt * 0.25)) {
            return;
        }
        draw_text_lines(lines, options.body_size_pt, options.margin_left_pt);
        cursor_y += options.body_size_pt * 0.35;
    };

    auto on_heading = [&](const Heading_block& hb) {
        const auto&  hm           = heading_metrics(hb.level);
        const double size         = options.body_size_pt * hm.size_factor;
        const double space_before = size * hm.space_before_factor;
        const double space_after  = size * hm.space_after_factor;
        const auto   lines        = wrap_runs(hb.runs, content_width, size, size * 1.15, measure);
        if (!prepare_lines(lines, space_before, space_after)) {
            return;
        }
        draw_text_lines(lines, size, options.margin_left_pt);
        cursor_y += space_after;
    };

    auto on_list = [&](const List_block& lb) {
        const double gap = options.body_size_pt * 0.55;
        for (size_t i = 0; i < lb.items.size(); ++i) {
            const std::string marker       = list_marker(lb, i);
            const double      marker_width = measure(Pdf_font::REGULAR, marker, options.body_size_pt);
            const double      item_left    = options.margin_left_pt + marker_width + gap;
            const double      item_width   = content_width - marker_width - gap;
            const auto lines = wrap_runs(
                lb.items[i].runs,
                item_width,
                options.body_size_pt,
                options.body_size_pt * options.line_spacing,
                measure);
            if (!prepare_lines(lines, 0.0, options.body_size_pt * 0.2)) {
                return;
            }
            writer.draw_text(
                options.margin_left_pt, cursor_y,
                options.body_size_pt, Pdf_font::REGULAR, marker);
            draw_text_lines(lines, options.body_size_pt, item_left);
            if (!render_ok) {
                return;
            }
            cursor_y += options.body_size_pt * 0.15;
        }
        cursor_y += options.body_size_pt * 0.15;
    };

    auto on_image = [&](const Image_content_block& ib) {
        Png_image image;
        fs::path image_path(ib.path);
        if (image_path.is_relative() && !options.asset_base_dir.empty()) {
            image_path = options.asset_base_dir / image_path;
        }

        if (!image.load_from_file(image_path) || image.width_px() <= 0) {
            const std::string placeholder = ib.alt_text.empty()
                ? ib.path
                : ib.alt_text;
            ensure_space(options.body_size_pt * 1.4);
            writer.draw_text(
                options.margin_left_pt,
                cursor_y,
                options.body_size_pt * 0.9,
                Pdf_font::ITALIC,
                placeholder,
                { 0.6, 0.0, 0.0 });
            cursor_y += options.body_size_pt * 1.4;
            return;
        }

        const double natural_width_pt  = static_cast<double>(image.width_px()) * 72.0 / 96.0;
        const double natural_height_pt = static_cast<double>(image.height_px()) * 72.0 / 96.0;
        double       image_width_pt    = std::min(natural_width_pt, content_width);
        double       image_height_pt   = image_width_pt * natural_height_pt / natural_width_pt;

        const double available_height =
            options.page_height_pt - options.margin_bottom_pt - options.margin_top_pt;
        if (image_height_pt > available_height) {
            const double scale = available_height / image_height_pt;
            image_width_pt *= scale;
            image_height_pt = available_height;
        }

        ensure_space(image_height_pt + options.body_size_pt * 0.25);
        writer.draw_png(options.margin_left_pt, cursor_y, image_width_pt, image_height_pt, image);
        cursor_y += image_height_pt + options.body_size_pt * 0.25;
    };

    auto on_code = [&](const Code_block& cb) {
        const double size            = options.body_size_pt * 0.92;
        const double pad             = options.body_size_pt * 0.45;
        const double available_width = content_width - pad * 2.0;
        const auto   lines           = code_lines(cb.text, available_width, size, size * 1.25, measure);
        const double height          = total_height(lines) + pad * 2.0;
        if (pad * 2.0 + lines.front().height_pt > content_height) {
            error = "Page content area is too short for a line of code and its padding";
            render_ok = false;
            return;
        }
        const double reserved_height = height + options.body_size_pt * 0.2;
        ensure_space(reserved_height <= content_height
            ? reserved_height : pad * 2.0 + lines.front().height_pt);

        std::size_t first = 0;
        while (first < lines.size()) {
            double chunk_height = pad * 2.0;
            std::size_t end = first;
            while (end < lines.size()
                && cursor_y + chunk_height + lines[end].height_pt <= content_bottom) {
                chunk_height += lines[end].height_pt;
                ++end;
            }
            if (end == first) {
                if (cursor_y == options.margin_top_pt) {
                    error = "Page content area is too short for a line of code and its padding";
                    render_ok = false;
                    return;
                }
                new_page();
                continue;
            }
            writer.fill_rect(options.margin_left_pt, cursor_y, content_width, chunk_height,
                code_block_fill);
            double line_y = cursor_y + pad;
            for (std::size_t i = first; i < end; ++i) {
                draw_line_at(lines[i], size, options.margin_left_pt + pad, line_y);
                line_y += lines[i].height_pt;
            }
            writer.stroke_rect(options.margin_left_pt, cursor_y, content_width, chunk_height,
                default_stroke, border_line_width);
            cursor_y += chunk_height;
            first = end;
            if (first < lines.size()) {
                new_page();
            }
        }
        cursor_y += options.body_size_pt * 0.25;
    };

    auto on_table = [&](const Table_block& tb) {
        if (tb.rows.empty()) {
            return;
        }

        table_style_t style;
        style.text_size_pt    = options.body_size_pt * 0.95;
        style.text_leading_pt = style.text_size_pt * 1.22;
        style.cell_padding_pt = options.body_size_pt * 0.35;
        style.border_width_pt = border_line_width;
        style.border_color    = default_stroke;
        style.header_fill     = table_header_fill;

        const Table_columns columns = compute_table_columns(tb, content_width, style, *metrics);
        if (!columns.valid) {
            error = "Unable to lay out table within content width";
            render_ok = false;
            return;
        }

        std::vector<double> row_heights(tb.rows.size(), 0.0);
        for (std::size_t row_index = 0; row_index < tb.rows.size(); ++row_index) {
            row_heights[row_index] = measure_table_row_height(
                tb,
                static_cast<int>(row_index),
                columns,
                style,
                *metrics);
        }

        auto draw_table_element = [&](const Table_element& element) {
            std::visit(
                Overloaded{
                    [&](const Table_text_span& span) {
                        writer.draw_text(
                            span.x_pt,
                            span.y_pt,
                            span.size_pt,
                            span.font,
                            span.text,
                            span.color);
                    },
                    [&](const table_line_t& line) {
                        writer.stroke_line(
                            line.x1_pt,
                            line.y1_pt,
                            line.x2_pt,
                            line.y2_pt,
                            line.color,
                            line.width_pt);
                    },
                    [&](const table_fill_rect_t& fill) {
                        writer.fill_rect(
                            fill.x_pt,
                            fill.y_pt,
                            fill.width_pt,
                            fill.height_pt,
                            fill.color);
                    },
                },
                element);
        };

        auto draw_row = [&](std::size_t row_idx) {
            const Table_row_layout layout = layout_table_row(
                tb,
                static_cast<int>(row_idx),
                columns,
                options.margin_left_pt,
                cursor_y,
                style,
                *metrics);
            for (const Table_element& element : layout.elements) {
                draw_table_element(element);
            }
            cursor_y += layout.height_pt;
        };

        const double available =
            options.page_height_pt - options.margin_bottom_pt - options.margin_top_pt;
        const double header_height = tb.has_header ? row_heights[0] : 0.0;

        const std::size_t body_start = tb.has_header ? 1 : 0;

        if (tb.has_header) {
            const double min_fit = header_height
                + (body_start < tb.rows.size() ? row_heights[body_start] : 0.0);
            ensure_space(min_fit);
            draw_row(0);
        }
        else {
            ensure_space(row_heights[0]);
        }

        for (std::size_t row_idx = body_start; row_idx < tb.rows.size(); ++row_idx) {
            const double row_height   = row_heights[row_idx];
            const bool   fits_on_page =
                cursor_y + row_height <= options.page_height_pt - options.margin_bottom_pt;
            if (!fits_on_page) {
                // If the row is taller than a full page there's nothing
                // better we can do than draw it where it lands; otherwise
                // start a fresh page and repeat the header.
                const bool row_exceeds_page = row_height > available;
                if (!row_exceeds_page) {
                    new_page();
                    if (tb.has_header) {
                        draw_row(0);
                    }
                }
            }
            draw_row(row_idx);
        }
        cursor_y += options.body_size_pt * 0.25;
    };

    auto on_page_break = [&](const Page_break_block&) {
        new_page();
    };

    auto on_thematic_break = [&](const Thematic_break_block&) {
        constexpr color_t rule_color    = { 0.5, 0.5, 0.5 };
        constexpr double  rule_width_pt = 0.5;
        const double      space_before  = options.body_size_pt * 0.5;
        const double      space_after   = options.body_size_pt * 0.5;
        ensure_space(space_before + rule_width_pt + space_after);
        cursor_y += space_before;
        writer.stroke_line(
            options.margin_left_pt,
            cursor_y,
            options.margin_left_pt + content_width,
            cursor_y,
            rule_color,
            rule_width_pt);
        cursor_y += rule_width_pt + space_after;
    };

    for (const auto& block : blocks) {
        std::visit(
            Overloaded{
                on_paragraph,
                on_heading,
                on_list,
                on_image,
                on_code,
                on_table,
                on_page_break,
                on_thematic_break,
            },
            block);
        if (!render_ok) {
            return false;
        }
    }

    if (!writer.save(output_path)) {
        error = "Failed to write PDF file: " + output_path.string();
        return false;
    }
    return true;
}

bool render_markdown_to_pdf(
    const std::string&     markdown,
    const fs::path&        output_path,
    const Render_options&  options)
{
    std::string scratch;
    return render_markdown_to_pdf(markdown, output_path, options, scratch);
}

} // namespace mark2haru
