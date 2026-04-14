#include "render.h"

#include "pdf_writer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mark2haru {
namespace {

// Split a UTF-8 string into complete code-point units. Invalid bytes are
// emitted as single-byte units so the rest of the string still makes it
// through and the character-level wrap fallback does not slice a multi-byte
// sequence in half.
std::vector<std::string> utf8_pieces(const std::string& text)
{
    std::vector<std::string> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((c & 0x80) == 0x00) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (i + len > text.size()) {
            len = 1;
        }
        out.emplace_back(text.substr(i, len));
        i += len;
    }
    return out;
}

PdfFont font_for(InlineStyle style)
{
    switch (style) {
    case InlineStyle::Bold: return PdfFont::Bold;
    case InlineStyle::Italic: return PdfFont::Italic;
    case InlineStyle::BoldItalic: return PdfFont::BoldItalic;
    case InlineStyle::Code: return PdfFont::Mono;
    case InlineStyle::Normal:
    default:
        return PdfFont::Regular;
    }
}

struct Token {
    std::string text;
    InlineStyle style = InlineStyle::Normal;
    bool newline = false;
};

struct Line {
    std::vector<std::pair<std::string, InlineStyle>> spans;
    double height_pt = 0.0;
};

std::vector<Token> tokenize_runs(const std::vector<InlineRun>& runs)
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
std::vector<Line> wrap_tokens(const std::vector<Token>& tokens,
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

    auto add_word = [&](const std::string& word, InlineStyle style) {
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
        for (const auto& piece : utf8_pieces(word)) {
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
            } else if (!current.spans.empty()) {
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
std::vector<Line> wrap_runs(const std::vector<InlineRun>& runs,
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

double heading_size(int level, double body_size)
{
    switch (level) {
    case 1: return body_size * 1.65;
    case 2: return body_size * 1.35;
    case 3: return body_size * 1.18;
    case 4: return body_size * 1.08;
    case 5: return body_size * 1.00;
    case 6: return body_size * 0.92;
    default: return body_size;
    }
}

double heading_spacing_before(int level, double body_size)
{
    switch (level) {
    case 1: return body_size * 0.95;
    case 2: return body_size * 0.75;
    case 3: return body_size * 0.60;
    case 4: return body_size * 0.50;
    default: return body_size * 0.45;
    }
}

double heading_spacing_after(int level, double body_size)
{
    switch (level) {
    case 1: return body_size * 0.65;
    case 2: return body_size * 0.55;
    case 3: return body_size * 0.45;
    case 4: return body_size * 0.40;
    default: return body_size * 0.35;
    }
}

std::string list_marker(const ListBlock& lb, size_t index)
{
    if (!lb.ordered) {
        // U+2022 BULLET
        return "\xe2\x80\xa2";
    }
    return std::to_string(lb.start_number + static_cast<int>(index)) + ".";
}

// Split a code block's body on '\n' (and trailing '\r'). Duplicates the
// parser's split_lines rather than depending on it because the parser lives in
// another translation unit; the logic is identical by design.
std::vector<std::string> code_split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            std::string line = text.substr(pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            break;
        }
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        pos = nl + 1;
    }
    return lines;
}

template <class MeasureFn>
std::vector<Line> code_lines(const std::string& text, double max_width_pt, double size_pt,
                             double leading, MeasureFn&& measure)
{
    std::vector<Line> lines;
    for (const auto& raw : code_split_lines(text)) {
        std::vector<Token> tokens;
        tokens.push_back({ raw, InlineStyle::Code, false });
        auto wrapped = wrap_tokens(tokens, max_width_pt, size_pt, leading, measure);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (lines.empty()) {
        lines.push_back({ {}, size_pt * leading });
    }
    return lines;
}

// Estimate the height of the first line of a block without actually wrapping
// it. Used for heading widow/orphan control so that we can keep a heading
// with its first body line on the same page.
template <class MeasureFn>
double first_line_height(const Block& block, double content_width,
                         const RenderOptions& options, MeasureFn&& measure)
{
    if (std::holds_alternative<ParagraphBlock>(block)) {
        const auto& pb = std::get<ParagraphBlock>(block);
        const auto lines = wrap_runs(pb.runs, content_width, options.body_size_pt,
                                     options.line_spacing, measure);
        return lines.empty() ? 0.0 : lines.front().height_pt;
    }
    if (std::holds_alternative<HeadingBlock>(block)) {
        const auto& hb = std::get<HeadingBlock>(block);
        const double size = heading_size(hb.level, options.body_size_pt);
        return size * 1.15;
    }
    if (std::holds_alternative<ListBlock>(block)) {
        const auto& lb = std::get<ListBlock>(block);
        if (lb.items.empty()) {
            return 0.0;
        }
        const auto lines = wrap_runs(lb.items.front().runs, content_width,
                                     options.body_size_pt, options.line_spacing, measure);
        return lines.empty() ? 0.0 : lines.front().height_pt;
    }
    if (std::holds_alternative<CodeBlock>(block)) {
        return options.body_size_pt * 0.92 * 1.25 + options.body_size_pt * 0.9;
    }
    if (std::holds_alternative<TableBlock>(block)) {
        return options.body_size_pt * 1.5;
    }
    return options.body_size_pt * options.line_spacing;
}

} // namespace

bool render_markdown_to_pdf(const std::string& markdown,
                            const std::filesystem::path& output_path,
                            const RenderOptions& options)
{
    const auto blocks = parse_markdown(markdown);
    auto metrics = std::make_shared<MeasurementContext>(options.font_family, options.font_root_dir);
    if (!metrics->loaded()) {
        return false;
    }

    PdfWriter writer(options.page_width_pt, options.page_height_pt, metrics);
    if (!writer.fonts_loaded()) {
        return false;
    }

    writer.set_stroke_color({ 0.2, 0.2, 0.2 });
    writer.set_fill_color({ 0.96, 0.96, 0.96 });
    writer.set_line_width(0.75);

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

    auto measure = [&](PdfFont font, const std::string& text, double size_pt) {
        return metrics->measure_text_width(font, text, size_pt);
    };

    auto draw_line = [&](const Line& line, double size_pt, double x_pt) {
        double x = x_pt;
        for (const auto& [text, style] : line.spans) {
            const PdfFont font = font_for(style);
            writer.draw_text(x, cursor_y, size_pt, font, text);
            x += measure(font, text, size_pt);
        }
    };

    for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        const auto& block = blocks[block_index];
        if (std::holds_alternative<ParagraphBlock>(block)) {
            const auto& pb = std::get<ParagraphBlock>(block);
            const auto lines = wrap_runs(pb.runs, content_width, options.body_size_pt,
                                         options.line_spacing, measure);
            const double height = total_height(lines);
            ensure_space(height + options.body_size_pt * 0.25);
            for (const auto& line : lines) {
                draw_line(line, options.body_size_pt, options.margin_left_pt);
                cursor_y += line.height_pt;
            }
            cursor_y += options.body_size_pt * 0.35;
            continue;
        }

        if (std::holds_alternative<HeadingBlock>(block)) {
            const auto& hb = std::get<HeadingBlock>(block);
            const double size = heading_size(hb.level, options.body_size_pt);
            const auto lines = wrap_runs(hb.runs, content_width, size, 1.15, measure);
            const double height = total_height(lines);
            // Orphan control: reserve space for at least the first line of the
            // next block as well, so a heading can never land at the very
            // bottom of a page with its body overflowing onto the next.
            double follow_height = 0.0;
            if (block_index + 1 < blocks.size()) {
                follow_height = first_line_height(blocks[block_index + 1], content_width,
                                                  options, measure);
            }
            ensure_space(heading_spacing_before(hb.level, size) + height
                         + heading_spacing_after(hb.level, size) + follow_height);
            cursor_y += heading_spacing_before(hb.level, size);
            for (const auto& line : lines) {
                draw_line(line, size, options.margin_left_pt);
                cursor_y += line.height_pt;
            }
            cursor_y += heading_spacing_after(hb.level, size);
            continue;
        }

        if (std::holds_alternative<ListBlock>(block)) {
            const auto& lb = std::get<ListBlock>(block);
            for (size_t i = 0; i < lb.items.size(); ++i) {
                const std::string marker = list_marker(lb, i);
                const double marker_width = measure(PdfFont::Regular, marker, options.body_size_pt);
                const double gap = options.body_size_pt * 0.55;
                const double item_left = options.margin_left_pt + marker_width + gap;
                const double item_width = content_width - marker_width - gap;
                const auto lines = wrap_runs(lb.items[i].runs, item_width, options.body_size_pt,
                                             options.line_spacing, measure);
                const double height = total_height(lines);
                ensure_space(height + options.body_size_pt * 0.2);
                writer.draw_text(options.margin_left_pt, cursor_y, options.body_size_pt,
                                 PdfFont::Regular, marker);
                for (const auto& line : lines) {
                    draw_line(line, options.body_size_pt, item_left);
                    cursor_y += line.height_pt;
                }
                cursor_y += options.body_size_pt * 0.15;
            }
            cursor_y += options.body_size_pt * 0.15;
            continue;
        }

        if (std::holds_alternative<CodeBlock>(block)) {
            const auto& cb = std::get<CodeBlock>(block);
            const double size = options.body_size_pt * 0.92;
            const double pad = options.body_size_pt * 0.45;
            const double available_width = content_width - pad * 2.0;
            const auto lines = code_lines(cb.text, available_width, size, 1.25, measure);
            const double height = total_height(lines) + pad * 2.0;
            ensure_space(height + options.body_size_pt * 0.2);
            writer.fill_rect(options.margin_left_pt, cursor_y, content_width, height);
            // Reuse draw_line by mutating cursor_y. The code-block spans are
            // already marked InlineStyle::Code, so font_for() picks Mono
            // naturally — no hard-coded mono branch needed here.
            const double saved_y = cursor_y;
            cursor_y = saved_y + pad;
            for (const auto& line : lines) {
                draw_line(line, size, options.margin_left_pt + pad);
                cursor_y += line.height_pt;
            }
            cursor_y = saved_y;
            writer.stroke_rect(options.margin_left_pt, cursor_y, content_width, height);
            cursor_y += height + options.body_size_pt * 0.25;
            continue;
        }

        if (std::holds_alternative<TableBlock>(block)) {
            const auto& tb = std::get<TableBlock>(block);
            if (tb.rows.empty()) {
                continue;
            }

            size_t column_count = 0;
            for (const auto& row : tb.rows) {
                column_count = std::max(column_count, row.cells.size());
            }
            if (column_count == 0) {
                continue;
            }

            const double cell_pad = options.body_size_pt * 0.35;
            const double cell_size = options.body_size_pt * 0.95;
            const double col_width = content_width / static_cast<double>(column_count);
            const double inner_width = col_width - cell_pad * 2.0;

            // Wrap every cell once up front and cache both the wrapped lines
            // and the computed row heights. The draw path then reuses the
            // cached wraps, avoiding the double-wrap that the previous
            // implementation performed per row.
            std::vector<std::vector<std::vector<Line>>> cell_lines(tb.rows.size());
            std::vector<double> row_heights(tb.rows.size(), 0.0);
            for (size_t r = 0; r < tb.rows.size(); ++r) {
                cell_lines[r].resize(column_count);
                const auto& row = tb.rows[r];
                double row_height = 0.0;
                for (size_t col = 0; col < column_count; ++col) {
                    const std::vector<InlineRun> empty;
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
                    writer.set_fill_color({ 0.92, 0.92, 0.92 });
                    writer.fill_rect(options.margin_left_pt, cursor_y, content_width, row_height);
                    writer.set_fill_color({ 0.96, 0.96, 0.96 });
                }

                double x = options.margin_left_pt;
                for (size_t col = 0; col < column_count; ++col) {
                    writer.stroke_rect(x, cursor_y, col_width, row_height);
                    const auto& lines = cell_lines[row_idx][col];
                    const double saved_y = cursor_y;
                    cursor_y = saved_y + cell_pad;
                    for (const auto& line : lines) {
                        draw_line(line, cell_size, x + cell_pad);
                        cursor_y += line.height_pt;
                    }
                    cursor_y = saved_y;
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
            } else {
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
            continue;
        }

        if (std::holds_alternative<PageBreakBlock>(block)) {
            // Avoid stacking blank pages: if the current page has no content
            // and the cursor is still at the top margin, the break is a
            // no-op.
            if (!writer.page_empty() || cursor_y > options.margin_top_pt + 0.001) {
                new_page();
            }
            continue;
        }
    }

    return writer.save(output_path);
}

} // namespace mark2haru
