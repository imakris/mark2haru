#include <mark2haru/text_layout.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using mark2haru::Inline_run;
using mark2haru::Inline_style;
using mark2haru::text_layout::Line;
using mark2haru::Pdf_font;
using mark2haru::text_layout::wrap_runs;

// One point per code point in every font, so the expected line breaks can be
// read straight off the text.
double measure(Pdf_font, const std::string& text, double size_pt)
{
    std::size_t code_points = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++code_points;
        }
    }
    return static_cast<double>(code_points) * size_pt;
}

std::vector<Line> wrap(const std::vector<Inline_run>& runs, double width_pt)
{
    return wrap_runs(runs, width_pt, 1.0, 1.0, measure);
}

std::string line_text(const Line& line)
{
    std::string text;
    for (const auto& span : line.spans) {
        text += span.text;
    }
    return text;
}

bool expect_lines(
    const char*                     name,
    const std::vector<Line>&        lines,
    const std::vector<std::string>& expected)
{
    bool ok = lines.size() == expected.size();
    for (std::size_t i = 0; ok && i < lines.size(); ++i) {
        ok = line_text(lines[i]) == expected[i];
    }
    if (!ok) {
        std::fprintf(stderr, "%s: got %zu lines:", name, lines.size());
        for (const auto& line : lines) {
            std::fprintf(stderr, " [%s]", line_text(line).c_str());
        }
        std::fprintf(stderr, "\n");
    }
    return ok;
}

bool expect_style_change_inside_word_does_not_break()
{
    // "go pre**fix**" in a line that holds "prefix" but not "go prefix".
    const auto lines = wrap({ { "go pre" }, { "fix", Inline_style::BOLD } }, 7.0);
    if (!expect_lines("style change inside word", lines, { "go", "prefix" })) {
        return false;
    }
    const Line& second = lines[1];
    if (second.spans.size() != 2
        || second.spans[0].text != "pre" || second.spans[0].style != Inline_style::NORMAL
        || second.spans[1].text != "fix" || second.spans[1].style != Inline_style::BOLD
        || second.spans[1].x_offset_pt != 3.0
        || second.width_pt != 6.0)
    {
        std::fprintf(stderr, "styled word kept its fragments but not their offsets\n");
        return false;
    }
    return true;
}

bool expect_punctuation_after_styled_text_stays_attached()
{
    // Code span directly followed by a comma: the comma must not be left alone
    // at the start of the next line.
    return expect_lines(
        "punctuation after code",
        wrap({ { "a " }, { "bb", Inline_style::CODE }, { ",", Inline_style::NORMAL } }, 4.0),
        { "a", "bb," });
}

bool expect_three_styles_form_one_word()
{
    return expect_lines(
        "three styles",
        wrap({ { "a ab" }, { "cd", Inline_style::BOLD }, { "ef", Inline_style::ITALIC } }, 6.0),
        { "a", "abcdef" });
}

bool expect_exact_fit_stays_on_one_line()
{
    const auto lines = wrap({ { "pre" }, { "fix", Inline_style::BOLD } }, 6.0);
    return expect_lines("exact fit", lines, { "prefix" }) && lines[0].width_pt == 6.0;
}

bool expect_overlong_styled_word_fills_lines_across_styles()
{
    // A word wider than the line starts a line of its own and is then broken by
    // code point, whatever the style of the piece being placed.
    const auto lines = wrap({ { "zz abc" }, { "defgh", Inline_style::BOLD } }, 5.0);
    if (!expect_lines("overlong styled word", lines, { "zz", "abcde", "fgh" })) {
        return false;
    }
    if (lines[1].spans.size() != 2 || lines[1].spans[1].style != Inline_style::BOLD
        || lines[1].spans[1].x_offset_pt != 3.0)
    {
        std::fprintf(stderr, "overlong styled word lost its style boundary on the full line\n");
        return false;
    }
    return true;
}

bool expect_multibyte_fragments_stay_whole()
{
    return expect_lines(
        "multibyte fragments",
        wrap({ { "x \xCE\xB1" }, { "\xCE\xB2\xCE\xB3", Inline_style::BOLD } }, 3.0),
        { "x", "\xCE\xB1\xCE\xB2\xCE\xB3" });
}

bool expect_whitespace_and_newlines_still_break()
{
    return expect_lines(
        "whitespace",
        wrap({ { "  aa\t\tbb\n\ncc  " } }, 10.0),
        { "aa bb", "", "cc" })
        && expect_lines(
            "space between styles",
            wrap({ { "ab" }, { " cd", Inline_style::BOLD } }, 3.0),
            { "ab", "cd" })
        && expect_lines(
            "newline between styles",
            wrap({ { "ab" }, { "\ncd", Inline_style::BOLD } }, 10.0),
            { "ab", "cd" });
}

bool expect_plain_overlong_word_unchanged()
{
    return expect_lines(
        "plain overlong word",
        wrap({ { "ab abcdefg" } }, 3.0),
        { "ab", "abc", "def", "g" });
}

} // namespace

int main()
{
    int result = 0;
    auto run = [&result](bool ok, int code) {
        if (!ok && result == 0) {
            result = code;
        }
    };

    run(expect_style_change_inside_word_does_not_break(), 1);
    run(expect_punctuation_after_styled_text_stays_attached(), 2);
    run(expect_three_styles_form_one_word(), 3);
    run(expect_exact_fit_stays_on_one_line(), 4);
    run(expect_overlong_styled_word_fills_lines_across_styles(), 5);
    run(expect_multibyte_fragments_stay_whole(), 6);
    run(expect_whitespace_and_newlines_still_break(), 7);
    run(expect_plain_overlong_word_unchanged(), 8);
    return result;
}
