#pragma once

#include <string>
#include <variant>
#include <vector>

namespace mark2haru {

enum class InlineStyle {
    Normal,
    Bold,
    Italic,
    BoldItalic,
    Code,
};

struct InlineRun {
    std::string text;
    InlineStyle style = InlineStyle::Normal;
};

struct ParagraphBlock {
    std::vector<InlineRun> runs;
};

struct HeadingBlock {
    int level = 1;
    std::vector<InlineRun> runs;
};

struct ListItem {
    std::vector<InlineRun> runs;
};

struct ListBlock {
    bool ordered = false;
    int start_number = 1;
    std::vector<ListItem> items;
};

struct CodeBlock {
    std::string language;
    std::string text;
};

struct TableCell {
    std::vector<InlineRun> runs;
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct TableBlock {
    std::vector<TableRow> rows;
    bool has_header = false;
};

struct PageBreakBlock {
};

using Block = std::variant<ParagraphBlock, HeadingBlock, ListBlock, CodeBlock, TableBlock, PageBreakBlock>;

std::vector<Block> parse_markdown(const std::string& input);

} // namespace mark2haru
