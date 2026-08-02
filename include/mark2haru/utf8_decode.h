#pragma once

// UTF-8 helpers shared between the PDF writer, the measurement context and
// the layout engine. Public because text_layout.h is: a consumer that wraps
// text through this library has to slice code points the same way the
// measurer and the writer decode them, and a private copy of this logic in
// the consumer would diverge from them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mark2haru
{

namespace utf8
{

namespace detail
{

struct Decoded_unit
{
    std::uint32_t codepoint = '?';
    std::size_t   byte_count = 1;
};

inline bool is_continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

inline Decoded_unit decode_unit(std::string_view text, std::size_t i)
{
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
        return { c, 1 };
    }

    if (c >= 0xC2 && c <= 0xDF) {
        if (i + 1 >= text.size()) {
            return {};
        }
        const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
        if (!is_continuation(c1)) {
            return {};
        }
        return {
            static_cast<std::uint32_t>(((c & 0x1F) << 6) | (c1 & 0x3F)),
            2
        };
    }

    if (c >= 0xE0 && c <= 0xEF) {
        if (i + 2 >= text.size()) {
            return {};
        }
        const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
        if (!is_continuation(c1) || !is_continuation(c2)) {
            return {};
        }
        if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) {
            return {};
        }

        const std::uint32_t cp =
            (static_cast<std::uint32_t>(c  & 0x0F) << 12) |
            (static_cast<std::uint32_t>(c1 & 0x3F) << 6)  |
             static_cast<std::uint32_t>(c2 & 0x3F);
        return { cp, 3 };
    }

    if (c >= 0xF0 && c <= 0xF4) {
        if (i + 3 >= text.size()) {
            return {};
        }
        const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
        if (!is_continuation(c1) || !is_continuation(c2) || !is_continuation(c3)) {
            return {};
        }
        if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90)) {
            return {};
        }

        const std::uint32_t cp =
            (static_cast<std::uint32_t>(c  & 0x07) << 18) |
            (static_cast<std::uint32_t>(c1 & 0x3F) << 12) |
            (static_cast<std::uint32_t>(c2 & 0x3F) << 6)  |
             static_cast<std::uint32_t>(c3 & 0x3F);
        return { cp, 4 };
    }

    return {};
}

} // namespace detail

// Decodes a UTF-8 string into a sequence of Unicode code points. Invalid
// or truncated sequences are replaced with the `?` code point and advance
// by a single byte, matching how the rest of the library treats unmapped
// glyphs.
inline std::vector<std::uint32_t> decode(std::string_view text)
{
    std::vector<std::uint32_t> cps;
    cps.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const detail::Decoded_unit unit = detail::decode_unit(text, i);
        cps.push_back(unit.codepoint);
        i += unit.byte_count;
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
        const detail::Decoded_unit unit = detail::decode_unit(text, i);
        out.emplace_back(text.substr(i, unit.byte_count));
        i += unit.byte_count;
    }
    return out;
}

} // namespace utf8
} // namespace mark2haru
