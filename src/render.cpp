#include <mark2haru/render.h>

#include <mark2haru/pdf_writer.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

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
        if ((c & 0x80) == 0x00) { len = 1; } else
        if ((c & 0xE0) == 0xC0) { len = 2; } else
        if ((c & 0xF0) == 0xE0) { len = 3; } else
        if ((c & 0xF8) == 0xF0) { len = 4; }

        if (i + len > text.size()) {
            len = 1;
        }
        out.emplace_back(text.substr(i, len));
        i += len;
    }
    return out;
}

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

struct token_t {
    std::string text;
    Inline_style style = Inline_style::NORMAL;
    bool newline = false;
};

struct line_t {
    std::vector<std::pair<std::string, Inline_style>> spans;
    double height_pt = 0.0;
};

std::vector<token_t> tokenize_runs(const std::vector<inline_run_t>& runs)
{
    std::vector<token_t> tokens;
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
std::vector<line_t> wrap_tokens(
    const std::vector<token_t>& tokens,
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    std::vector<line_t> lines;
    line_t current;
    double current_width = 0.0;

    auto finish_line = [&]() {
        current.height_pt = size_pt * leading;
        lines.push_back(current);
        current = line_t{};
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
std::vector<line_t> wrap_runs(
    const std::vector<inline_run_t>& runs,
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    return wrap_tokens(tokenize_runs(runs), max_width_pt, size_pt, leading, measure);
}

double total_height(const std::vector<line_t>& lines)
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
        case 1:  return body_size * 1.65;
        case 2:  return body_size * 1.35;
        case 3:  return body_size * 1.18;
        case 4:  return body_size * 1.08;
        case 5:  return body_size * 1.00;
        case 6:  return body_size * 0.92;
        default: return body_size;
    }
}

double heading_spacing_before(int level, double body_size)
{
    switch (level) {
        case 1:  return body_size * 0.95;
        case 2:  return body_size * 0.75;
        case 3:  return body_size * 0.60;
        case 4:  return body_size * 0.50;
        default: return body_size * 0.45;
    }
}

double heading_spacing_after(int level, double body_size)
{
    switch (level) {
        case 1:  return body_size * 0.65;
        case 2:  return body_size * 0.55;
        case 3:  return body_size * 0.45;
        case 4:  return body_size * 0.40;
        default: return body_size * 0.35;
    }
}

std::string list_marker(const list_block_t& lb, size_t index)
{
    if (!lb.ordered) {
        // U+2022 BULLET
        return "\xe2\x80\xa2";
    }
    return std::to_string(lb.start_number + static_cast<int>(index)) + ".";
}

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
std::vector<line_t> code_lines(
    const std::string& text,
    double max_width_pt,
    double size_pt,
    double leading,
    MeasureFn&& measure)
{
    std::vector<line_t> lines;
    for (const auto& raw : code_split_lines(text)) {
        std::vector<token_t> tokens;
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
    const render_options_t& options)
{
    const auto blocks = parse_markdown(markdown);
    auto metrics = std::make_shared<Measurement_context>(options.font_family, options.font_root_dir);
    if (!metrics->loaded()) {
        return false;
    }

    Pdf_writer writer(options.page_width_pt, options.page_height_pt, metrics);
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

    auto measure = [&](Pdf_font font, const std::string& text, double size_pt) {
        return metrics->measure_text_width(font, text, size_pt);
    };

    auto draw_line = [&](const line_t& line, double size_pt, double x_pt) {
        double x = x_pt;
        for (const auto& [text, style] : line.spans) {
            const Pdf_font font = font_for(style);
            writer.draw_text(x, cursor_y, size_pt, font, text);
            x += measure(font, text, size_pt);
        }
    };

    for (const auto& block : blocks) {
        if (std::holds_alternative<paragraph_block_t>(block)) {
            const auto& pb = std::get<paragraph_block_t>(block);
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

        if (std::holds_alternative<heading_block_t>(block)) {
            const auto& hb = std::get<heading_block_t>(block);
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

        if (std::holds_alternative<list_block_t>(block)) {
            const auto& lb = std::get<list_block_t>(block);
            for (size_t i = 0; i < lb.items.size(); ++i) {
                const std::string marker = list_marker(lb, i);
                const double marker_width = measure(Pdf_font::REGULAR, marker, options.body_size_pt);
                const double gap = options.body_size_pt * 0.55;
                const double item_left = options.margin_left_pt + marker_width + gap;
                const double item_width = content_width - marker_width - gap;
                const auto lines = wrap_runs(lb.items[i].runs, item_width, options.body_size_pt,
                                             options.line_spacing, measure);
                const double height = total_height(lines);
                ensure_space(height + options.body_size_pt * 0.2);
                writer.draw_text(
                    options.margin_left_pt,
                    cursor_y,
                    options.body_size_pt,
                    Pdf_font::REGULAR,
                    marker);
                for (const auto& line : lines) {
                    draw_line(line, options.body_size_pt, item_left);
                    cursor_y += line.height_pt;
                }
                cursor_y += options.body_size_pt * 0.15;
            }
            cursor_y += options.body_size_pt * 0.15;
            continue;
        }

        if (std::holds_alternative<code_block_t>(block)) {
            const auto& cb = std::get<code_block_t>(block);
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
                    writer.draw_text(x, y, size, Pdf_font::MONO, text);
                    x += measure(Pdf_font::MONO, text, size);
                }
                y += line.height_pt;
            }
            writer.stroke_rect(options.margin_left_pt, cursor_y, content_width, height);
            cursor_y += height + options.body_size_pt * 0.25;
            continue;
        }

        if (std::holds_alternative<table_block_t>(block)) {
            const auto& tb = std::get<table_block_t>(block);
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
            std::vector<std::vector<std::vector<line_t>>> cell_lines(tb.rows.size());
            std::vector<double> row_heights(tb.rows.size(), 0.0);
            for (size_t r = 0; r < tb.rows.size(); ++r) {
                cell_lines[r].resize(column_count);
                const auto& row = tb.rows[r];
                double row_height = 0.0;
                for (size_t col = 0; col < column_count; ++col) {
                    const std::vector<inline_run_t> empty;
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
                    double cell_y = cursor_y + cell_pad;
                    for (const auto& line : lines) {
                        double text_x = x + cell_pad;
                        for (const auto& [text, style] : line.spans) {
                            const Pdf_font font = style == Inline_style::CODE ? Pdf_font::MONO : font_for(style);
                            writer.draw_text(text_x, cell_y, cell_size, font, text);
                            text_x += measure(font, text, cell_size);
                        }
                        cell_y += line.height_pt;
                    }
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
            continue;
        }

        if (std::holds_alternative<page_break_block_t>(block)) {
            new_page();
            continue;
        }
    }

    return writer.save(output_path);
}

} // namespace mark2haru
