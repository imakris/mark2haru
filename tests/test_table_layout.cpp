#include <mark2haru/table_layout.h>

#include "test_common.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

using mark2haru::color_t;
using mark2haru::Inline_run;
using mark2haru::Inline_style;
using mark2haru::Measurement_context;
using mark2haru::Pdf_font;
using mark2haru::Table_block;
using mark2haru::Table_cell;
using mark2haru::Table_columns;
using mark2haru::Table_element;
using mark2haru::Table_row;
using mark2haru::Table_row_layout;
using mark2haru::Table_text_span;
using mark2haru::compute_table_columns;
using mark2haru::layout_table_row;
using mark2haru::table_fill_rect_t;
using mark2haru::table_style_t;

constexpr double k_epsilon_pt = 0.001;

bool nearly_equal(double a, double b)
{
    return std::fabs(a - b) <= k_epsilon_pt;
}

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        return false;
    }
    return true;
}

Table_cell cell(std::initializer_list<Inline_run> runs)
{
    Table_cell out;
    out.runs.assign(runs.begin(), runs.end());
    return out;
}

Table_cell text_cell(const std::string& text, Inline_style style = Inline_style::NORMAL)
{
    return cell({ { text, style } });
}

Table_row row(std::initializer_list<Table_cell> cells)
{
    Table_row out;
    out.cells.assign(cells.begin(), cells.end());
    return out;
}

double width_sum(const std::vector<double>& widths_pt)
{
    double total = 0.0;
    for (double width_pt : widths_pt) {
        total += width_pt;
    }
    return total;
}

bool has_header_fill(const Table_row_layout& layout)
{
    for (const Table_element& element : layout.elements) {
        if (std::get_if<table_fill_rect_t>(&element)) {
            return true;
        }
    }
    return false;
}

const Table_text_span* find_text_span(
    const Table_row_layout&   layout,
    const std::string&        text)
{
    for (const Table_element& element : layout.elements) {
        const auto* span = std::get_if<Table_text_span>(&element);
        if (span && span->text == text) {
            return span;
        }
    }
    return nullptr;
}

std::vector<Table_text_span> text_spans(const Table_row_layout& layout)
{
    std::vector<Table_text_span> spans;
    for (const Table_element& element : layout.elements) {
        if (const auto* span = std::get_if<Table_text_span>(&element)) {
            spans.push_back(*span);
        }
    }
    return spans;
}

bool expect_computed_columns_and_header_layout(const Measurement_context& metrics)
{
    table_style_t style;
    style.text_size_pt    = 10.0;
    style.text_leading_pt = 12.0;
    style.cell_padding_pt =  4.0;

    Table_block table;
    table.has_header = true;
    table.rows = {
        row({
            text_cell("Item"),
            text_cell("State", Inline_style::ITALIC),
            text_cell("Notes"),
        }),
        row({
            text_cell("Alpha"),
            text_cell("ok"),
            text_cell("one two three four five six"),
        }),
        row({
            text_cell("Beta"),
            text_cell("retry"),
            text_cell("short"),
        }),
    };

    constexpr double available_width_pt = 126.0;
    const Table_columns columns = compute_table_columns(
        table,
        available_width_pt,
        style,
        metrics);

    if (!check(columns.valid, "expected table columns to be valid")) {
        return false;
    }
    if (!check(columns.column_count == 3, "expected three computed columns")) {
        return false;
    }
    if (!check(columns.widths_pt.size() == 3, "expected three column widths")) {
        return false;
    }
    if (!check(width_sum(columns.widths_pt) <= available_width_pt + k_epsilon_pt,
            "computed columns exceeded available width"))
    {
        return false;
    }

    const Table_row_layout header = layout_table_row(
        table,
        0,
        columns,
        20.0,
        30.0,
        style,
        metrics);

    if (!check(has_header_fill(header), "expected header layout to include a fill rect")) {
        return false;
    }

    const auto* item_span  = find_text_span(header, "Item");
    const auto* state_span = find_text_span(header, "State");
    if (!check(item_span != nullptr, "expected header Item text span")) {
        return false;
    }
    if (!check(state_span != nullptr, "expected header State text span")) {
        return false;
    }
    if (!check(item_span->font == Pdf_font::BOLD, "normal header text should promote to bold")) {
        return false;
    }
    if (!check(state_span->font == Pdf_font::BOLD_ITALIC,
            "italic header text should promote to bold italic"))
    {
        return false;
    }

    const Table_row_layout body = layout_table_row(
        table,
        1,
        columns,
        20.0,
        60.0,
        style,
        metrics);
    const double single_line_height = style.text_leading_pt + 2.0 * style.cell_padding_pt;
    if (!check(body.height_pt > single_line_height + k_epsilon_pt,
            "expected wrapped body row to be taller than one line"))
    {
        return false;
    }

    return true;
}

bool expect_wrapped_body_positions(const Measurement_context& metrics)
{
    table_style_t style;
    style.text_size_pt    = 10.0;
    style.text_leading_pt = 12.0;
    style.cell_padding_pt =  4.0;

    Table_block table;
    table.rows = {
        row({
            text_cell("Alpha Beta Gamma Delta"),
        }),
    };

    Table_columns columns;
    columns.valid        = true;
    columns.column_count = 1;
    columns.widths_pt    = { 55.0 };

    const Table_row_layout layout = layout_table_row(
        table,
        0,
        columns,
        10.0,
        25.0,
        style,
        metrics);

    const std::vector<Table_text_span> spans = text_spans(layout);
    if (!check(spans.size() >= 3, "expected wrapped cell to produce multiple text spans")) {
        return false;
    }
    if (!check(layout.height_pt >= style.text_leading_pt * 3.0 + 2.0 * style.cell_padding_pt,
            "expected wrapped cell row height to include multiple line heights"))
    {
        return false;
    }
    if (!check(nearly_equal(spans[0].x_pt, 14.0), "unexpected first wrapped span x position")) {
        return false;
    }
    if (!check(nearly_equal(spans[0].y_pt, 29.0), "unexpected first wrapped span y position")) {
        return false;
    }
    if (!check(nearly_equal(spans[1].x_pt, 14.0), "unexpected second wrapped span x position")) {
        return false;
    }
    if (!check(nearly_equal(spans[1].y_pt, 41.0), "unexpected second wrapped span y position")) {
        return false;
    }

    return true;
}

bool expect_inline_span_fonts_and_positions(const Measurement_context& metrics)
{
    table_style_t style;
    style.text_size_pt    = 10.0;
    style.text_leading_pt = 12.0;
    style.cell_padding_pt =  4.0;
    style.text_color      = color_t{ 0.1, 0.2, 0.3 };

    Table_block table;
    table.rows = {
        row({
            cell({
                { "plain", Inline_style::NORMAL },
                { "code",  Inline_style::CODE   },
                { "bold",  Inline_style::BOLD   },
            }),
        }),
    };

    Table_columns columns;
    columns.valid        = true;
    columns.column_count = 1;
    columns.widths_pt    = { 200.0 };

    const Table_row_layout layout = layout_table_row(
        table,
        0,
        columns,
        8.0,
        12.0,
        style,
        metrics);

    const auto* plain = find_text_span(layout, "plain");
    const auto* code  = find_text_span(layout, "code");
    const auto* bold  = find_text_span(layout, "bold");
    if (!check(plain != nullptr, "expected plain text span")) {
        return false;
    }
    if (!check(code != nullptr, "expected code text span")) {
        return false;
    }
    if (!check(bold != nullptr, "expected bold text span")) {
        return false;
    }
    if (!check(plain->font == Pdf_font::REGULAR, "plain span should use regular font")) {
        return false;
    }
    if (!check(code->font == Pdf_font::MONO, "code span should use mono font")) {
        return false;
    }
    if (!check(bold->font == Pdf_font::BOLD, "bold span should use bold font")) {
        return false;
    }
    if (!check(nearly_equal(plain->x_pt, 12.0), "unexpected plain span x position")) {
        return false;
    }
    if (!check(nearly_equal(plain->y_pt, 16.0), "unexpected plain span y position")) {
        return false;
    }
    if (!check(code->x_pt > plain->x_pt, "code span should advance after plain span")) {
        return false;
    }
    if (!check(bold->x_pt > code->x_pt, "bold span should advance after code span")) {
        return false;
    }
    if (!check(nearly_equal(code->y_pt, plain->y_pt), "inline spans should stay on same line")) {
        return false;
    }
    if (!check(nearly_equal(bold->y_pt, plain->y_pt), "inline spans should stay on same line")) {
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 1) {
        return 2;
    }

    const fs::path exe_dir = mark2haru_test::executable_dir(argc, argv);
    auto metrics = mark2haru_test::load_default_metrics(exe_dir);
    if (!metrics) {
        return 3;
    }

    if (!expect_computed_columns_and_header_layout(*metrics)) {
        return 4;
    }
    if (!expect_wrapped_body_positions(*metrics)) {
        return 5;
    }
    if (!expect_inline_span_fonts_and_positions(*metrics)) {
        return 6;
    }

    return 0;
}
