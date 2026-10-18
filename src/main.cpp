#include <mark2haru/render.h>

#include "io_helpers.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include <cstdlib>
#include <cstdint>
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

fs::path resolve_executable_path(const fs::path& argv0)
{
    // argv[0] can be a bare command or a launcher symlink, neither of which
    // identifies the directory containing the installed resources.
    fs::path path;
#ifdef _WIN32
    std::vector<wchar_t> buffer(256);
    while (buffer.size() <= 32768) {
        const DWORD size  = (DWORD)buffer.size();
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), size);
        if (count == 0) {
            break;
        }
        if (count < size) {
            path = std::wstring(buffer.data(), count);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::vector<char> buffer(1024);
    std::uint32_t size = (std::uint32_t)buffer.size();
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
    }
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        path = buffer.data();
    }
#elif defined(__linux__)
    std::error_code link_error;
    path = fs::read_symlink("/proc/self/exe", link_error);
#endif

    if (path.empty()) {
        path = argv0;
#ifndef _WIN32
        // Linux can run without procfs; other POSIX systems also need PATH
        // lookup when the process image is unavailable through a native API.
        const char* env_path = std::getenv("PATH");
        if (!argv0.has_parent_path() && env_path) {
            const std::string search(env_path);
            std::size_t start = 0;
            while (start <= search.size()) {
                const auto end   = search.find(':', start);
                const auto entry = search.substr(start, end == search.npos ? search.npos : end - start);
                const fs::path candidate = (entry.empty() ? fs::path(".") : fs::path(entry)) / argv0;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0) {
                    path = candidate;
                    break;
                }
                if (end == search.npos) {
                    break;
                }
                start = end + 1;
            }
        }
#endif
    }

    std::error_code ec;
    const auto canonical = fs::canonical(path, ec);
    if (!ec) {
        return canonical;
    }
    const auto absolute = fs::absolute(path, ec);
    return ec ? path : absolute;
}

fs::path resolve_font_root(const fs::path& exe_path)
{
    const fs::path exe_dir = resolve_executable_path(exe_path).parent_path();
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
