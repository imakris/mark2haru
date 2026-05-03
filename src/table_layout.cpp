#include <mark2haru/table_layout.h>

#include "utf8_decode.h"

#include <algorithm>
#include <utility>

namespace mark2haru
{
namespace {

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

std::vector<Inline_run> table_cell_runs(
    const Table_row& row,
    int col,
    bool is_header)
{
    std::vector<Inline_run> runs;
    if (col < static_cast<int>(row.cells.size())) {
        runs = row.cells[col].runs;
    }

    if (!is_header) {
        return runs;
    }

    for (auto& run : runs) {
        if (run.style == Inline_style::NORMAL) {
            run.style = Inline_style::BOLD;
        }
        else
        if (run.style == Inline_style::ITALIC) {
            run.style = Inline_style::BOLD_ITALIC;
        }
    }
    return runs;
}

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
            tokens.push_back({ {}, run.style, true });
            start = nl + 1;
        }
    }
    return tokens;
}

std::vector<Line> wrap_runs(
    const std::vector<Inline_run>& runs,
    double max_width_pt,
    double size_pt,
    double leading_pt,
    const Measurement_context& metrics)
{
    std::vector<Line> lines;
    Line current;
    double current_width = 0.0;

    auto append_span = [&](const std::string& text, Inline_style style) {
        if (text.empty()) {
            return;
        }
        if (!current.spans.empty() && current.spans.back().second == style) {
            current.spans.back().first += text;
            return;
        }
        current.spans.emplace_back(text, style);
    };

    auto finish_line = [&]() {
        current.height_pt = leading_pt;
        lines.push_back(current);
        current = Line{};
        current_width = 0.0;
    };

    auto add_word = [&](const std::string& word, Inline_style style) {
        const Pdf_font font = font_for(style);
        const double word_width = metrics.measure_text_width(font, word, size_pt);
        if (current_width > 0.0 && current_width + word_width > max_width_pt) {
            finish_line();
        }
        if (word_width <= max_width_pt) {
            append_span(word, style);
            current_width += word_width;
            return;
        }

        std::string fragment;
        double fragment_width = 0.0;
        for (const auto& piece : utf8::split_pieces(word)) {
            const double piece_width = metrics.measure_text_width(font, piece, size_pt);
            const bool has_content = current_width > 0.0 || fragment_width > 0.0;
            if (has_content && current_width + fragment_width + piece_width > max_width_pt) {
                if (!fragment.empty()) {
                    append_span(fragment, style);
                    current_width += fragment_width;
                    fragment.clear();
                    fragment_width = 0.0;
                }
                finish_line();
            }
            fragment += piece;
            fragment_width += piece_width;
        }
        if (!fragment.empty()) {
            append_span(fragment, style);
            current_width += fragment_width;
        }
    };

    for (const auto& token : tokenize_runs(runs)) {
        if (token.newline) {
            finish_line();
            continue;
        }
        if (token.text == " ") {
            const double space_width =
                metrics.measure_text_width(font_for(token.style), token.text, size_pt);
            if (!current.spans.empty() && current_width + space_width > max_width_pt) {
                finish_line();
            }
            else
            if (!current.spans.empty()) {
                append_span(token.text, token.style);
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

double lines_height(const std::vector<Line>& lines)
{
    double height = 0.0;
    for (const auto& line : lines) {
        height += line.height_pt;
    }
    return height;
}

double cell_min_width(
    const std::vector<Inline_run>& runs,
    double size_pt,
    const Measurement_context& metrics)
{
    double max_word = 0.0;
    for (const auto& run : runs) {
        const Pdf_font font = font_for(run.style);
        for (const auto& token : tokenize_runs({ run })) {
            if (!token.text.empty() && token.text != " ") {
                max_word = std::max(
                    max_word,
                    metrics.measure_text_width(font, token.text, size_pt));
            }
        }
    }
    return max_word;
}

double cell_preferred_width(
    const std::vector<Inline_run>& runs,
    double size_pt,
    const Measurement_context& metrics)
{
    double total = 0.0;
    for (const auto& run : runs) {
        total += metrics.measure_text_width(font_for(run.style), run.text, size_pt);
    }
    return total;
}

double width_sum(const std::vector<double>& widths_pt)
{
    double total = 0.0;
    for (double width_pt : widths_pt) {
        total += width_pt;
    }
    return total;
}

double table_row_height(
    const Table_block& table,
    int row_index,
    const Table_columns& columns,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    double row_height = style.text_leading_pt;
    const int header_rows = table.has_header ? 1 : 0;
    const auto& row = table.rows[row_index];
    for (int col = 0; col < columns.column_count; ++col) {
        const double content_width = columns.widths_pt[col] - 2.0 * style.cell_padding_pt;
        const auto runs = table_cell_runs(row, col, row_index < header_rows);
        row_height = std::max(
            row_height,
            lines_height(wrap_runs(
                runs,
                content_width,
                style.text_size_pt,
                style.text_leading_pt,
                metrics)) + 2.0 * style.cell_padding_pt);
    }
    return row_height;
}

double table_total_height(
    const Table_block& table,
    const Table_columns& columns,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    double height = 0.0;
    for (int row = 0; row < static_cast<int>(table.rows.size()); ++row) {
        height += table_row_height(table, row, columns, style, metrics);
    }
    return height;
}

int cell_line_count(
    const std::vector<Inline_run>& runs,
    double content_width_pt,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    return static_cast<int>(wrap_runs(
        runs,
        content_width_pt,
        style.text_size_pt,
        style.text_leading_pt,
        metrics).size());
}

double min_content_width_for_line_count(
    const std::vector<Inline_run>& runs,
    double current_width_pt,
    double max_width_pt,
    int target_lines,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    if (cell_line_count(runs, max_width_pt, style, metrics) > target_lines) {
        return -1.0;
    }

    double lo = current_width_pt;
    double hi = max_width_pt;
    for (int step = 0; step < 18; ++step) {
        const double mid = (lo + hi) * 0.5;
        if (cell_line_count(runs, mid, style, metrics) <= target_lines) {
            hi = mid;
        }
        else {
            lo = mid;
        }
    }
    return hi;
}

bool candidate_is_better(
    const Table_columns& candidate,
    double candidate_height,
    const Table_columns& best,
    double best_height)
{
    static constexpr double k_height_epsilon_pt = 0.03;
    static constexpr double k_width_epsilon_pt = 0.03;

    if (candidate_height + k_height_epsilon_pt < best_height) {
        return true;
    }
    if (candidate_height > best_height + k_height_epsilon_pt) {
        return false;
    }
    return width_sum(candidate.widths_pt) + k_width_epsilon_pt < width_sum(best.widths_pt);
}

Table_columns optimize_columns(
    const Table_block& table,
    Table_columns columns,
    double available_width_pt,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    static constexpr double k_height_epsilon_pt = 0.03;
    static constexpr double k_width_epsilon_pt = 0.03;

    for (int iteration = 0; iteration < 64; ++iteration) {
        const double current_height = table_total_height(table, columns, style, metrics);
        const double slack = available_width_pt - width_sum(columns.widths_pt);
        if (slack <= k_width_epsilon_pt) {
            break;
        }

        Table_columns best = columns;
        double best_height = current_height;

        for (int col = 0; col < columns.column_count; ++col) {
            Table_columns full = columns;
            full.widths_pt[col] += slack;
            if (table_total_height(table, full, style, metrics) + k_height_epsilon_pt
                >= current_height)
            {
                continue;
            }

            double lo = 0.0;
            double hi = slack;
            for (int step = 0; step < 18; ++step) {
                const double mid = (lo + hi) * 0.5;
                Table_columns probe = columns;
                probe.widths_pt[col] += mid;
                if (table_total_height(table, probe, style, metrics) + k_height_epsilon_pt
                    < current_height)
                {
                    hi = mid;
                }
                else {
                    lo = mid;
                }
            }

            Table_columns candidate = columns;
            candidate.widths_pt[col] += hi;
            const double candidate_height = table_total_height(table, candidate, style, metrics);
            if (candidate_is_better(candidate, candidate_height, best, best_height)) {
                best = std::move(candidate);
                best_height = candidate_height;
            }
        }

        const int header_rows = table.has_header ? 1 : 0;
        for (int row_index = 0; row_index < static_cast<int>(table.rows.size()); ++row_index) {
            std::vector<int> line_counts(columns.column_count, 0);
            int max_lines = 0;
            for (int col = 0; col < columns.column_count; ++col) {
                const auto runs = table_cell_runs(
                    table.rows[row_index],
                    col,
                    row_index < header_rows);
                const double content_width = columns.widths_pt[col] - 2.0 * style.cell_padding_pt;
                line_counts[col] = cell_line_count(runs, content_width, style, metrics);
                max_lines = std::max(max_lines, line_counts[col]);
            }

            for (int target_lines = max_lines - 1; target_lines >= 1; --target_lines) {
                Table_columns candidate = columns;
                bool possible = true;
                for (int col = 0; col < columns.column_count; ++col) {
                    if (line_counts[col] <= target_lines) {
                        continue;
                    }

                    const auto runs = table_cell_runs(
                        table.rows[row_index],
                        col,
                        row_index < header_rows);
                    const double current_content_width =
                        candidate.widths_pt[col] - 2.0 * style.cell_padding_pt;
                    const double required_content_width = min_content_width_for_line_count(
                        runs,
                        current_content_width,
                        current_content_width + slack,
                        target_lines,
                        style,
                        metrics);
                    if (required_content_width < 0.0) {
                        possible = false;
                        break;
                    }
                    candidate.widths_pt[col] = std::max(
                        candidate.widths_pt[col],
                        required_content_width + 2.0 * style.cell_padding_pt);
                }

                if (!possible || width_sum(candidate.widths_pt) > available_width_pt) {
                    continue;
                }

                const double candidate_height = table_total_height(
                    table,
                    candidate,
                    style,
                    metrics);
                if (candidate_is_better(candidate, candidate_height, best, best_height)) {
                    best = std::move(candidate);
                    best_height = candidate_height;
                }
            }
        }

        if (best_height + k_height_epsilon_pt >= current_height) {
            break;
        }
        columns = std::move(best);
    }
    return columns;
}

} // namespace

Table_columns compute_table_columns(
    const Table_block& table,
    double available_width_pt,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    Table_columns columns;
    if (table.rows.empty()) {
        return columns;
    }

    for (const auto& row : table.rows) {
        columns.column_count = std::max(columns.column_count, static_cast<int>(row.cells.size()));
    }
    if (columns.column_count == 0) {
        return columns;
    }

    const int header_rows = table.has_header ? 1 : 0;
    const double pad = 2.0 * style.cell_padding_pt;
    std::vector<double> min_widths(columns.column_count, 0.0);
    std::vector<double> pref_widths(columns.column_count, 0.0);

    for (int row_index = 0; row_index < static_cast<int>(table.rows.size()); ++row_index) {
        const auto& row = table.rows[row_index];
        for (int col = 0; col < static_cast<int>(row.cells.size()); ++col) {
            const auto runs = table_cell_runs(row, col, row_index < header_rows);
            min_widths[col] = std::max(
                min_widths[col],
                cell_min_width(runs, style.text_size_pt, metrics) + pad);
            pref_widths[col] = std::max(
                pref_widths[col],
                cell_preferred_width(runs, style.text_size_pt, metrics) + pad);
        }
    }

    if (width_sum(min_widths) > available_width_pt) {
        return columns;
    }

    columns.valid = true;
    const double total_pref = width_sum(pref_widths);
    if (total_pref <= available_width_pt) {
        columns.widths_pt = pref_widths;
    }
    else {
        const double total_min = width_sum(min_widths);
        const double shrinkable = total_pref - total_min;
        const double excess = total_pref - available_width_pt;
        columns.widths_pt.resize(columns.column_count);
        for (int col = 0; col < columns.column_count; ++col) {
            const double room = pref_widths[col] - min_widths[col];
            const double reduction = shrinkable > 0.0 ? excess * (room / shrinkable) : 0.0;
            columns.widths_pt[col] = pref_widths[col] - reduction;
        }
    }

    return optimize_columns(table, std::move(columns), available_width_pt, style, metrics);
}

Table_row_layout layout_table_row(
    const Table_block& table,
    int row_index,
    const Table_columns& columns,
    double left_pt,
    double top_pt,
    const table_style_t& style,
    const Measurement_context& metrics)
{
    Table_row_layout result;
    if (!columns.valid || row_index < 0 || row_index >= static_cast<int>(table.rows.size())) {
        return result;
    }

    result.height_pt = table_row_height(table, row_index, columns, style, metrics);
    double table_width = width_sum(columns.widths_pt);
    const bool is_header = table.has_header && row_index == 0;
    if (is_header) {
        result.elements.push_back(table_fill_rect_t{
            left_pt,
            top_pt,
            table_width,
            result.height_pt,
            style.header_fill
        });
    }

    double x = left_pt;
    const auto& row = table.rows[row_index];
    for (int col = 0; col < columns.column_count; ++col) {
        const double cell_x = x + style.cell_padding_pt;
        double cell_y = top_pt + style.cell_padding_pt;
        const auto runs = table_cell_runs(row, col, is_header);
        for (const auto& line : wrap_runs(
                 runs,
                 columns.widths_pt[col] - 2.0 * style.cell_padding_pt,
                 style.text_size_pt,
                 style.text_leading_pt,
                 metrics))
        {
            double text_x = cell_x;
            for (const auto& [text, inline_style] : line.spans) {
                const Pdf_font font = font_for(inline_style);
                result.elements.push_back(Table_text_span{
                    text_x,
                    cell_y,
                    text,
                    font,
                    style.text_size_pt,
                    style.text_color
                });
                text_x += metrics.measure_text_width(font, text, style.text_size_pt);
            }
            cell_y += line.height_pt;
        }
        x += columns.widths_pt[col];
    }

    result.elements.push_back(table_line_t{
        left_pt, top_pt, left_pt + table_width, top_pt,
        style.border_width_pt, style.border_color
    });
    result.elements.push_back(table_line_t{
        left_pt, top_pt + result.height_pt, left_pt + table_width, top_pt + result.height_pt,
        style.border_width_pt, style.border_color
    });

    x = left_pt;
    for (int col = 0; col <= columns.column_count; ++col) {
        result.elements.push_back(table_line_t{
            x, top_pt, x, top_pt + result.height_pt,
            style.border_width_pt, style.border_color
        });
        if (col < columns.column_count) {
            x += columns.widths_pt[col];
        }
    }

    return result;
}

} // namespace mark2haru
