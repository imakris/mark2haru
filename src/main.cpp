#include "render.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& out)
{
    out << "Usage: mark2haru [options] <input.md> <output.pdf>\n"
        << "\n"
        << "Positional arguments:\n"
        << "  input.md        Markdown source file. Use '-' to read from stdin.\n"
        << "  output.pdf      Destination PDF path.\n"
        << "\n"
        << "Options:\n"
        << "  --page-size NAME    A4 (default) or Letter\n"
        << "  --margin MM         All four margins in millimetres (default 20)\n"
        << "  --body-size PT      Body font size in points (default 10.5)\n"
        << "  --font-dir DIR      Directory to search for bundled fonts\n"
        << "  -v, --verbose       Print extra diagnostics on success\n"
        << "  -h, --help          Show this message and exit\n";
}

bool read_file(const std::string& path, std::string& out)
{
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        out = ss.str();
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Convert millimetres to PDF points. 1 inch = 25.4 mm = 72 pt.
double mm_to_pt(double mm)
{
    return mm * 72.0 / 25.4;
}

bool set_page_size(const std::string& name, mark2haru::RenderOptions& opts)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "a4") {
        opts.page_width_pt = mm_to_pt(210.0);
        opts.page_height_pt = mm_to_pt(297.0);
        return true;
    }
    if (lower == "letter") {
        opts.page_width_pt = 8.5 * 72.0;
        opts.page_height_pt = 11.0 * 72.0;
        return true;
    }
    return false;
}

struct ParsedArgs {
    std::string input_path;
    std::string output_path;
    mark2haru::RenderOptions options;
    bool verbose = false;
    bool help = false;
    bool parse_error = false;
    std::string error;
};

bool needs_value(const std::string& arg)
{
    return arg == "--page-size" || arg == "--margin" || arg == "--body-size"
        || arg == "--font-dir";
}

ParsedArgs parse_args(int argc, char** argv)
{
    ParsedArgs pa;
    std::vector<std::string> positionals;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            pa.help = true;
            continue;
        }
        if (arg == "-v" || arg == "--verbose") {
            pa.verbose = true;
            continue;
        }
        if (needs_value(arg)) {
            if (i + 1 >= argc) {
                pa.parse_error = true;
                pa.error = "missing value for " + arg;
                return pa;
            }
            const std::string value = argv[++i];
            if (arg == "--page-size") {
                if (!set_page_size(value, pa.options)) {
                    pa.parse_error = true;
                    pa.error = "unknown page size: " + value;
                    return pa;
                }
            } else if (arg == "--margin") {
                try {
                    const double mm = std::stod(value);
                    const double pt = mm_to_pt(mm);
                    pa.options.margin_left_pt = pt;
                    pa.options.margin_right_pt = pt;
                    pa.options.margin_top_pt = pt;
                    pa.options.margin_bottom_pt = pt;
                } catch (...) {
                    pa.parse_error = true;
                    pa.error = "invalid --margin value: " + value;
                    return pa;
                }
            } else if (arg == "--body-size") {
                try {
                    pa.options.body_size_pt = std::stod(value);
                } catch (...) {
                    pa.parse_error = true;
                    pa.error = "invalid --body-size value: " + value;
                    return pa;
                }
            } else if (arg == "--font-dir") {
                pa.options.font_root_dir = value;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-' && arg != "-") {
            pa.parse_error = true;
            pa.error = "unknown option: " + arg;
            return pa;
        }
        positionals.push_back(arg);
    }

    if (pa.help) {
        return pa;
    }

    if (positionals.size() != 2) {
        pa.parse_error = true;
        pa.error = "expected exactly two positional arguments (input and output)";
        return pa;
    }
    pa.input_path = positionals[0];
    pa.output_path = positionals[1];
    return pa;
}

} // namespace

int main(int argc, char** argv)
{
    ParsedArgs pa = parse_args(argc, argv);
    if (pa.help) {
        print_usage(std::cout);
        return 0;
    }
    if (pa.parse_error) {
        std::cerr << "mark2haru: " << pa.error << "\n";
        print_usage(std::cerr);
        return 1;
    }

    std::string markdown;
    if (!read_file(pa.input_path, markdown)) {
        std::cerr << "mark2haru: failed to read input file: " << pa.input_path << "\n";
        return 2;
    }

    // Resolve the bundled-font directory relative to the executable when the
    // user has not overridden it. We prefer the exe-relative path because the
    // install layout keeps fonts/ next to the binary.
    if (pa.options.font_root_dir.empty()) {
        pa.options.font_root_dir = std::filesystem::path(argv[0]).parent_path();
    }

    const auto result = mark2haru::render_markdown_to_pdf(markdown, pa.output_path, pa.options);
    if (!result.ok) {
        std::cerr << "mark2haru: " << (result.error.empty() ? "render failed" : result.error) << "\n";
        return 3;
    }

    if (pa.verbose) {
        std::cout << "Wrote " << pa.output_path << "\n";
    }
    return 0;
}
