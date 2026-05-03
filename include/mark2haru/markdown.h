#pragma once

#include <string>
#include <variant>
#include <vector>

namespace mark2haru
{

enum class Inline_style
{
    NORMAL,
    BOLD,
    ITALIC,
    BOLD_ITALIC,
    CODE,
};

struct Inline_run
{
    std::string text;
    Inline_style style = Inline_style::NORMAL;
};

struct Paragraph_block
{
    std::vector<Inline_run> runs;
};

struct Heading_block
{
    int level = 1;
    std::vector<Inline_run> runs;
};

struct List_item
{
    std::vector<Inline_run> runs;
};

struct List_block
{
    bool ordered     = false;
    int start_number = 1;
    std::vector<List_item> items;
};

struct Code_block
{
    std::string language;
    std::string text;
};

struct Image_content_block
{
    std::string path;
    std::string alt_text;
};

struct Table_cell
{
    std::vector<Inline_run> runs;
};

struct Table_row
{
    std::vector<Table_cell> cells;
};

struct Table_block
{
    std::vector<Table_row> rows;
    bool has_header = false;
};

struct Page_break_block
{
};

using Block = std::variant<
    Paragraph_block,
    Heading_block,
    List_block,
    Image_content_block,
    Code_block,
    Table_block,
    Page_break_block>;

std::vector<Block> parse_markdown(const std::string& input);

} // namespace mark2haru
