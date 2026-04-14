#include <mark2haru/markdown.h>

#include <cctype>
#include <cstdlib>
#include <iterator>
#include <string_view>

namespace mark2haru {
namespace {

bool starts_with(std::string_view s, size_t pos, std::string_view prefix)
{
    return pos + prefix.size() <= s.size() && s.substr(pos, prefix.size()) == prefix;
}

bool is_word_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

std::string rtrim(const std::string& s)
{
    const auto end = s.find_last_not_of(" \t\r");
    if (end == std::string::npos) {
        return {};
    }
    return s.substr(0, end + 1);
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            auto line = text.substr(pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            break;
        }
        auto line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        pos = nl + 1;
    }
    return lines;
}

// Find the next asterisk run whose length is exactly `run_len`, skipping runs
// of other lengths. Used to prevent a single `*` italic from closing on the
// first star of a nested `**bold**`, and similarly for `**`/`***`.
size_t find_asterisk_run(std::string_view s, size_t start, size_t run_len)
{
    size_t pos = start;
    while (pos < s.size() && s[pos] != '\n') {
        if (s[pos] != '*') {
            ++pos;
            continue;
        }
        const size_t run_start = pos;
        while (pos < s.size() && s[pos] == '*') {
            ++pos;
        }
        if (pos - run_start == run_len) {
            return run_start;
        }
    }
    return std::string::npos;
}

size_t find_closing_underscore(std::string_view s, size_t start, size_t delim_len)
{
    for (size_t pos = start; pos + delim_len <= s.size(); ++pos) {
        if (s[pos] == '\n') {
            return std::string::npos;
        }

        bool all_underscore = true;
        for (size_t k = 0; k < delim_len; ++k) {
            if (s[pos + k] != '_') {
                all_underscore = false;
                break;
            }
        }
        if (!all_underscore) {
            continue;
        }

        const bool extends = pos + delim_len < s.size() && s[pos + delim_len] == '_';
        const char after = pos + delim_len < s.size() ? s[pos + delim_len] : ' ';
        if (!extends && !is_word_char(after)) {
            return pos;
        }
    }
    return std::string::npos;
}

bool is_escapable(char c)
{
    return c == '\\' || c == '`' || c == '*' || c == '_' || c == '{' || c == '}'
        || c == '[' || c == ']' || c == '(' || c == ')' || c == '#' || c == '+'
        || c == '-' || c == '.' || c == '!' || c == '|' || c == '~';
}

size_t find_backtick_run(std::string_view s, size_t start, size_t run_len)
{
    size_t pos = start;
    while (pos < s.size() && s[pos] != '\n') {
        if (s[pos] != '`') {
            ++pos;
            continue;
        }
        const size_t run_start = pos;
        while (pos < s.size() && s[pos] == '`') {
            ++pos;
        }
        if (pos - run_start == run_len) {
            return run_start;
        }
    }
    return std::string::npos;
}

size_t find_link_close(std::string_view s, size_t open)
{
    int depth = 1;
    size_t pos = open + 1;
    while (pos < s.size()) {
        const char c = s[pos];
        if (c == '\n') {
            return std::string::npos;
        }
        if (c == '\\' && pos + 1 < s.size() && is_escapable(s[pos + 1])) {
            pos += 2;
            continue;
        }
        if (c == '(') {
            ++depth;
        }
        else
        if (c == ')') {
            if (--depth == 0) {
                return pos;
            }
        }
        ++pos;
    }
    return std::string::npos;
}

std::vector<inline_run_t> parse_inline(const std::string& text)
{
    std::vector<inline_run_t> runs;
    std::string current;
    size_t i = 0;

    auto flush = [&](Inline_style style = Inline_style::NORMAL) {
        if (!current.empty()) {
            runs.push_back({ current, style });
            current.clear();
        }
    };

    auto can_open_underscore = [&](size_t pos) {
        return pos == 0 || !is_word_char(text[pos - 1]);
    };

    while (i < text.size()) {
        if (text[i] == '\\' && i + 1 < text.size() && is_escapable(text[i + 1])) {
            current.push_back(text[i + 1]);
            i += 2;
            continue;
        }

        if (starts_with(text, i, "***")) {
            const size_t close = find_asterisk_run(text, i + 3, 3);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3), Inline_style::BOLD_ITALIC });
                i = close + 3;
                continue;
            }
        }

        if (starts_with(text, i, "**")) {
            const size_t close = find_asterisk_run(text, i + 2, 2);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2), Inline_style::BOLD });
                i = close + 2;
                continue;
            }
        }

        if (text[i] == '*' && !starts_with(text, i, "**")) {
            const size_t close = find_asterisk_run(text, i + 1, 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1), Inline_style::ITALIC });
                i = close + 1;
                continue;
            }
        }

        if (starts_with(text, i, "___") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 3, 3);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3), Inline_style::BOLD_ITALIC });
                i = close + 3;
                continue;
            }
        }

        if (starts_with(text, i, "__") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 2, 2);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2), Inline_style::BOLD });
                i = close + 2;
                continue;
            }
        }

        if (text[i] == '_' && !starts_with(text, i, "__") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 1, 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1), Inline_style::ITALIC });
                i = close + 1;
                continue;
            }
        }

        if (text[i] == '`') {
            size_t run_len = 0;
            while (i + run_len < text.size() && text[i + run_len] == '`') {
                ++run_len;
            }
            const size_t close = find_backtick_run(text, i + run_len, run_len);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + run_len, close - i - run_len), Inline_style::CODE });
                i = close + run_len;
                continue;
            }
        }

        if (text[i] == '[') {
            const size_t bracket_close = text.find("](", i + 1);
            if (bracket_close != std::string::npos) {
                const size_t paren_close = find_link_close(text, bracket_close + 1);
                if (paren_close != std::string::npos) {
                    const std::string display = text.substr(i + 1, bracket_close - i - 1);
                    const std::string url = text.substr(
                        bracket_close + 2,
                        paren_close - bracket_close - 2);
                    if (display.empty()) {
                        current += url;
                    }
                    else
                    if (display == url) {
                        current += display;
                    }
                    else {
                        current += display;
                        current += " (";
                        current += url;
                        current += ')';
                    }
                    i = paren_close + 1;
                    continue;
                }
            }
        }

        current.push_back(text[i]);
        ++i;
    }

    flush();
    return runs;
}

struct classified_line_t {
    enum class Type {
        EMPTY,
        HEADING,
        BULLET_ITEM,
        ORDERED_ITEM,
        TABLE_ROW,
        TABLE_SEPARATOR,
        PAGE_BREAK,
        TEXT,
    } type = Type::TEXT;
    std::string content;
    int heading_level = 0;
    int list_number = 0;
};

classified_line_t classify_line(const std::string& line)
{
    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
        return { classified_line_t::Type::EMPTY, {}, 0, 0 };
    }

    if (trimmed == "<!-- pagebreak -->") {
        return { classified_line_t::Type::PAGE_BREAK, {}, 0, 0 };
    }

    if (trimmed.rfind("```", 0) == 0) {
        return { classified_line_t::Type::TEXT, trimmed, 0, 0 };
    }

    if (trimmed.rfind('#', 0) == 0) {
        int level = 0;
        while (level < 6 && level < static_cast<int>(trimmed.size()) && trimmed[level] == '#') {
            ++level;
        }
        if (level > 0 && level < static_cast<int>(trimmed.size()) && trimmed[level] == ' ') {
            return { classified_line_t::Type::HEADING, trim(trimmed.substr(level + 1)), level, 0 };
        }
    }

    if (trimmed.size() >= 2
        && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')
        && trimmed[1] == ' ') {
        return { classified_line_t::Type::BULLET_ITEM, trim(trimmed.substr(2)), 0, 0 };
    }

    size_t pos = 0;
    while (pos < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
        ++pos;
    }
    if (pos > 0 && pos + 1 < trimmed.size() && trimmed[pos] == '.' && trimmed[pos + 1] == ' ') {
        return {
            classified_line_t::Type::ORDERED_ITEM,
            trim(trimmed.substr(pos + 2)),
            0,
            std::atoi(trimmed.substr(0, pos).c_str())
        };
    }

    if (trimmed.size() >= 2 && trimmed.front() == '|' && trimmed.back() == '|') {
        bool separator = true;
        for (char c : trimmed) {
            if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') {
                separator = false;
                break;
            }
        }
        return {
            separator ? classified_line_t::Type::TABLE_SEPARATOR : classified_line_t::Type::TABLE_ROW,
            trimmed,
            0,
            0
        };
    }

    return { classified_line_t::Type::TEXT, rtrim(line), 0, 0 };
}

std::vector<std::string> split_table_cells(const std::string& row)
{
    std::vector<std::string> cells;
    std::string working = row;
    if (!working.empty() && working.front() == '|') {
        working.erase(0, 1);
    }
    if (!working.empty() && working.back() == '|') {
        working.pop_back();
    }
    size_t i = 0;
    while (i <= working.size()) {
        const size_t pipe = working.find('|', i);
        if (pipe == std::string::npos) {
            cells.push_back(trim(working.substr(i)));
            break;
        }
        cells.push_back(trim(working.substr(i, pipe - i)));
        i = pipe + 1;
    }
    return cells;
}

} // namespace

std::vector<block_t> parse_markdown(const std::string& input)
{
    std::vector<block_t> blocks;
    const auto lines = split_lines(input);
    size_t i = 0;
    std::string paragraph_accum;

    auto flush_paragraph = [&]() {
        if (paragraph_accum.empty()) {
            return;
        }
        paragraph_block_t pb;
        pb.runs = parse_inline(paragraph_accum);
        blocks.push_back(std::move(pb));
        paragraph_accum.clear();
    };

    while (i < lines.size()) {
        const std::string trimmed = trim(lines[i]);

        if (trimmed.rfind("```", 0) == 0) {
            flush_paragraph();
            code_block_t code;
            code.language = trim(trimmed.substr(3));
            ++i;
            while (i < lines.size()) {
                const std::string inner = trim(lines[i]);
                if (inner == "```") {
                    ++i;
                    break;
                }
                if (!code.text.empty()) {
                    code.text.push_back('\n');
                }
                code.text += lines[i];
                ++i;
            }
            blocks.push_back(std::move(code));
            continue;
        }

        const classified_line_t cl = classify_line(lines[i]);

        switch (cl.type) {
            case classified_line_t::Type::EMPTY:
                flush_paragraph();
                ++i;
                break;

            case classified_line_t::Type::HEADING: {
                flush_paragraph();
                heading_block_t hb;
                hb.level = cl.heading_level;
                hb.runs = parse_inline(cl.content);
                blocks.push_back(std::move(hb));
                ++i;
                break;
            }

            case classified_line_t::Type::BULLET_ITEM:
            case classified_line_t::Type::ORDERED_ITEM: {
                flush_paragraph();
                const bool ordered = cl.type == classified_line_t::Type::ORDERED_ITEM;
                list_block_t lb;
                lb.ordered = ordered;
                lb.start_number = cl.list_number > 0 ? cl.list_number : 1;

                std::string item_text;
                bool item_open = false;
                auto flush_item = [&]() {
                    if (!item_open) {
                        return;
                    }
                    list_item_t item;
                    item.runs = parse_inline(item_text);
                    lb.items.push_back(std::move(item));
                    item_text.clear();
                    item_open = false;
                };

                while (i < lines.size()) {
                    const classified_line_t item_line = classify_line(lines[i]);
                    const bool matching =
                        (ordered && item_line.type == classified_line_t::Type::ORDERED_ITEM)
                        || (!ordered && item_line.type == classified_line_t::Type::BULLET_ITEM);
                    if (matching) {
                        flush_item();
                        item_text = item_line.content;
                        item_open = true;
                        ++i;
                        continue;
                    }

                    if (item_open && item_line.type == classified_line_t::Type::TEXT
                        && !lines[i].empty()
                        && (lines[i][0] == ' ' || lines[i][0] == '\t'))
                    {
                        if (!item_text.empty()) {
                            item_text.push_back(' ');
                        }
                        item_text += item_line.content;
                        ++i;
                        continue;
                    }

                    break;
                }

                flush_item();
                blocks.push_back(std::move(lb));
                break;
            }

            case classified_line_t::Type::TABLE_ROW: {
                if (i + 1 < lines.size()
                    && classify_line(lines[i + 1]).type == classified_line_t::Type::TABLE_SEPARATOR) {
                    flush_paragraph();
                    table_block_t table;
                    while (i < lines.size()) {
                        const classified_line_t row_line = classify_line(lines[i]);
                        if (row_line.type == classified_line_t::Type::TABLE_SEPARATOR) {
                            table.has_header = true;
                            ++i;
                            continue;
                        }
                        if (row_line.type != classified_line_t::Type::TABLE_ROW) {
                            break;
                        }

                        table_row_t row;
                        const auto cells = split_table_cells(row_line.content);
                        for (const auto& cell_text : cells) {
                            table_cell_t cell;
                            cell.runs = parse_inline(cell_text);
                            row.cells.push_back(std::move(cell));
                        }
                        table.rows.push_back(std::move(row));
                        ++i;
                    }
                    blocks.push_back(std::move(table));
                }
                else {
                    if (!paragraph_accum.empty()) {
                        paragraph_accum.push_back(' ');
                    }
                    paragraph_accum += cl.content;
                    ++i;
                }
                break;
            }

            case classified_line_t::Type::TABLE_SEPARATOR:
                if (!paragraph_accum.empty()) {
                    paragraph_accum.push_back(' ');
                }
                paragraph_accum += cl.content;
                ++i;
                break;

            case classified_line_t::Type::PAGE_BREAK:
                flush_paragraph();
                blocks.push_back(page_break_block_t{});
                ++i;
                break;

            case classified_line_t::Type::TEXT:
            default:
                if (!paragraph_accum.empty()) {
                    paragraph_accum.push_back(' ');
                }
                paragraph_accum += cl.content;
                ++i;
                break;
        }
    }

    flush_paragraph();
    return blocks;
}

} // namespace mark2haru
