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

struct inline_run_t
{
    std::string text;
    Inline_style style = Inline_style::NORMAL;
};

struct paragraph_block_t
{
    std::vector<inline_run_t> runs;
};

struct heading_block_t
{
    int level = 1;
    std::vector<inline_run_t> runs;
};

struct list_item_t
{
    std::vector<inline_run_t> runs;
};

struct list_block_t
{
    bool ordered     = false;
    int start_number = 1;
    std::vector<list_item_t> items;
};

struct code_block_t
{
    std::string language;
    std::string text;
};

struct table_cell_t
{
    std::vector<inline_run_t> runs;
};

struct table_row_t
{
    std::vector<table_cell_t> cells;
};

struct table_block_t
{
    std::vector<table_row_t> rows;
    bool has_header = false;
};

struct page_break_block_t
{
};

using block_t = std::variant<
    paragraph_block_t,
    heading_block_t,
    list_block_t,
    code_block_t,
    table_block_t,
    page_break_block_t>;

std::vector<block_t> parse_markdown(const std::string& input);

} // namespace mark2haru
