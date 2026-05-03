#include <mark2haru/render.h>

#include <mark2haru/pdf_writer.h>
#include <mark2haru/png_image.h>

#include "io_helpers.h"
#include "utf8_decode.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

Pdf_font font_for(Inline_style style)
{
    switch (style) {
        case Inline_style::BOLD:        return Pdf_font::BOLD;
        case Inline_style::ITALIC:      return Pdf_font::ITALIC;
        case Inline_style::BOLD_ITALIC: return Pdf_font::BOLD_ITALIC;
        case Inline_style::CODE:        return Pdf_font::MONO;
        case Inline_style::NORMAL:
        default:                        return Pdf_font::REGULAR;
    }
}

struct Token
{
    std::string text;
    Inline_style style = Inline_style::NORMAL;
    bool newline = false;
};

struct Line
{
    std::vector<std::pair<std::string, Inline_style>> spans;
    double height_pt = 0.0;
};

std::vector<Token> tokenize_runs(const std::vector<Inline_run>& runs)
{
    std::vector<Token> tokens;
    for (const auto& run : runs) {
        size_t start = 0;
        while (start <= run.text.size()) {
            const size_t nl = run.text.find('\n', start);
            const std::string chunk = nl == std::string::npos
                ? run.text.substr(start)
                : run.text.substr(start, nl - start);

            size_t piece = 0;
            while (piece <= chunk.size()) {
                const size_t sp = chunk.find(' ', piece);
                if (sp == std::string::npos) {
                    if (piece < chunk.size()) {
                        tokens.push_back({ chunk.substr(piece), run.style, false });
                    }
                    break;
                }
                if (sp > piece) {
                    tokens.push_back({ chunk.substr(piece, sp - piece), run.style, false });
                }
                tokens.push_back({ " ", run.style, false });
                piece = sp + 1;
            }

            if (nl == std::string::npos) {
                break;
            }
            tokens.push_back({ "", run.style, true });
            start = nl + 1;
        }
    }
    return tokens;
}

template <class MeasureFn>
std::vector<Line> wrap_tokens(
    const std::vector<Token>& tokens,
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    std::vector<Line> lines;
    Line current;
    double current_width = 0.0;

    auto finish_line = [&]() {
        current.height_pt = size_pt * leading;
        lines.push_back(current);
        current = Line{};
        current_width = 0.0;
    };

    auto add_word = [&](const std::string& word, Inline_style style) {
        const double word_width = measure(font_for(style), word, size_pt);
        if (current_width > 0.0 && current_width + word_width > max_width_pt) {
            finish_line();
        }
        if (word_width <= max_width_pt) {
            current.spans.emplace_back(word, style);
            current_width += word_width;
            return;
        }

        // Word is wider than a full line: break it at code-point boundaries.
        std::string fragment;
        double fragment_width = 0.0;
        for (const auto& piece : utf8::split_pieces(word)) {
            const double ch_width = measure(font_for(style), piece, size_pt);
            const double projected = current_width + fragment_width + ch_width;
            const bool any_content_on_line = current_width > 0.0 || fragment_width > 0.0;
            if (any_content_on_line && projected > max_width_pt) {
                if (!fragment.empty()) {
                    current.spans.emplace_back(fragment, style);
                    current_width += fragment_width;
                    fragment.clear();
                    fragment_width = 0.0;
                }
                finish_line();
            }
            fragment += piece;
            fragment_width += ch_width;
        }
        if (!fragment.empty()) {
            current.spans.emplace_back(fragment, style);
            current_width += fragment_width;
        }
    };

    for (const auto& token : tokens) {
        if (token.newline) {
            finish_line();
            continue;
        }
        if (token.text == " ") {
            const double space_width = measure(font_for(token.style), token.text, size_pt);
            if (!current.spans.empty() && current_width + space_width > max_width_pt) {
                finish_line();
            }
            else
            if (!current.spans.empty()) {
                current.spans.emplace_back(token.text, token.style);
                current_width += space_width;
            }
            continue;
        }
        if (!token.text.empty()) {
            add_word(token.text, token.style);
        }
    }

    if (!current.spans.empty() || lines.empty()) {
        finish_line();
    }
    return lines;
}

template <class MeasureFn>
std::vector<Line> wrap_runs(
    const std::vector<Inline_run>& runs,
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    return wrap_tokens(tokenize_runs(runs), max_width_pt, size_pt, leading, measure);
}

double total_height(const std::vector<Line>& lines)
{
    double h = 0.0;
    for (const auto& line : lines) {
        h += line.height_pt;
    }
    return h;
}

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
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    std::vector<Line> lines;
    for (const auto& raw : split_lines(text)) {
        std::vector<Token> tokens;
        tokens.push_back({ raw, Inline_style::CODE, false });
        auto wrapped = wrap_tokens(tokens, max_width_pt, size_pt, leading, measure);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (lines.empty()) {
        lines.push_back({ {}, size_pt * leading });
    }
    return lines;
}

} // namespace

bool render_markdown_to_pdf(
    const std::string& markdown,
    const fs::path& output_path,
    const Render_options& options,
    std::string& error)
{
    const auto blocks = parse_markdown(markdown);
    auto metrics = std::make_shared<Measurement_context>(options.font_family, options.font_root_dir);
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

    constexpr color_t default_stroke = { 0.2, 0.2, 0.2 };
    constexpr color_t code_block_fill = { 0.96, 0.96, 0.96 };
    constexpr color_t table_header_fill = { 0.92, 0.92, 0.92 };
    constexpr double border_line_width = 0.75;

    const double content_width = options.page_width_pt - options.margin_left_pt - options.margin_right_pt;
    double cursor_y = options.margin_top_pt;

    auto new_page = [&]() {
        writer.begin_page();
        cursor_y = options.margin_top_pt;
    };

    auto ensure_space = [&](double height_pt) {
        if (cursor_y + height_pt > options.page_height_pt - options.margin_bottom_pt) {
            new_page();
        }
    };

    auto measure = [&](Pdf_font font, const std::string& text, double size_pt) {
        return metrics->measure_text_width(font, text, size_pt);
    };

    // Draws a sequence of lines starting at (x, y), advancing y by each
    // line's height. Returns the y after the last line. Used by every
    // block that draws text: paragraph/heading/list pass cursor_y in and
    // store the result back; code/table pass a local box-relative y in
    // and ignore the return.
    auto draw_lines_at = [&](
        const std::vector<Line>& lines,
        double size, double x, double y) -> double
    {
        for (const auto& line : lines) {
            double tx = x;
            for (const auto& [text, style] : line.spans) {
                const Pdf_font font = font_for(style);
                writer.draw_text(tx, y, size, font, text);
                tx += measure(font, text, size);
            }
            y += line.height_pt;
        }
        return y;
    };

    auto on_paragraph = [&](const Paragraph_block& pb) {
        const auto lines = wrap_runs(pb.runs, content_width, options.body_size_pt,
                                     options.line_spacing, measure);
        ensure_space(total_height(lines) + options.body_size_pt * 0.25);
        cursor_y = draw_lines_at(lines, options.body_size_pt, options.margin_left_pt, cursor_y);
        cursor_y += options.body_size_pt * 0.35;
    };

    auto on_heading = [&](const Heading_block& hb) {
        const auto& hm = heading_metrics(hb.level);
        const double size = options.body_size_pt * hm.size_factor;
        const double space_before = size * hm.space_before_factor;
        const double space_after = size * hm.space_after_factor;
        const auto lines = wrap_runs(hb.runs, content_width, size, 1.15, measure);
        ensure_space(space_before + total_height(lines) + space_after);
        cursor_y += space_before;
        cursor_y = draw_lines_at(lines, size, options.margin_left_pt, cursor_y);
        cursor_y += space_after;
    };

    auto on_list = [&](const List_block& lb) {
        const double gap = options.body_size_pt * 0.55;
        for (size_t i = 0; i < lb.items.size(); ++i) {
            const std::string marker = list_marker(lb, i);
            const double marker_width = measure(Pdf_font::REGULAR, marker, options.body_size_pt);
            const double item_left = options.margin_left_pt + marker_width + gap;
            const double item_width = content_width - marker_width - gap;
            const auto lines = wrap_runs(lb.items[i].runs, item_width, options.body_size_pt,
                                         options.line_spacing, measure);
            ensure_space(total_height(lines) + options.body_size_pt * 0.2);
            writer.draw_text(
                options.margin_left_pt, cursor_y,
                options.body_size_pt, Pdf_font::REGULAR, marker);
            cursor_y = draw_lines_at(lines, options.body_size_pt, item_left, cursor_y);
            cursor_y += options.body_size_pt * 0.15;
        }
        cursor_y += options.body_size_pt * 0.15;
    };

    auto on_image = [&](const Image_content_block& ib) {
        Png_image image;
        if (!image.load_from_file(fs::path(ib.path)) || image.width_px() <= 0) {
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

        const double natural_width_pt = static_cast<double>(image.width_px()) * 72.0 / 96.0;
        const double natural_height_pt = static_cast<double>(image.height_px()) * 72.0 / 96.0;
        double image_width_pt = std::min(natural_width_pt, content_width);
        double image_height_pt = image_width_pt * natural_height_pt / natural_width_pt;

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
        const double size = options.body_size_pt * 0.92;
        const double pad = options.body_size_pt * 0.45;
        const double available_width = content_width - pad * 2.0;
        const auto lines = code_lines(cb.text, available_width, size, 1.25, measure);
        const double height = total_height(lines) + pad * 2.0;
        ensure_space(height + options.body_size_pt * 0.2);
        writer.fill_rect(options.margin_left_pt, cursor_y, content_width, height,
                         code_block_fill);
        draw_lines_at(lines, size, options.margin_left_pt + pad, cursor_y + pad);
        writer.stroke_rect(options.margin_left_pt, cursor_y, content_width, height,
                           default_stroke, border_line_width);
        cursor_y += height + options.body_size_pt * 0.25;
    };

    auto on_table = [&](const Table_block& tb) {
        if (tb.rows.empty()) {
            return;
        }

        size_t column_count = 0;
        for (const auto& row : tb.rows) {
            column_count = std::max(column_count, row.cells.size());
        }
        if (column_count == 0) {
            return;
        }

        const double cell_pad = options.body_size_pt * 0.35;
        const double cell_size = options.body_size_pt * 0.95;
        const double col_width = content_width / static_cast<double>(column_count);
        const double inner_width = col_width - cell_pad * 2.0;
        std::vector<std::vector<std::vector<Line>>> cell_lines(tb.rows.size());
        std::vector<double> row_heights(tb.rows.size(), 0.0);
        for (size_t r = 0; r < tb.rows.size(); ++r) {
            cell_lines[r].resize(column_count);
            const auto& row = tb.rows[r];
            double row_height = 0.0;
            for (size_t col = 0; col < column_count; ++col) {
                const std::vector<Inline_run> empty;
                const auto& runs = col < row.cells.size() ? row.cells[col].runs : empty;
                cell_lines[r][col] = wrap_runs(runs, inner_width, cell_size, 1.22, measure);
                const double cell_height = total_height(cell_lines[r][col]) + cell_pad * 2.0;
                row_height = std::max(row_height, cell_height);
            }
            row_heights[r] = row_height;
        }

        auto draw_row = [&](size_t row_idx) {
            const double row_height = row_heights[row_idx];
            const bool header = tb.has_header && row_idx == 0;
            if (header) {
                writer.fill_rect(options.margin_left_pt, cursor_y, content_width, row_height,
                                 table_header_fill);
            }

            double x = options.margin_left_pt;
            for (size_t col = 0; col < column_count; ++col) {
                writer.stroke_rect(x, cursor_y, col_width, row_height,
                                   default_stroke, border_line_width);
                draw_lines_at(cell_lines[row_idx][col], cell_size,
                              x + cell_pad, cursor_y + cell_pad);
                x += col_width;
            }
            cursor_y += row_height;
        };

        const double available =
            options.page_height_pt - options.margin_bottom_pt - options.margin_top_pt;
        const double header_height = tb.has_header ? row_heights[0] : 0.0;

        size_t body_start = tb.has_header ? 1 : 0;

        if (tb.has_header) {
            const double min_fit = header_height
                + (body_start < tb.rows.size() ? row_heights[body_start] : 0.0);
            ensure_space(min_fit);
            draw_row(0);
        }
        else {
            ensure_space(row_heights[0]);
        }

        for (size_t row_idx = body_start; row_idx < tb.rows.size(); ++row_idx) {
            const double row_height = row_heights[row_idx];
            const bool fits_on_page =
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
            },
            block);
    }

    if (!writer.save(output_path)) {
        error = "Failed to write PDF file: " + output_path.string();
        return false;
    }
    return true;
}

bool render_markdown_to_pdf(
    const std::string& markdown,
    const fs::path& output_path,
    const Render_options& options)
{
    std::string scratch;
    return render_markdown_to_pdf(markdown, output_path, options, scratch);
}

} // namespace mark2haru
