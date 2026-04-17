#pragma once

// Internal UTF-8 helpers shared between the PDF writer, the measurement
// context, and the layout engine. Kept header-only and private to `src/`
// so it doesn't become part of the installed API surface.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mark2haru
{
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
