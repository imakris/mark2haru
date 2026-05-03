#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace mark2haru
{

inline bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

// Splits on '\n', stripping a trailing '\r' so CRLF and LF inputs produce
// the same line vector. Keeps a final empty line if the input ends with '\n'.
inline std::vector<std::string> split_lines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
        std::string line(text.substr(pos, end - pos));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
    return lines;
}

} // namespace mark2haru
