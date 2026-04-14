#include <mark2haru/markdown.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using mark2haru::Inline_style;
using mark2haru::block_t;
using mark2haru::heading_block_t;
using mark2haru::inline_run_t;
using mark2haru::paragraph_block_t;
using mark2haru::parse_markdown;

const paragraph_block_t* expect_single_paragraph(const std::vector<block_t>& blocks)
{
    if (blocks.size() != 1) {
        std::fprintf(stderr, "expected 1 block, got %zu\n", blocks.size());
        return nullptr;
    }
    return std::get_if<paragraph_block_t>(&blocks[0]);
}

std::string flatten_runs(const std::vector<inline_run_t>& runs)
{
    std::string out;
    for (const auto& run : runs) {
        out += run.text;
    }
    return out;
}

bool expect_text_equals(const std::vector<inline_run_t>& runs, const std::string& expected)
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
        const auto* heading = std::get_if<heading_block_t>(&blocks[0]);
        if (!heading) {
            std::fprintf(stderr, "expected heading block for trailing-newline case\n");
            return 12;
        }
        if (heading->level != 1 || !expect_text_equals(heading->runs, "Heading")) {
            return 13;
        }
    }

    return 0;
}
