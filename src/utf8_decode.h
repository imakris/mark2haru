#pragma once

// Internal helpers shared between the PDF writer, the measurement context,
// the layout engine, and the file-format parsers. Kept header-only and
// private to `src/` so it doesn't become part of the installed API surface.

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

namespace utf8
{

// Decodes a UTF-8 string into a sequence of Unicode code points. Invalid
// or truncated sequences are replaced with the `?` code point and advance
// by a single byte, matching how the rest of the library treats unmapped
// glyphs.
inline std::vector<std::uint32_t> decode(std::string_view text)
{
    std::vector<std::uint32_t> cps;
    cps.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp      = '?';
        std::size_t advance   = 1;
        if (c < 0x80) {
            cp = c;
        }
        else
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6)
                | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            advance = 2;
        }
        else
        if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12)                                      |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                  (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            advance = 3;
        }
        else
        if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18)                                       |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6)  |
                  (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            advance = 4;
        }
        cps.push_back(cp);
        i += advance;
    }
    return cps;
}

// Splits a UTF-8 string into one string per complete code-point unit so
// character-level wrapping fallback can never slice a multi-byte sequence
// in half. Invalid lead bytes emit a single-byte unit.
inline std::vector<std::string> split_pieces(std::string_view text)
{
    std::vector<std::string> out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len       = 1;
        if ((c & 0x80) == 0x00) {
            len = 1;
        }
        else
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        }
        else
        if ((c & 0xF0) == 0xE0) {
            len = 3;
        }
        else
        if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (i + len > text.size()) {
            len = 1;
        }
        out.emplace_back(text.substr(i, len));
        i += len;
    }
    return out;
}

} // namespace utf8
} // namespace mark2haru
