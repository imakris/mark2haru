#include <mark2haru/render.h>
#include <mark2haru/markdown.h>
#include <mark2haru/table_layout.h>

#include "miniz.h"
#include "test_common.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <variant>

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
    std::size_t expected_x_count, bool code_boxes, bool multiple_pages, bool table_headers = false)
{
    std::ifstream input(path, std::ios::binary);
    const std::string pdf{std::istreambuf_iterator<char>(input), {}};
    const std::regex page_re(R"(/Contents ([0-9]+) 0 R)");
    const std::regex text_re(
        R"((/F[0-9]+) ([0-9.]+) Tf\n(-?[0-9.]+) (-?[0-9.]+) Td\n<([0-9a-fA-F]*)> Tj)");
    const std::regex rect_re(R"((-?[0-9.]+) (-?[0-9.]+) ([0-9.]+) ([0-9.]+) re)");
    const std::regex font_re(R"((/F[0-9]+) ([0-9]+) 0 R)");
    const std::regex unicode_re(R"(/ToUnicode ([0-9]+) 0 R)");
    const std::regex x_re(R"(<([0-9A-Fa-f]{4})> <0078>)");
    std::map<std::string, unsigned> x_cids;
    for (auto font = std::sregex_iterator(pdf.begin(), pdf.end(), font_re);
         font != std::sregex_iterator(); ++font)
    {
        const auto object = pdf.find("\n" + (*font)[2].str() + " 0 obj\n");
        const auto body   = pdf.substr(object, pdf.find("\nendobj", object) - object);
        std::smatch unicode_match;
        std::smatch x_match;
        if (std::regex_search(body, unicode_match, unicode_re)) {
            const auto cmap = content_stream(pdf, unicode_match[1].str());
            if (std::regex_search(cmap, x_match, x_re)) {
                x_cids[(*font)[1].str()] = static_cast<unsigned>(std::stoul(x_match[1], nullptr, 16));
            }
        }
    }
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
            const double size = std::stod((*text)[2]);
            const double baseline = std::stod((*text)[4]);
            const double top = options.page_height_pt - baseline - size;
            if (top < options.margin_top_pt - 0.01
                || baseline < options.margin_bottom_pt - 0.01) {
                std::cerr << path.filename() << ": text outside page margins on page " << pages << '\n';
                ok = false;
            }
            const auto cids  = (*text)[5].str();
            const auto font = x_cids.find((*text)[1].str());
            for (std::size_t i = 0; font != x_cids.end() && i + 4 <= cids.size(); i += 4) {
                if (std::stoul(cids.substr(i, 4), nullptr, 16) == font->second) {
                    ++x_count;
                }
            }
        }
        if (text_count == 0) {
            std::cerr << path.filename() << ": unexpected empty page " << pages << '\n';
            ok = false;
        }
        if (code_boxes || table_headers) {
            std::size_t boxes = 0;
            for (auto rect = std::sregex_iterator(stream.begin(), stream.end(), rect_re);
                 rect != std::sregex_iterator(); ++rect) {
                ++boxes;
                const double bottom = std::stod((*rect)[2]);
                const double height = std::stod((*rect)[4]);
                if (bottom < options.margin_bottom_pt - 0.01
                    || bottom + height > options.page_height_pt - options.margin_top_pt + 0.01) {
                    std::cerr << path.filename() << ": box outside page margins\n";
                    ok = false;
                }
            }
            // Code has a fill and outline; each table page has one header fill.
            if (boxes != (code_boxes ? 2u : 1u)) {
                std::cerr << path.filename() << ": unexpected page box count\n";
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

bool check_table_pagination(const fs::path& directory)
{
    using namespace mark2haru;
    Render_options options;
    options.font_root_dir    = directory;
    options.page_width_pt    = 180.0;
    options.margin_left_pt   = 12.0;
    options.margin_right_pt  = 12.0;
    options.margin_top_pt    = 0.0;
    options.margin_bottom_pt = 0.0;
    options.body_size_pt     = 10.0;

    const auto metrics = mark2haru_test::load_default_metrics(directory);
    if (!metrics) {
        return false;
    }
    const std::string header = "| x |\n| --- |\n";
    const std::string body   = "| " + repeat("x ", 60) + "|\n";
    const auto blocks = parse_markdown(header + body);
    const auto& table = std::get<Table_block>(blocks.front());
    table_style_t style;
    style.text_size_pt    = options.body_size_pt * 0.95;
    style.text_leading_pt = style.text_size_pt * 1.22;
    style.cell_padding_pt = options.body_size_pt * 0.35;
    const auto columns = compute_table_columns(table,
        options.page_width_pt - options.margin_left_pt - options.margin_right_pt, style, *metrics);
    const double header_height   = measure_table_row_height(table, 0, columns, style, *metrics);
    const double body_height     = measure_table_row_height(table, 1, columns, style, *metrics);
    const double combined_height = header_height + body_height;

    // Use the real font/layout measurements to place the capacity boundary.
    // Each row fits alone; only repeating the header makes this too tall.
    options.page_height_pt = combined_height - 0.25;
    if (header_height >= options.page_height_pt || body_height >= options.page_height_pt) {
        std::cerr << "table fixture must fit each row separately\n";
        return false;
    }
    auto rejected = [&](const char* name, const std::string& markdown) {
        const auto path = directory / (std::string("pagination_table_") + name + ".pdf");
        const std::string sentinel = "preserve existing destination\n";
        {
            std::ofstream output(path, std::ios::binary);
            output << sentinel;
            output.close();
            if (!output) {
                std::cerr << "could not create table destination sentinel\n";
                return false;
            }
        }
        std::string error;
        const bool rendered = render_markdown_to_pdf(markdown, path, options, error);
        std::ifstream input(path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        if (rendered || error.find("Table") == std::string::npos || contents != sentinel) {
            std::cerr << name << ": oversized table must fail and preserve the destination: " << error << '\n';
            return false;
        }
        return true;
    };
    bool ok = rejected("first_row", header + body);
    ok = rejected("later_row", header + "| x |\n" + body) && ok;
    options.page_height_pt = header_height - 0.25;
    ok = rejected("header_only", header) && ok;

    auto fitting = [&](const char* name, const std::string& markdown,
                       std::size_t x_count, bool multiple_pages) {
        const auto path = directory / (std::string("pagination_table_") + name + ".pdf");
        std::string error;
        if (!render_markdown_to_pdf(markdown, path, options, error)) {
            std::cerr << name << ": " << error << '\n';
            return false;
        }
        return check_pdf(path, options, x_count, false, multiple_pages, true);
    };
    options.page_height_pt = combined_height;
    ok = fitting("exact_fit", header + body, 61, false) && ok;
    options.page_height_pt = combined_height + 0.25;
    ok = fitting("continued", header + body + body, 122, true) && ok;
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path directory = mark2haru_test::executable_dir(argc, argv);
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
        return check_pdf(path, options, x_count, code, multiple_pages);
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
    ok = check_table_pagination(directory) && ok;

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
