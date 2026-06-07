#pragma once

#include <mark2haru/markdown.h>
#include <mark2haru/pdf_writer.h>

#include "utf8_decode.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mark2haru {
namespace text_layout {

// Maps the parser's inline-style enum to the writer's font slot. Used by
// every wrap- or measure-shaped helper to look up the right metrics for a
// given run of text.
inline Pdf_font font_for(Inline_style style)
{
    switch (style) {
        case Inline_style::BOLD:        return Pdf_font::BOLD;
        case Inline_style::ITALIC:      return Pdf_font::ITALIC;
        case Inline_style::BOLD_ITALIC: return Pdf_font::BOLD_ITALIC;
        case Inline_style::CODE:        return Pdf_font::MONO;
        case Inline_style::NORMAL:
        default:                        return Pdf_font::REGULAR;
    }
}

struct Token
{
    std::string    text;
    Inline_style   style     = Inline_style::NORMAL;
    bool           newline   = false;
};

struct Line
{
    std::vector<std::pair<std::string, Inline_style>>
                   spans;
    double         height_pt = 0.0;
};

// Splits each Inline_run on '\n' and ' '. Newlines become tokens with
// `newline=true` and empty text; spaces become single-space tokens; other
// runs of non-space, non-newline characters become word tokens. Style is
// carried through unchanged.
inline std::vector<Token> tokenize_runs(const std::vector<Inline_run>& runs)
{
    std::vector<Token> tokens;
    for (const auto& run : runs) {
        std::size_t start = 0;
        while (start <= run.text.size()) {
            const std::size_t nl = run.text.find('\n', start);
            const std::string chunk = nl == std::string::npos
                ? run.text.substr(start)
                : run.text.substr(start, nl - start);

            std::size_t piece = 0;
            while (piece <= chunk.size()) {
                const std::size_t sp = chunk.find(' ', piece);
                if (sp == std::string::npos) {
                    if (piece < chunk.size()) {
                        tokens.push_back({ chunk.substr(piece), run.style, false });
                    }
                    break;
                }
                if (sp > piece) {
                    tokens.push_back({ chunk.substr(piece, sp - piece), run.style, false });
                }
                tokens.push_back({ " ", run.style, false });
                piece = sp + 1;
            }

            if (nl == std::string::npos) {
                break;
            }
            tokens.push_back({ {}, run.style, true });
            start = nl + 1;
        }
    }
    return tokens;
}

// Wraps a pre-tokenized sequence into lines that fit within `max_width_pt`.
// `line_height_pt` is the resolved per-line height (callers that think in
// terms of a leading factor pass `size_pt * factor`). `measure(font, text,
// size_pt) -> double` returns the rendered width of `text` in points.
//
// Consecutive spans of the same style on the same line are merged so the
// caller sees the fewest possible draw spans.
template <class MeasureFn>
std::vector<Line> wrap_tokens(
    const std::vector<Token>&  tokens,
    double                     max_width_pt,
    double                     size_pt,
    double                     line_height_pt,
    MeasureFn&&                measure)
{
    std::vector<Line> lines;
    Line   current;
    double current_width = 0.0;

    auto append_span = [&](const std::string& text, Inline_style style) {
        if (text.empty()) {
            return;
        }
        if (!current.spans.empty() && current.spans.back().second == style) {
            current.spans.back().first += text;
            return;
        }
        current.spans.emplace_back(text, style);
    };

    auto finish_line = [&]() {
        current.height_pt = line_height_pt;
        lines.push_back(current);
        current = Line{};
        current_width = 0.0;
    };

    auto add_word = [&](const std::string& word, Inline_style style) {
        const Pdf_font font       = font_for(style);
        const double   word_width = measure(font, word, size_pt);
        if (current_width > 0.0 && current_width + word_width > max_width_pt) {
            finish_line();
        }
        if (word_width <= max_width_pt) {
            append_span(word, style);
            current_width += word_width;
            return;
        }

        // Word is wider than a full line: break it at code-point boundaries.
        std::string fragment;
        double      fragment_width = 0.0;
        for (const auto& piece : utf8::split_pieces(word)) {
            const double piece_width = measure(font, piece, size_pt);
            const bool   has_content = current_width > 0.0 || fragment_width > 0.0;
            if (has_content && current_width + fragment_width + piece_width > max_width_pt) {
                if (!fragment.empty()) {
                    append_span(fragment, style);
                    current_width += fragment_width;
                    fragment.clear();
                    fragment_width = 0.0;
                }
                finish_line();
            }
            fragment += piece;
            fragment_width += piece_width;
        }
        if (!fragment.empty()) {
            append_span(fragment, style);
            current_width += fragment_width;
        }
    };

    for (const auto& token : tokens) {
        if (token.newline) {
            finish_line();
            continue;
        }
        if (token.text == " ") {
            const double space_width = measure(font_for(token.style), token.text, size_pt);
            if (!current.spans.empty() && current_width + space_width > max_width_pt) {
                finish_line();
            }
            else
            if (!current.spans.empty()) {
                append_span(token.text, token.style);
                current_width += space_width;
            }
            continue;
        }
        if (!token.text.empty()) {
            add_word(token.text, token.style);
        }
    }

    if (!current.spans.empty() || lines.empty()) {
        finish_line();
    }
    return lines;
}

template <class MeasureFn>
std::vector<Line> wrap_runs(
    const std::vector<Inline_run>& runs,
    double                         max_width_pt,
    double                         size_pt,
    double                         line_height_pt,
    MeasureFn&&                    measure)
{
    return wrap_tokens(
        tokenize_runs(runs),
        max_width_pt,
        size_pt,
        line_height_pt,
        std::forward<MeasureFn>(measure));
}

inline double total_height(const std::vector<Line>& lines)
{
    double height = 0.0;
    for (const auto& line : lines) {
        height += line.height_pt;
    }
    return height;
}

} // namespace text_layout
} // namespace mark2haru
