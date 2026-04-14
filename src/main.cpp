#include "render.h"

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

bool read_file(const std::string& path, std::string& out)
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

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        print_usage();
        return 1;
    }

    const std::string input_path = argv[1];
    const std::string output_path = argv[2];
    std::string markdown;
    if (!read_file(input_path, markdown)) {
        std::cerr << "Failed to read input file: " << input_path << "\n";
        return 2;
    }

    mark2haru::RenderOptions options;
    options.font_root_dir = std::filesystem::path(argv[0]).parent_path();

    if (!mark2haru::render_markdown_to_pdf(markdown, output_path, options)) {
        std::cerr << "Failed to write PDF: " << output_path << "\n";
        return 3;
    }

    std::cout << "Wrote " << output_path << "\n";
    return 0;
}
