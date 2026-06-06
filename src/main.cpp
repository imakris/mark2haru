#include <mark2haru/render.h>

#include "io_helpers.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

void print_usage()
{
    std::cerr << "Usage: mark2haru <input.md> <output.pdf>\n";
}

bool has_bundled_fonts(const fs::path& root)
{
    if (root.empty()) {
        return false;
    }

    std::error_code ec;
    return fs::is_regular_file(root / "fonts" / "DejaVuSans.ttf", ec) && !ec;
}

fs::path installed_font_root(const fs::path& exe_dir)
{
#ifdef MARK2HARU_INSTALL_DATADIR
    const fs::path install_data_dir(MARK2HARU_INSTALL_DATADIR);
    if (install_data_dir.is_absolute()) {
        return install_data_dir;
    }
    if (!exe_dir.empty()) {
        return exe_dir.parent_path() / install_data_dir;
    }
    return install_data_dir;
#else
    if (!exe_dir.empty()) {
        return exe_dir.parent_path() / "share" / "mark2haru";
    }
    return {};
#endif
}

fs::path resolve_font_root(const fs::path& exe_path)
{
    std::error_code ec;
    fs::path        exe_abs = fs::absolute(exe_path, ec);
    if (ec) {
        exe_abs = exe_path;
    }

    const fs::path exe_dir = exe_abs.parent_path();
    const std::vector<fs::path> candidates = {
        exe_dir,
        installed_font_root(exe_dir),
    };

    for (const auto& candidate : candidates) {
        if (has_bundled_fonts(candidate)) {
            return candidate;
        }
    }
    return !exe_dir.empty() ? exe_dir : installed_font_root(exe_dir);
}

int run(
    const fs::path&    exe_path,
    const fs::path&    input_path,
    const fs::path&    output_path)
{
    std::string markdown;
    if (!mark2haru::read_file_string(input_path, markdown)) {
        std::cerr << "Failed to read input file: " << input_path.string() << "\n";
        return 2;
    }

    mark2haru::Render_options options;
    options.asset_base_dir = fs::absolute(input_path).parent_path();
    options.font_root_dir = resolve_font_root(exe_path);

    std::string error;
    if (!mark2haru::render_markdown_to_pdf(markdown, output_path, options, error)) {
        std::cerr << "Failed to write PDF: " << output_path.string();
        if (!error.empty()) {
            std::cerr << ": " << error;
        }
        std::cerr << "\n";
        return 3;
    }

    std::cout << "Wrote " << output_path.string() << "\n";
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        print_usage();
        return 1;
    }
    return run(
        fs::path(argv[0]),
        fs::path(argv[1]),
        fs::path(argv[2]));
}
#else
int main(int argc, char** argv)
{
    if (argc != 3) {
        print_usage();
        return 1;
    }

    return run(
        fs::path(argv[0]),
        fs::path(argv[1]),
        fs::path(argv[2]));
}
#endif
