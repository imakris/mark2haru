#include "markdown.h"

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

size_t find_closing(std::string_view s, size_t start, std::string_view delim)
{
    for (size_t pos = start; pos + delim.size() <= s.size(); ++pos) {
        if (s[pos] == '\n') {
            return std::string::npos;
        }
        if (s.substr(pos, delim.size()) == delim) {
            return pos;
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

std::vector<InlineRun> parse_inline(const std::string& text)
{
    std::vector<InlineRun> runs;
    std::string current;
    size_t i = 0;

    auto flush = [&](InlineStyle style = InlineStyle::Normal) {
        if (!current.empty()) {
            runs.push_back({ current, style });
            current.clear();
        }
    };

    auto can_open_underscore = [&](size_t pos) {
        return pos == 0 || !is_word_char(text[pos - 1]);
    };

    while (i < text.size()) {
        if (starts_with(text, i, "***")) {
            const size_t close = find_closing(text, i + 3, "***");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3), InlineStyle::BoldItalic });
                i = close + 3;
                continue;
            }
        }

        if (starts_with(text, i, "**")) {
            const size_t close = find_closing(text, i + 2, "**");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2), InlineStyle::Bold });
                i = close + 2;
                continue;
            }
        }

        if (text[i] == '*' && !starts_with(text, i, "**")) {
            const size_t close = find_closing(text, i + 1, "*");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1), InlineStyle::Italic });
                i = close + 1;
                continue;
            }
        }

        if (starts_with(text, i, "___") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 3, 3);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3), InlineStyle::BoldItalic });
                i = close + 3;
                continue;
            }
        }

        if (starts_with(text, i, "__") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 2, 2);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2), InlineStyle::Bold });
                i = close + 2;
                continue;
            }
        }

        if (text[i] == '_' && !starts_with(text, i, "__") && can_open_underscore(i)) {
            const size_t close = find_closing_underscore(text, i + 1, 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1), InlineStyle::Italic });
                i = close + 1;
                continue;
            }
        }

        if (text[i] == '`') {
            const size_t close = text.find('`', i + 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1), InlineStyle::Code });
                i = close + 1;
                continue;
            }
        }

        if (text[i] == '[') {
            const size_t bracket_close = text.find("](", i + 1);
            if (bracket_close != std::string::npos) {
                const size_t paren_close = text.find(')', bracket_close + 2);
                if (paren_close != std::string::npos) {
                    const std::string display = text.substr(i + 1, bracket_close - i - 1);
                    const std::string url = text.substr(bracket_close + 2,
                                                        paren_close - bracket_close - 2);
                    if (display.empty()) {
                        current += url;
                    } else if (display == url) {
                        current += display;
                    } else {
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

struct ClassifiedLine {
    enum class Type {
        Empty,
        Heading,
        BulletItem,
        OrderedItem,
        TableRow,
        TableSeparator,
        PageBreak,
        Text,
    } type = Type::Text;
    std::string content;
    int heading_level = 0;
    int list_number = 0;
};

ClassifiedLine classify_line(const std::string& line)
{
    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
        return { ClassifiedLine::Type::Empty, {}, 0, 0 };
    }

    if (trimmed == "<!-- pagebreak -->") {
        return { ClassifiedLine::Type::PageBreak, {}, 0, 0 };
    }

    if (trimmed.rfind("```", 0) == 0) {
        return { ClassifiedLine::Type::Text, trimmed, 0, 0 };
    }

    if (trimmed.rfind('#', 0) == 0) {
        int level = 0;
        while (level < 6 && level < static_cast<int>(trimmed.size()) && trimmed[level] == '#') {
            ++level;
        }
        if (level > 0 && level < static_cast<int>(trimmed.size()) && trimmed[level] == ' ') {
            return { ClassifiedLine::Type::Heading, trim(trimmed.substr(level + 1)), level, 0 };
        }
    }

    if (trimmed.size() >= 2
        && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')
        && trimmed[1] == ' ') {
        return { ClassifiedLine::Type::BulletItem, trim(trimmed.substr(2)), 0, 0 };
    }

    size_t pos = 0;
    while (pos < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
        ++pos;
    }
    if (pos > 0 && pos + 1 < trimmed.size() && trimmed[pos] == '.' && trimmed[pos + 1] == ' ') {
        return {
            ClassifiedLine::Type::OrderedItem,
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
            separator ? ClassifiedLine::Type::TableSeparator : ClassifiedLine::Type::TableRow,
            trimmed,
            0,
            0
        };
    }

    return { ClassifiedLine::Type::Text, rtrim(line), 0, 0 };
}

std::vector<std::string> split_table_cells(const std::string& row)
{
    std::vector<std::string> cells;
    size_t i = 0;
    if (!row.empty() && row.front() == '|') {
        ++i;
    }
    while (i < row.size()) {
        const size_t pipe = row.find('|', i);
        if (pipe == std::string::npos) {
            break;
        }
        cells.push_back(trim(row.substr(i, pipe - i)));
        i = pipe + 1;
    }
    return cells;
}

} // namespace

std::vector<Block> parse_markdown(const std::string& input)
{
    std::vector<Block> blocks;
    const auto lines = split_lines(input);
    size_t i = 0;
    std::string paragraph_accum;

    auto flush_paragraph = [&]() {
        if (paragraph_accum.empty()) {
            return;
        }
        ParagraphBlock pb;
        pb.runs = parse_inline(paragraph_accum);
        blocks.push_back(std::move(pb));
        paragraph_accum.clear();
    };

    while (i < lines.size()) {
        const std::string trimmed = trim(lines[i]);

        if (trimmed.rfind("```", 0) == 0) {
            flush_paragraph();
            CodeBlock code;
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

        const ClassifiedLine cl = classify_line(lines[i]);

        switch (cl.type) {
        case ClassifiedLine::Type::Empty:
            flush_paragraph();
            ++i;
            break;

        case ClassifiedLine::Type::Heading: {
            flush_paragraph();
            HeadingBlock hb;
            hb.level = cl.heading_level;
            hb.runs = parse_inline(cl.content);
            blocks.push_back(std::move(hb));
            ++i;
            break;
        }

        case ClassifiedLine::Type::BulletItem:
        case ClassifiedLine::Type::OrderedItem: {
            flush_paragraph();
            const bool ordered = cl.type == ClassifiedLine::Type::OrderedItem;
            ListBlock lb;
            lb.ordered = ordered;
            lb.start_number = cl.list_number > 0 ? cl.list_number : 1;

            while (i < lines.size()) {
                const ClassifiedLine item_line = classify_line(lines[i]);
                const bool matching =
                    (ordered && item_line.type == ClassifiedLine::Type::OrderedItem)
                    || (!ordered && item_line.type == ClassifiedLine::Type::BulletItem);
                if (matching) {
                    ListItem item;
                    item.runs = parse_inline(item_line.content);
                    lb.items.push_back(std::move(item));
                    ++i;
                    continue;
                }

                if (!lb.items.empty() && item_line.type == ClassifiedLine::Type::Text
                    && !lines[i].empty()
                    && (lines[i][0] == ' ' || lines[i][0] == '\t')) {
                    auto& last = lb.items.back();
                    const std::string cont = " " + item_line.content;
                    auto cont_runs = parse_inline(cont);
                    last.runs.insert(last.runs.end(),
                                     std::make_move_iterator(cont_runs.begin()),
                                     std::make_move_iterator(cont_runs.end()));
                    ++i;
                    continue;
                }

                break;
            }

            blocks.push_back(std::move(lb));
            break;
        }

        case ClassifiedLine::Type::TableRow: {
            if (i + 1 < lines.size()
                && classify_line(lines[i + 1]).type == ClassifiedLine::Type::TableSeparator) {
                flush_paragraph();
                TableBlock table;
                while (i < lines.size()) {
                    const ClassifiedLine row_line = classify_line(lines[i]);
                    if (row_line.type == ClassifiedLine::Type::TableSeparator) {
                        table.has_header = true;
                        ++i;
                        continue;
                    }
                    if (row_line.type != ClassifiedLine::Type::TableRow) {
                        break;
                    }

                    TableRow row;
                    const auto cells = split_table_cells(row_line.content);
                    for (const auto& cell_text : cells) {
                        TableCell cell;
                        cell.runs = parse_inline(cell_text);
                        row.cells.push_back(std::move(cell));
                    }
                    table.rows.push_back(std::move(row));
                    ++i;
                }
                blocks.push_back(std::move(table));
            } else {
                if (!paragraph_accum.empty()) {
                    paragraph_accum.push_back('\n');
                }
                paragraph_accum += cl.content;
                ++i;
            }
            break;
        }

        case ClassifiedLine::Type::TableSeparator:
            if (!paragraph_accum.empty()) {
                paragraph_accum.push_back('\n');
            }
            paragraph_accum += cl.content;
            ++i;
            break;

        case ClassifiedLine::Type::PageBreak:
            flush_paragraph();
            blocks.push_back(PageBreakBlock{});
            ++i;
            break;

        case ClassifiedLine::Type::Text:
        default:
            if (!paragraph_accum.empty()) {
                paragraph_accum.push_back('\n');
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
