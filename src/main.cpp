#include <mark2haru/render.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void print_usage()
{
    std::cerr << "Usage: mark2haru <input.md> <output.pdf>\n";
}

bool read_file(const std::filesystem::path& path, std::string& out)
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

int run(const std::filesystem::path& exe_path,
        const std::filesystem::path& input_path,
        const std::filesystem::path& output_path)
{
    std::string markdown;
    if (!read_file(input_path, markdown)) {
        std::cerr << "Failed to read input file: " << input_path.string() << "\n";
        return 2;
    }

    mark2haru::RenderOptions options;
    options.font_root_dir = exe_path.parent_path();

    if (!mark2haru::render_markdown_to_pdf(markdown, output_path, options)) {
        std::cerr << "Failed to write PDF: " << output_path.string() << "\n";
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
    return run(std::filesystem::path(argv[0]),
               std::filesystem::path(argv[1]),
               std::filesystem::path(argv[2]));
}
#else
int main(int argc, char** argv)
{
    if (argc != 3) {
        print_usage();
        return 1;
    }

    return run(std::filesystem::path(argv[0]),
               std::filesystem::path(argv[1]),
               std::filesystem::path(argv[2]));
}
#endif
