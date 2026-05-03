#include <mark2haru/markdown.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using mark2haru::Inline_style;
using mark2haru::Block;
using mark2haru::Code_block;
using mark2haru::Heading_block;
using mark2haru::Inline_run;
using mark2haru::List_block;
using mark2haru::Page_break_block;
using mark2haru::Paragraph_block;
using mark2haru::parse_markdown;
using mark2haru::Table_block;

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

} // namespace

int main()
{
    {
        const auto blocks = parse_markdown(R"(Escaped \*star\* and \_under\_ and \\ slash.)");
        const auto* para = expect_single_paragraph(blocks);
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
        const auto blocks = parse_markdown("Use ``code ` with tick`` here.");
        const auto* para = expect_single_paragraph(blocks);
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
        const auto blocks = parse_markdown("[doc](https://example.com/a_(b))");
        const auto* para = expect_single_paragraph(blocks);
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
        if (!expect_text_equals(list->items[0].runs, "first item continued")) {
            return 16;
        }
        if (!expect_text_equals(list->items[1].runs, "second item")) {
            return 17;
        }
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
        // Fenced code block preserves interior whitespace and does not
        // parse inline markup inside the body.
        const auto blocks = parse_markdown(
            "```text\n"
            "line one\n"
            "  *not italic*\n"
            "```\n");
        if (blocks.size() != 1) {
            std::fprintf(stderr, "expected 1 code block, got %zu\n", blocks.size());
            return 21;
        }
        const auto* code = std::get_if<Code_block>(&blocks[0]);
        if (!code) {
            std::fprintf(stderr, "expected code block\n");
            return 22;
        }
        if (code->language != "text") {
            std::fprintf(stderr, "unexpected code language: '%s'\n", code->language.c_str());
            return 23;
        }
        if (code->text != "line one\n  *not italic*") {
            std::fprintf(stderr, "unexpected code body: '%s'\n", code->text.c_str());
            return 24;
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
            return 25;
        }
        const auto* table = std::get_if<Table_block>(&blocks[0]);
        if (!table || !table->has_header || table->rows.size() != 3) {
            std::fprintf(stderr, "unexpected table shape\n");
            return 26;
        }
        if (table->rows[0].cells.size() != 2 ||
            table->rows[1].cells.size() != 2 ||
            table->rows[2].cells.size() != 2)
        {
            std::fprintf(stderr, "unexpected table column shape\n");
            return 27;
        }
        if (!expect_text_equals(table->rows[0].cells[0].runs, "Item") ||
            !expect_text_equals(table->rows[2].cells[1].runs, "fail"))
        {
            return 28;
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
            return 29;
        }
        if (!std::get_if<Paragraph_block >(&blocks[0]) ||
            !std::get_if<Page_break_block>(&blocks[1]) ||
            !std::get_if<Paragraph_block >(&blocks[2]))
        {
            std::fprintf(stderr, "unexpected block shape around page break\n");
            return 30;
        }
    }

    return 0;
}
