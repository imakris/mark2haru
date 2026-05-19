#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
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

// Reads `sizeof(T)` big-endian bytes from `p` and returns them as T. T must
// be an unsigned integer of 1, 2, 4, or 8 bytes. Callers are responsible
// for bounds-checking `p` before the call.
template <class T>
constexpr T read_be(const std::uint8_t* p) noexcept
{
    static_assert(std::is_unsigned_v<T>, "read_be<T>: T must be unsigned");
    static_assert(
        sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "read_be<T>: only 8/16/32/64-bit widths are supported");
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>((value << 8) | static_cast<T>(p[i]));
    }
    return value;
}

// Splits on '\n', stripping a trailing '\r' so CRLF and LF inputs produce
// the same line vector. Keeps a final empty line if the input ends with '\n'.
inline std::vector<std::string> split_lines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl  = text.find('\n', pos);
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
