#pragma once

#include <mark2haru/font_context.h>
#include <mark2haru/markdown.h>
#include <mark2haru/pdf_writer.h>

#include <string>
#include <variant>
#include <vector>

namespace mark2haru
{

struct table_style_t
{
    double text_size_pt     = 10.0;
    double text_leading_pt  = 12.0;
    double cell_padding_pt  = 5.67;
    double border_width_pt  = 0.5;
    color_t text_color      = { 0.0, 0.0, 0.0 };
    color_t border_color    = { 0.5, 0.5, 0.5 };
    color_t header_fill     = { 0.94, 0.94, 0.94 };
};

struct Table_text_span
{
    double x_pt = 0.0;
    double y_pt = 0.0;
    std::string text;
    Pdf_font font = Pdf_font::REGULAR;
    double size_pt = 10.0;
    color_t color;
};

struct table_line_t
{
    double x1_pt = 0.0;
    double y1_pt = 0.0;
    double x2_pt = 0.0;
    double y2_pt = 0.0;
    double width_pt = 0.5;
    color_t color;
};

struct table_fill_rect_t
{
    double x_pt = 0.0;
    double y_pt = 0.0;
    double width_pt = 0.0;
    double height_pt = 0.0;
    color_t color;
};

using Table_element = std::variant<Table_text_span, table_line_t, table_fill_rect_t>;

struct Table_columns
{
    int column_count = 0;
    std::vector<double> widths_pt;
    bool valid = false;
};

struct Table_row_layout
{
    std::vector<Table_element> elements;
    double height_pt = 0.0;
};

Table_columns compute_table_columns(
    const Table_block& table,
    double available_width_pt,
    const table_style_t& style,
    const Measurement_context& metrics);

Table_row_layout layout_table_row(
    const Table_block& table,
    int row_index,
    const Table_columns& columns,
    double left_pt,
    double top_pt,
    const table_style_t& style,
    const Measurement_context& metrics);

} // namespace mark2haru
