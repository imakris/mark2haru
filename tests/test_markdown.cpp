#include <mark2haru/markdown.h>

#include <mark2haru/utf8_decode.h>

#include <climits>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using mark2haru::Inline_style;
using mark2haru::Block;
using mark2haru::Code_block;
using mark2haru::Heading_block;
using mark2haru::Image_content_block;
using mark2haru::Inline_run;
using mark2haru::List_block;
using mark2haru::Page_break_block;
using mark2haru::Paragraph_block;
using mark2haru::parse_markdown;
using mark2haru::Table_block;
using mark2haru::Thematic_break_block;

const Paragraph_block* expect_single_paragraph(const std::vector<Block>& blocks)
{
    if (blocks.size() != 1) {
        std::fprintf(stderr, "expected 1 block, got %zu\n", blocks.size());
        return nullptr;
    }
    return std::get_if<Paragraph_block>(&blocks[0]);
}

std::string flatten_runs(const std::vector<Inline_run>& runs)
{
    std::string out;
    for (const auto& run : runs) {
        out += run.text;
    }
    return out;
}

bool expect_text_equals(const std::vector<Inline_run>& runs, const std::string& expected)
{
    const std::string actual = flatten_runs(runs);
    if (actual != expected) {
        std::fprintf(
            stderr,
            "text mismatch:\nexpected: %s\nactual:   %s\n",
            expected.c_str(), actual.c_str());
        return false;
    }
    return true;
}

std::string byte_string(std::initializer_list<unsigned int> values)
{
    std::string out;
    out.reserve(values.size());
    for (const unsigned int value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

bool expect_codepoints(
    const std::string&                 text,
    const std::vector<std::uint32_t>&  expected)
{
    const std::vector<std::uint32_t> actual = mark2haru::utf8::decode(text);
    if (actual != expected) {
        std::fprintf(
            stderr,
            "unexpected UTF-8 decode size/content: expected %zu codepoints, got %zu\n",
            expected.size(), actual.size());
        return false;
    }
    return true;
}

bool expect_piece_sizes(const std::string& text, const std::vector<std::size_t>& expected)
{
    const std::vector<std::string> actual = mark2haru::utf8::split_pieces(text);
    if (actual.size() != expected.size()) {
        std::fprintf(
            stderr,
            "unexpected UTF-8 piece count: expected %zu, got %zu\n",
            expected.size(), actual.size());
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (actual[i].size() != expected[i]) {
            std::fprintf(
                stderr,
                "unexpected UTF-8 piece size at %zu: expected %zu, got %zu\n",
                i, expected[i], actual[i].size());
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    {
        const auto  blocks = parse_markdown(R"(Escaped \*star\* and \_under\_ and \\ slash.)");
        const auto* para   = expect_single_paragraph(blocks);
        if (!para) {
            return 1;
        }
        if (!expect_text_equals(para->runs, R"(Escaped *star* and _under_ and \ slash.)")) {
            return 2;
        }
        for (const auto& run : para->runs) {
            if (run.style != Inline_style::NORMAL) {
                std::fprintf(stderr, "unexpected styled run in escape case\n");
                return 3;
            }
        }
    }

    {
        const auto  blocks = parse_markdown("Use ``code ` with tick`` here.");
        const auto* para   = expect_single_paragraph(blocks);
        if (!para) {
            return 4;
        }
        if (para->runs.size() != 3) {
            std::fprintf(
                stderr,
                "expected 3 runs in code-span case, got %zu\n",
                para->runs.size());
            return 5;
        }
        if (para->runs[0].text != "Use " || para->runs[0].style != Inline_style::NORMAL) {
            std::fprintf(stderr, "unexpected first run in code-span case\n");
            return 6;
        }
        if (para->runs[1].text != "code ` with tick" || para->runs[1].style != Inline_style::CODE) {
            std::fprintf(stderr, "unexpected code run in code-span case\n");
            return 7;
        }
        if (para->runs[2].text != " here." || para->runs[2].style != Inline_style::NORMAL) {
            std::fprintf(stderr, "unexpected trailing run in code-span case\n");
            return 8;
        }
    }

    {
        const auto  blocks = parse_markdown("[doc](https://example.com/a_(b))");
        const auto* para   = expect_single_paragraph(blocks);
        if (!para) {
            return 9;
        }
        if (!expect_text_equals(para->runs, "doc (https://example.com/a_(b))")) {
            return 10;
        }
    }

    {
        const auto blocks = parse_markdown("# Heading\n");
        if (blocks.size() != 1) {
            std::fprintf(
                stderr,
                "expected 1 block for trailing-newline heading, got %zu\n",
                blocks.size());
            return 11;
        }
        const auto* heading = std::get_if<Heading_block>(&blocks[0]);
        if (!heading) {
            std::fprintf(stderr, "expected heading block for trailing-newline case\n");
            return 12;
        }
        if (heading->level != 1 || !expect_text_equals(heading->runs, "Heading")) {
            return 13;
        }
    }

    {
        // Bulleted list with a continuation line indented by a single space.
        const auto blocks = parse_markdown(
            "- first item\n"
            "  continued\n"
            "- second item\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 list block, got %zu\n", blocks.size());
            return 14;
        }
        const auto* list = std::get_if<List_block>(&blocks[0]);
        if (!list || list->ordered || list->items.size() != 2) {
            std::fprintf(stderr, "unexpected list shape\n");
            return 15;
        }
        if (!expect_text_equals(list->items[0].runs, "first item continued")) { return 16; }
        if (!expect_text_equals(list->items[1].runs, "second item"))          { return 17; }
    }

    {
        // Ordered list starting at a non-1 number preserves the starting value
        // and counts each item correctly.
        const auto blocks = parse_markdown(
            "3. three\n"
            "4. four\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 ordered list block, got %zu\n", blocks.size());
            return 18;
        }
        const auto* list = std::get_if<List_block>(&blocks[0]);
        if (!list || !list->ordered || list->start_number != 3 || list->items.size() != 2) {
            std::fprintf(stderr, "unexpected ordered list shape\n");
            return 19;
        }
        if (!expect_text_equals(list->items[0].runs, "three")) {
            return 20;
        }
    }

    {
        const auto blocks = parse_markdown("999999999999999999999999999999. Large\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 overflowing ordered list block, got %zu\n", blocks.size());
            return 21;
        }
        const auto* list = std::get_if<List_block>(&blocks[0]);
        if (!list || !list->ordered || list->start_number != INT_MAX || list->items.size() != 1) {
            std::fprintf(stderr, "unexpected overflowing ordered list shape\n");
            return 22;
        }
        if (!expect_text_equals(list->items[0].runs, "Large")) {
            return 23;
        }
    }

    {
        // Fenced code block preserves interior whitespace and does not
        // parse inline markup inside the body.
        const auto blocks = parse_markdown(
            "```text\n"
            "line one\n"
            "  *not italic*\n"
            "```\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 code block, got %zu\n", blocks.size());
            return 24;
        }
        const auto* code = std::get_if<Code_block>(&blocks[0]);
        if (!code) {
            std::fprintf(stderr, "expected code block\n");
            return 25;
        }
        if (code->text != "line one\n  *not italic*") {
            std::fprintf(stderr, "unexpected code body: '%s'\n", code->text.c_str());
            return 26;
        }
    }

    {
        const auto blocks = parse_markdown("![Alt text](image.png)");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 image block, got %zu\n", blocks.size());
            return 28;
        }
        const auto* image = std::get_if<Image_content_block>(&blocks[0]);
        if (!image || image->path != "image.png" || image->alt_text != "Alt text") {
            std::fprintf(stderr, "unexpected image block\n");
            return 29;
        }
    }

    {
        // A pipe table with a header row and two body rows parses into a
        // table block with has_header set.
        const auto blocks = parse_markdown(
            "| Item | State |\n"
            "| --- | --- |\n"
            "| A | ok |\n"
            "| B | fail |\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 table block, got %zu\n", blocks.size());
            return 30;
        }
        const auto* table = std::get_if<Table_block>(&blocks[0]);
        if (!table || !table->has_header || table->rows.size() != 3) {
            std::fprintf(stderr, "unexpected table shape\n");
            return 31;
        }
        if (table->rows[0].cells.size() != 2 ||
            table->rows[1].cells.size() != 2 ||
            table->rows[2].cells.size() != 2)
        {
            std::fprintf(stderr, "unexpected table column shape\n");
            return 32;
        }
        if (!expect_text_equals(table->rows[0].cells[0].runs, "Item") ||
            !expect_text_equals(table->rows[2].cells[1].runs, "fail"))
        {
            return 33;
        }
    }

    {
        // Explicit page break splits the document into separate blocks.
        const auto blocks = parse_markdown(
            "before\n"
            "\n"
            "<!-- pagebreak -->\n"
            "\n"
            "after\n");
        if (blocks.size() != 3) {
            std::fprintf(stderr, "expected 3 blocks for page-break case, got %zu\n", blocks.size());
            return 34;
        }
        if (!std::get_if<Paragraph_block >(&blocks[0]) ||
            !std::get_if<Page_break_block>(&blocks[1]) ||
            !std::get_if<Paragraph_block >(&blocks[2]))
        {
            std::fprintf(stderr, "unexpected block shape around page break\n");
            return 35;
        }
    }

    {
        // Each of the three thematic-break forms parses as a Thematic_break_block.
        for (const char* form : { "---", "***", "___" }) {
            const auto blocks = parse_markdown(form);
            if (blocks.size() != 1 || !std::get_if<Thematic_break_block>(&blocks[0])) {
                std::fprintf(stderr, "expected thematic break for '%s'\n", form);
                return 36;
            }
        }
    }

    {
        // Spaced forms `- - -` and `* * *` are also thematic breaks, and
        // must win over the bullet-item classification.
        for (const char* form : { "- - -", "* * *", "_ _ _ _" }) {
            const auto blocks = parse_markdown(form);
            if (blocks.size() != 1 || !std::get_if<Thematic_break_block>(&blocks[0])) {
                std::fprintf(stderr, "expected thematic break for '%s'\n", form);
                return 37;
            }
        }
    }

    {
        // Fewer than three markers, mixed markers, or markers with other
        // content are not thematic breaks.
        for (const char* form : { "--", "-*-", "___emphasis___", "- item" }) {
            const auto blocks = parse_markdown(form);
            if (blocks.empty() || std::get_if<Thematic_break_block>(&blocks[0])) {
                std::fprintf(stderr, "did not expect thematic break for '%s'\n", form);
                return 38;
            }
        }
    }

    {
        // A thematic break between two paragraphs flushes the first.
        const auto blocks = parse_markdown(
            "before\n"
            "\n"
            "---\n"
            "\n"
            "after\n");
        if (blocks.size() != 3                             ||
            !std::get_if<Paragraph_block     >(&blocks[0]) ||
            !std::get_if<Thematic_break_block>(&blocks[1]) ||
            !std::get_if<Paragraph_block     >(&blocks[2]))
        {
            std::fprintf(stderr, "unexpected block shape around thematic break\n");
            return 39;
        }
    }

    {
        const std::string valid = byte_string({
            0x61,
            0xC2, 0xA2,
            0xE2, 0x82, 0xAC,
            0xF0, 0x9F, 0x98, 0x80,
            0x62
        });
        if (!expect_codepoints(valid, { 0x61, 0x00A2, 0x20AC, 0x1F600, 0x62 })) {
            return 40;
        }
        if (!expect_piece_sizes(valid, { 1, 2, 3, 4, 1 })) {
            return 41;
        }
    }

    {
        if (!expect_codepoints(byte_string({ 0xC0, 0xAF }), { '?', '?' })) {
            return 42;
        }
        if (!expect_codepoints(byte_string({ 0xE0, 0x80, 0x80 }), { '?', '?', '?' })) {
            return 43;
        }
        if (!expect_codepoints(byte_string({ 0xED, 0xA0, 0x80 }), { '?', '?', '?' })) {
            return 44;
        }
        if (!expect_codepoints(byte_string({ 0xF4, 0x90, 0x80, 0x80 }), { '?', '?', '?', '?' })) {
            return 45;
        }
        if (!expect_codepoints(byte_string({ 0xE2, 0x28, 0xA1 }), { '?', '(', '?' })) {
            return 46;
        }
        if (!expect_codepoints(byte_string({ 0xF0, 0x9F, 0x98 }), { '?', '?', '?' })) {
            return 47;
        }
        if (!expect_piece_sizes(byte_string({ 0xF0, 0x9F, 0x98 }), { 1, 1, 1 })) {
            return 48;
        }
    }

    {
        // Only the line right after the header is the delimiter row. A later
        // line made of dashes and pipes is ordinary body data, not structure.
        const auto blocks = parse_markdown(
            "| Item | Result |\n"
            "| --- | --- |\n"
            "| - | - |\n"
            "| done | 1 |\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 placeholder table block, got %zu\n", blocks.size());
            return 49;
        }
        const auto* table = std::get_if<Table_block>(&blocks[0]);
        if (!table || !table->has_header || table->rows.size() != 3) {
            std::fprintf(stderr, "placeholder row was dropped from the table\n");
            return 50;
        }
        if (table->rows[1].cells.size() != 2 ||
            !expect_text_equals(table->rows[1].cells[0].runs, "-") ||
            !expect_text_equals(table->rows[1].cells[1].runs, "-") ||
            !expect_text_equals(table->rows[2].cells[0].runs, "done"))
        {
            return 51;
        }
    }

    {
        // Delimiter-shaped lines after the real delimiter stay body rows, in
        // order, and do not change which row is the header.
        const auto blocks = parse_markdown(
            "| Item | Result |\n"
            "| --- | --- |\n"
            "| --- | --- |\n"
            "| :-- | --: |\n"
            "| a | b |\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 repeated-delimiter table block, got %zu\n", blocks.size());
            return 52;
        }
        const auto* table = std::get_if<Table_block>(&blocks[0]);
        if (!table || !table->has_header || table->rows.size() != 4) {
            std::fprintf(stderr, "delimiter-shaped body rows were dropped from the table\n");
            return 53;
        }
        if (!expect_text_equals(table->rows[0].cells[0].runs, "Item") ||
            !expect_text_equals(table->rows[1].cells[0].runs, "---") ||
            !expect_text_equals(table->rows[2].cells[0].runs, ":--") ||
            !expect_text_equals(table->rows[2].cells[1].runs, "--:") ||
            !expect_text_equals(table->rows[3].cells[1].runs, "b"))
        {
            return 54;
        }
    }

    {
        // A placeholder row at the end of a table is kept, and the table still
        // ends at the first line that is not a table line.
        const auto blocks = parse_markdown(
            "| Item | Result |\n"
            "| --- | --- |\n"
            "| - | - |\n"
            "\n"
            "after\n");
        if (blocks.size() != 2) {
            std::fprintf(stderr, "expected table and paragraph, got %zu blocks\n", blocks.size());
            return 55;
        }
        const auto* table     = std::get_if<Table_block>(&blocks[0]);
        const auto* paragraph = std::get_if<Paragraph_block>(&blocks[1]);
        if (!table || !table->has_header || table->rows.size() != 2) {
            std::fprintf(stderr, "trailing placeholder row was dropped from the table\n");
            return 56;
        }
        if (!paragraph || !expect_text_equals(paragraph->runs, "after")) {
            return 57;
        }
    }

    return 0;
}
