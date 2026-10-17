#pragma once

#include <mark2haru/markdown.h>
#include <mark2haru/pdf_writer.h>

#include <mark2haru/utf8_decode.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mark2haru {
namespace text_layout {

// The line-breaking policy for this library, and for anything that lays text
// out alongside it. It is stated once here because a document that wraps its
// tables through this engine and its paragraphs through a private copy breaks
// lines in two different places on one page.
//
// The policy:
//   - a line breaks at a space or a tab, and at an explicit newline;
//   - a run of breakable whitespace is one break opportunity and renders as
//     one space, the way the same source reads in a browser;
//   - a word is a run of characters between breaks, however many style runs
//     it spans: a style change inside a word is not a break opportunity;
//   - a word wider than the whole line starts on a line of its own and is
//     then broken at code-point boundaries, whatever the style of the piece
//     being placed, so the tail of the preceding line is left empty rather
//     than partly filled;
//   - a separating space belongs to the word after it, so a space that falls
//     at a line break is dropped rather than left at the end of one line or
//     carried to the start of the next;
//   - consecutive pieces of the same style on one line are merged into one
//     span, so a consumer draws the fewest possible spans.
//
// Callers supply the measurer, so the policy is independent of where the font
// metrics come from, and they receive each span's offset along the line, so
// nobody has to re-measure the line prefix to position a span.

// Whitespace a line may break at. A tab breaks like a space rather than being
// measured as a glyph the font may not even carry.
inline bool is_breakable_space(char c)
{
    return c == ' ' || c == '\t';
}

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

struct Span
{
    std::string    text;
    Inline_style   style       = Inline_style::NORMAL;

    // Where the span starts along its line, in points from the line origin.
    // The engine knows this while it is placing the span, so consumers that
    // position spans absolutely do not have to measure the line prefix again.
    double         x_offset_pt = 0.0;
};

struct Line
{
    std::vector<Span>
                   spans;
    double         height_pt = 0.0;
    double         width_pt  = 0.0;
};

// Splits each Inline_run on '\n' and on breakable whitespace. Newlines become
// tokens with `newline=true` and empty text; a run of breakable whitespace
// becomes one single-space token; other runs of characters become word tokens.
// Style is carried through unchanged.
inline std::vector<Token> tokenize_runs(const std::vector<Inline_run>& runs)
{
    std::vector<Token> tokens;

    // Collapsing here rather than per chunk is what makes a whitespace run
    // that straddles an inline-style boundary, or a run of tabs and spaces
    // mixed together, come out as the single space it reads as. A leading
    // space on a line is dropped outright, which is what the wrap engine does
    // with it anyway.
    auto push_space = [&tokens](Inline_style style) {
        if (tokens.empty() || tokens.back().newline || tokens.back().text == " ") {
            return;
        }
        tokens.push_back({ " ", style, false });
    };

    for (const auto& run : runs) {
        std::size_t start = 0;
        while (start <= run.text.size()) {
            const std::size_t nl = run.text.find('\n', start);
            const std::string chunk = nl == std::string::npos
                ? run.text.substr(start)
                : run.text.substr(start, nl - start);

            std::size_t piece = 0;
            while (piece <= chunk.size()) {
                std::size_t sp = piece;
                while (sp < chunk.size() && !is_breakable_space(chunk[sp])) {
                    ++sp;
                }
                if (sp == chunk.size()) {
                    if (piece < chunk.size()) {
                        tokens.push_back({ chunk.substr(piece), run.style, false });
                    }
                    break;
                }
                if (sp > piece) {
                    tokens.push_back({ chunk.substr(piece, sp - piece), run.style, false });
                }
                push_space(run.style);
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

// Convenience for a caller's plain string that carries no inline markup of
// its own: one run, one style, the same policy. The policy applies whole, so
// a run of whitespace collapses to one space and a leading one disappears
// entirely; text whose own spacing is part of its meaning has to reach
// wrap_tokens as tokens the caller built itself.
inline std::vector<Token> tokenize_text(const std::string& text, Inline_style style)
{
    return tokenize_runs({ Inline_run{ text, style } });
}

// tokenize_runs emits one word token per style run, so a word whose style
// changes inside it arrives as several adjacent word tokens with no space or
// newline between them. These two helpers define a word over that sequence, so
// the wrap engine and any caller measuring minimum widths agree on it.
inline bool is_word_token(const Token& token)
{
    return !token.newline && !token.text.empty() && token.text != " ";
}

// One past the last token of the word that starts at tokens[begin], which must
// be a word token.
inline std::size_t word_end(const std::vector<Token>& tokens, std::size_t begin)
{
    std::size_t end = begin + 1;
    while (end < tokens.size() && is_word_token(tokens[end])) {
        ++end;
    }
    return end;
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

    // A separating space belongs to the word that follows it, not to the word
    // before it. Holding it until that word is placed is what keeps it off the
    // end of a line it turned out to break at.
    bool         has_pending_space   = false;
    double       pending_space_width = 0.0;
    Inline_style pending_space_style = Inline_style::NORMAL;

    // Called before current_width advances past the span, so current_width is
    // the span's offset along the line.
    auto append_span = [&](const std::string& text, Inline_style style) {
        if (text.empty()) {
            return;
        }
        if (!current.spans.empty() && current.spans.back().style == style) {
            current.spans.back().text += text;
            return;
        }
        current.spans.push_back({ text, style, current_width });
    };

    auto finish_line = [&]() {
        current.height_pt = line_height_pt;
        current.width_pt  = current_width;
        lines.push_back(current);
        current           = Line{};
        current_width     = 0.0;
        has_pending_space = false;
    };

    auto take_pending_space = [&]() {
        if (!has_pending_space) {
            return;
        }
        append_span(" ", pending_space_style);
        current_width += pending_space_width;
        has_pending_space = false;
    };

    // Places tokens[begin, end), the fragments of one word, as a unit: the
    // word breaks to the next line as a whole if it does not fit the rest of
    // this one, wherever its style changes fall.
    auto add_word = [&](std::size_t begin, std::size_t end) {
        double word_width = 0.0;
        for (std::size_t i = begin; i < end; ++i) {
            word_width += measure(font_for(tokens[i].style), tokens[i].text, size_pt);
        }
        const double lead_width = has_pending_space ? pending_space_width : 0.0;
        if (current_width > 0.0 && current_width + lead_width + word_width > max_width_pt) {
            finish_line();
        }
        take_pending_space();
        if (word_width <= max_width_pt) {
            for (std::size_t i = begin; i < end; ++i) {
                const Token& fragment = tokens[i];
                append_span(fragment.text, fragment.style);

                // A word of one token has just been measured; only the
                // fragments of a styled word need their own width again.
                current_width += end - begin == 1
                    ? word_width
                    : measure(font_for(fragment.style), fragment.text, size_pt);
            }
            return;
        }

        // Word is wider than a full line: break it at code-point boundaries,
        // continuing across its style changes.
        for (std::size_t i = begin; i < end; ++i) {
            const Token&   token = tokens[i];
            const Pdf_font font  = font_for(token.style);
            std::string    fragment;
            double         fragment_width = 0.0;
            for (const auto& piece : utf8::split_pieces(token.text)) {
                const double piece_width = measure(font, piece, size_pt);
                const bool   has_content = current_width > 0.0 || fragment_width > 0.0;
                if (has_content && current_width + fragment_width + piece_width > max_width_pt) {
                    if (!fragment.empty()) {
                        append_span(fragment, token.style);
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
                append_span(fragment, token.style);
                current_width += fragment_width;
            }
        }
    };

    for (std::size_t i = 0; i < tokens.size();) {
        const Token& token = tokens[i];
        if (token.newline) {
            finish_line();
            ++i;
            continue;
        }
        if (token.text == " ") {
            if (!current.spans.empty()) {
                has_pending_space   = true;
                pending_space_style = token.style;
                pending_space_width = measure(font_for(token.style), token.text, size_pt);
            }
            ++i;
            continue;
        }
        if (!is_word_token(token)) {
            ++i;
            continue;
        }
        const std::size_t end = word_end(tokens, i);
        add_word(i, end);
        i = end;
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
