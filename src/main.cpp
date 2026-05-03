#include <mark2haru/render.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

void print_usage()
{
    std::cerr << "Usage: mark2haru <input.md> <output.pdf>\n";
}

bool read_file(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

int run(
    const fs::path& exe_path,
    const fs::path& input_path,
    const fs::path& output_path)
{
    std::string markdown;
    if (!read_file(input_path, markdown)) {
        std::cerr << "Failed to read input file: " << input_path.string() << "\n";
        return 2;
    }

    mark2haru::Render_options options;
    options.font_root_dir = exe_path.parent_path();

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
