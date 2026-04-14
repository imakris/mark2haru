#include "render.h"

#include "pdf_writer.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace mark2haru {
namespace {

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

        std::string fragment;
        double fragment_width = 0.0;
        for (char ch : word) {
            const std::string piece(1, ch);
            const double ch_width = measure(font_for(style), piece, size_pt);
            if (current_width > 0.0 && current_width + fragment_width + ch_width > max_width_pt) {
                if (!fragment.empty()) {
                    current.spans.emplace_back(fragment, style);
                    current_width += fragment_width;
                    fragment.clear();
                    fragment_width = 0.0;
                }
                finish_line();
            }
            fragment.push_back(ch);
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
    default: return body_size * 1.05;
    }
}

double heading_spacing_before(int level, double body_size)
{
    switch (level) {
    case 1: return body_size * 0.95;
    case 2: return body_size * 0.75;
    default: return body_size * 0.55;
    }
}

double heading_spacing_after(int level, double body_size)
{
    switch (level) {
    case 1: return body_size * 0.65;
    case 2: return body_size * 0.55;
    default: return body_size * 0.45;
    }
}

std::string list_marker(const ListBlock& lb, size_t index)
{
    if (!lb.ordered) {
        return "-";
    }
    return std::to_string(lb.start_number + static_cast<int>(index)) + ".";
}

template <class MeasureFn>
std::vector<Line> code_lines(const std::string& text, double max_width_pt, double size_pt,
                             double leading, MeasureFn&& measure)
{
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<Token> tokens;
        tokens.push_back({ line, InlineStyle::Code, false });
        auto wrapped = wrap_tokens(tokens, max_width_pt, size_pt, leading, measure);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (lines.empty()) {
        lines.push_back({ {}, size_pt * leading });
    }
    return lines;
}

} // namespace

bool render_markdown_to_pdf(const std::string& markdown,
                            const std::string& output_path,
                            const RenderOptions& options)
{
    const auto blocks = parse_markdown(markdown);
    PdfWriter writer(options.page_width_pt, options.page_height_pt, options.font_root_dir);
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
        return writer.measure_text_width(font, text, size_pt);
    };

    auto draw_line = [&](const Line& line, double size_pt, double x_pt) {
        double x = x_pt;
        for (const auto& [text, style] : line.spans) {
            const PdfFont font = font_for(style);
            writer.draw_text(x, cursor_y, size_pt, font, text);
            x += measure(font, text, size_pt);
        }
    };

    for (const auto& block : blocks) {
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
            ensure_space(heading_spacing_before(hb.level, size) + height + heading_spacing_after(hb.level, size));
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
            double y = cursor_y + pad;
            for (const auto& line : lines) {
                double x = options.margin_left_pt + pad;
                for (const auto& [text, style] : line.spans) {
                    writer.draw_text(x, y, size, PdfFont::Mono, text);
                    x += measure(PdfFont::Mono, text, size);
                }
                y += line.height_pt;
            }
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
            const double col_width = content_width / static_cast<double>(column_count);
            std::vector<double> row_heights;
            row_heights.reserve(tb.rows.size());
            for (const auto& row : tb.rows) {
                double row_height = 0.0;
                for (size_t col = 0; col < column_count; ++col) {
                    const std::vector<InlineRun> empty;
                    const auto& runs = col < row.cells.size() ? row.cells[col].runs : empty;
                    const auto lines = wrap_runs(runs, col_width - cell_pad * 2.0,
                                                 options.body_size_pt * 0.95, 1.22, measure);
                    const double cell_height = total_height(lines) + cell_pad * 2.0;
                    row_height = std::max(row_height, cell_height);
                }
                row_heights.push_back(row_height);
            }

            double total = 0.0;
            for (double h : row_heights) total += h;
            ensure_space(total + options.body_size_pt * 0.25);

            double y = cursor_y;
            for (size_t row_idx = 0; row_idx < tb.rows.size(); ++row_idx) {
                const auto& row = tb.rows[row_idx];
                const double row_height = row_heights[row_idx];
                const bool header = tb.has_header && row_idx == 0;
                if (header) {
                    writer.set_fill_color({ 0.92, 0.92, 0.92 });
                    writer.fill_rect(options.margin_left_pt, y, content_width, row_height);
                    writer.set_fill_color({ 0.96, 0.96, 0.96 });
                }

                double x = options.margin_left_pt;
                for (size_t col = 0; col < column_count; ++col) {
                    writer.stroke_rect(x, y, col_width, row_height);
                    const std::vector<InlineRun> empty;
                    const auto& runs = col < row.cells.size() ? row.cells[col].runs : empty;
                    const auto lines = wrap_runs(runs, col_width - cell_pad * 2.0,
                                                 options.body_size_pt * 0.95, 1.22, measure);
                    double cell_y = y + cell_pad;
                    for (const auto& line : lines) {
                        double text_x = x + cell_pad;
                        for (const auto& [text, style] : line.spans) {
                            const PdfFont font = style == InlineStyle::Code ? PdfFont::Mono : font_for(style);
                            writer.draw_text(text_x, cell_y, options.body_size_pt * 0.95, font, text);
                            text_x += measure(font, text, options.body_size_pt * 0.95);
                        }
                        cell_y += line.height_pt;
                    }
                    x += col_width;
                }
                y += row_height;
            }
            cursor_y += total + options.body_size_pt * 0.25;
            continue;
        }

        if (std::holds_alternative<PageBreakBlock>(block)) {
            new_page();
            continue;
        }
    }

    return writer.save(output_path);
}

} // namespace mark2haru
