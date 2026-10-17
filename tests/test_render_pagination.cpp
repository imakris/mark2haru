#include <mark2haru/render.h>

#include "miniz.h"
#include "test_common.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string content_stream(const std::string& pdf, const std::string& object_id)
{
    const auto object = pdf.find("\n" + object_id + " 0 obj\n");
    if (object == std::string::npos) {
        return {};
    }
    const auto start = pdf.find("stream\n", object);
    const auto end = pdf.find("\nendstream", start);
    if (start == std::string::npos || end == std::string::npos) {
        return {};
    }
    const auto payload = pdf.substr(start + 7, end - start - 7);
    if (pdf.substr(object, start - object).find("/FlateDecode") == std::string::npos) {
        return payload;
    }
    mz_ulong capacity = 4096;
    for (int attempt = 0; attempt < 10; ++attempt, capacity *= 2) {
        std::string result(capacity, '\0');
        mz_ulong size = capacity;
        if (mz_uncompress(reinterpret_cast<unsigned char*>(result.data()), &size,
                reinterpret_cast<const unsigned char*>(payload.data()),
                static_cast<mz_ulong>(payload.size())) == MZ_OK) {
            result.resize(size);
            return result;
        }
    }
    return {};
}

// Inspect the actual page content streams: every emitted line must be on a
// page, and every input 'x' must still be present. This catches both clipping
// and a pagination implementation that drops or repeats continuation lines.
bool check_pdf(const fs::path& path, const mark2haru::Render_options& options,
    unsigned x_glyph, std::size_t expected_x_count, bool code_boxes, bool multiple_pages)
{
    std::ifstream input(path, std::ios::binary);
    const std::string pdf{std::istreambuf_iterator<char>(input), {}};
    const std::regex page_re(R"(/Contents ([0-9]+) 0 R)");
    const std::regex text_re(
        R"(/F[0-9]+ ([0-9.]+) Tf\n(-?[0-9.]+) (-?[0-9.]+) Td\n<([0-9a-fA-F]*)> Tj)");
    const std::regex rect_re(R"((-?[0-9.]+) (-?[0-9.]+) ([0-9.]+) ([0-9.]+) re)");
    std::size_t pages = 0;
    std::size_t x_count = 0;
    bool ok = true;
    for (auto page = std::sregex_iterator(pdf.begin(), pdf.end(), page_re);
         page != std::sregex_iterator(); ++page) {
        ++pages;
        const auto stream = content_stream(pdf, (*page)[1].str());
        std::size_t text_count = 0;
        for (auto text = std::sregex_iterator(stream.begin(), stream.end(), text_re);
             text != std::sregex_iterator(); ++text) {
            ++text_count;
            const double size = std::stod((*text)[1]);
            const double baseline = std::stod((*text)[3]);
            const double top = options.page_height_pt - baseline - size;
            if (top < options.margin_top_pt - 0.01
                || baseline < options.margin_bottom_pt - 0.01) {
                std::cerr << path.filename() << ": text outside page margins on page " << pages << '\n';
                ok = false;
            }
            const auto glyphs = (*text)[4].str();
            for (std::size_t i = 0; i + 4 <= glyphs.size(); i += 4) {
                if (std::stoul(glyphs.substr(i, 4), nullptr, 16) == x_glyph) {
                    ++x_count;
                }
            }
        }
        if (text_count == 0) {
            std::cerr << path.filename() << ": unexpected empty page " << pages << '\n';
            ok = false;
        }
        if (code_boxes) {
            std::size_t boxes = 0;
            for (auto rect = std::sregex_iterator(stream.begin(), stream.end(), rect_re);
                 rect != std::sregex_iterator(); ++rect) {
                ++boxes;
                const double bottom = std::stod((*rect)[2]);
                const double height = std::stod((*rect)[4]);
                if (bottom < options.margin_bottom_pt - 0.01
                    || bottom + height > options.page_height_pt - options.margin_top_pt + 0.01) {
                    std::cerr << path.filename() << ": code box outside page margins\n";
                    ok = false;
                }
            }
            if (boxes != 2) { // fill and outline for this page's portion
                std::cerr << path.filename() << ": missing continued code box\n";
                ok = false;
            }
        }
    }
    if ((multiple_pages ? pages < 2 : pages != 1) || x_count != expected_x_count) {
        std::cerr << path.filename() << ": pages=" << pages << ", x count=" << x_count
                  << ", expected x count=" << expected_x_count << '\n';
        ok = false;
    }
    return ok;
}

std::string repeat(const std::string& text, std::size_t count)
{
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        result += text;
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path directory = mark2haru_test::executable_dir(argc, argv);
    const auto metrics = mark2haru_test::load_default_metrics(directory);
    if (!metrics) {
        return 1;
    }
    mark2haru::Render_options options;
    options.font_root_dir = directory;
    auto check = [&](const std::string& name, const std::string& markdown,
                     std::size_t x_count, bool code = false, bool multiple_pages = true) {
        const auto path = directory / ("pagination_" + name + ".pdf");
        std::string error;
        if (!mark2haru::render_markdown_to_pdf(markdown, path, options, error)) {
            std::cerr << name << ": " << error << '\n';
            return false;
        }
        const auto font = code ? mark2haru::Pdf_font::MONO : mark2haru::Pdf_font::REGULAR;
        return check_pdf(path, options, metrics->font_face(font).glyph_for_codepoint('x'),
            x_count, code, multiple_pages);
    };
    bool ok = true;
    ok = check("paragraph", repeat("xxxx ", 1400), 5600) && ok;
    ok = check("heading", "# " + repeat("xxxx ", 700), 2800) && ok;
    ok = check("list", "- " + repeat("xxxx ", 1400), 5600) && ok;
    ok = check("ordered_list", "7. " + repeat("xxxx ", 1400), 5600) && ok;
    const auto code = "```\n" + repeat("x\n", 120) + "```\n";
    ok = check("code", code, 120, true) && ok;
    ok = check("code_after_text", "intro\n\n" + code, 120, true) && ok;
    ok = check("short_paragraph", "x", 1, false, false) && ok;
    ok = check("short_code", "```\nx\n```\n", 1, true, false) && ok;

    // A page too short for even one padded line must fail, not create an
    // unbounded series of empty continuation pages.
    options.page_height_pt = options.margin_top_pt + options.margin_bottom_pt + 1.0;
    std::string error;
    if (mark2haru::render_markdown_to_pdf(code, directory / "pagination_too_short.pdf", options, error)
        || error.find("too short") == std::string::npos) {
        std::cerr << "expected a clear failure for a page shorter than one code line\n";
        ok = false;
    }
    return ok ? 0 : 1;
}
