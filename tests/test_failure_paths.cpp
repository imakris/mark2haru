#include <mark2haru/render.h>
#include <mark2haru/ttf_font.h>

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

std::vector<std::uint8_t> read_file_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

bool write_file_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::uint16_t read_u16_be(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

void write_u32_be(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset    ] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >>  8) & 0xFF);
    bytes[offset + 3] = static_cast<std::uint8_t>( value        & 0xFF);
}

std::size_t table_record_offset(const std::vector<std::uint8_t>& bytes, const char tag[5])
{
    if (bytes.size() < 12) {
        return std::string::npos;
    }

    const std::uint16_t num_tables = read_u16_be(bytes, 4);
    std::size_t table_dir = 12;
    for (std::uint16_t i = 0; i < num_tables; ++i, table_dir += 16) {
        if (table_dir + 16 > bytes.size()) {
            return std::string::npos;
        }
        const std::string record_tag(
            reinterpret_cast<const char*>(&bytes[table_dir]),
            4);
        if (record_tag == std::string(tag, 4)) {
            return table_dir;
        }
    }
    return std::string::npos;
}

bool patch_table_length(
    const fs::path& path,
    const char      tag[5],
    std::uint32_t   length)
{
    std::vector<std::uint8_t> bytes = read_file_bytes(path);
    if (bytes.empty()) {
        return false;
    }

    const std::size_t record_offset = table_record_offset(bytes, tag);
    if (record_offset == std::string::npos) {
        return false;
    }

    write_u32_be(bytes, record_offset + 12, length);
    return write_file_bytes(path, bytes);
}

fs::path find_repo_fonts_dir(const fs::path& exe_path)
{
    const std::vector<fs::path> candidates = {
        fs::current_path() / "fonts",
        fs::current_path().parent_path() / "fonts",
        exe_path.parent_path() / "fonts",
        exe_path.parent_path().parent_path() / "fonts",
    };

    for (const fs::path& candidate : candidates) {
        if (fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return {};
}

bool expect_font_load_failure(
    const fs::path& temp_dir,
    const fs::path& source_font,
    const char      tag[5],
    std::uint32_t   patched_length,
    const char*     label)
{
    const fs::path target = temp_dir / (std::string(label) + ".ttf");
    fs::copy_file(source_font, target, fs::copy_options::overwrite_existing);
    if (!patch_table_length(target, tag, patched_length)) {
        std::fprintf(stderr, "failed to patch %s table length\n", label);
        return false;
    }

    mark2haru::True_type_font font;
    if (font.load_from_file(target)) {
        std::fprintf(stderr, "corrupted font unexpectedly loaded: %s\n", label);
        return false;
    }

    return true;
}

bool expect_render_font_failure(const fs::path& temp_dir)
{
    const fs::path bogus_font = temp_dir / "bogus_font.bin";
    {
        std::ofstream out(bogus_font, std::ios::binary);
        out << "not a font";
    }

    mark2haru::Render_options options;
    options.font_family.regular = mark2haru::Font_source::from_path(bogus_font);

    std::string error;
    if (mark2haru::render_markdown_to_pdf(
            "hello\n",
            temp_dir / "unused.pdf",
            options,
            error))
    {
        std::fprintf(stderr, "render unexpectedly succeeded with bogus font file\n");
        return false;
    }

    if (error.find("Unable to load font file") == std::string::npos) {
        std::fprintf(stderr, "unexpected font-load error: %s\n", error.c_str());
        return false;
    }

    return true;
}

// PDF parsers require '.' as the decimal separator. setlocale to a locale
// that uses ',' when one is available and verify that rendering still
// emits a parseable PDF. That means number formatting in the writer is
// genuinely locale-independent.
bool expect_render_locale_independent(const fs::path& temp_dir, const fs::path& font_root_dir)
{
    const char* candidate_locales[] = {
        "de_DE.UTF-8",
        "de_DE.utf8",
        "de_DE",
        "fr_FR.UTF-8",
        "fr_FR.utf8",
        "fr_FR",
    };
    const char* applied = nullptr;
    for (const char* loc : candidate_locales) {
        if (std::setlocale(LC_NUMERIC, loc) != nullptr) {
            const std::lconv* conv = std::localeconv();
            if (conv && conv->decimal_point && std::strcmp(conv->decimal_point, ",") == 0) {
                applied = loc;
                break;
            }
        }
    }
    if (!applied) {
        std::setlocale(LC_NUMERIC, "C");
        std::fprintf(stderr, "skipping locale-safety test: no decimal-comma locale available\n");
        return true;
    }

    mark2haru::Render_options options;
    options.font_root_dir = font_root_dir;

    const fs::path out_path = temp_dir / "locale_test.pdf";
    std::string error;
    const bool ok = mark2haru::render_markdown_to_pdf(
        "x\n",
        out_path,
        options,
        error);
    std::setlocale(LC_NUMERIC, "C");

    if (!ok) {
        std::fprintf(stderr, "locale render failed (locale=%s): %s\n", applied, error.c_str());
        return false;
    }

    const std::vector<std::uint8_t> bytes = read_file_bytes(out_path);
    if (bytes.empty()) {
        std::fprintf(stderr, "locale render produced empty file\n");
        return false;
    }
    // PDF allows '.' but never ',' as a decimal separator inside number
    // tokens. Scan the file but skip over stream..endstream blocks because
    // those are arbitrary compressed bytes that can coincidentally match
    // a `d,d` pattern. Inside the PDF dictionary tokens we control, any
    // float emitted with ',' as the separator is a locale-honouring bug.
    const std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::size_t scan = 0;
    while (scan < raw.size()) {
        const std::size_t stream_pos = raw.find("stream\n", scan);
        const std::size_t scan_end = stream_pos == std::string::npos ? raw.size() : stream_pos;
        for (std::size_t i = scan + 1; i + 1 < scan_end; ++i) {
            const unsigned char a = static_cast<unsigned char>(raw[i - 1]);
            const unsigned char b = static_cast<unsigned char>(raw[i]);
            const unsigned char c = static_cast<unsigned char>(raw[i + 1]);
            if (a >= '0' && a <= '9' && b == ',' && c >= '0' && c <= '9') {
                std::fprintf(stderr,
                    "found locale-formatted float `%c,%c` in PDF dict (locale=%s)\n",
                    static_cast<char>(a), static_cast<char>(c), applied);
                return false;
            }
        }
        if (stream_pos == std::string::npos) {
            break;
        }
        const std::size_t end_pos = raw.find("\nendstream", stream_pos);
        if (end_pos == std::string::npos) {
            break;
        }
        scan = end_pos + std::string("\nendstream").size();
    }
    return true;
}

bool expect_render_save_failure(const fs::path& temp_dir, const fs::path& font_root_dir)
{
    mark2haru::Render_options options;
    options.font_root_dir = font_root_dir;

    const fs::path missing_parent = temp_dir / "missing_parent";
    fs::remove_all(missing_parent);

    std::string error;
    if (mark2haru::render_markdown_to_pdf(
            "hello\n",
            missing_parent / "out.pdf",
            options,
            error))
    {
        std::fprintf(stderr, "render unexpectedly succeeded for unwritable output path\n");
        return false;
    }

    if (error.find("Failed to write PDF file:") == std::string::npos) {
        std::fprintf(stderr, "unexpected save error: %s\n", error.c_str());
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 1) {
        return 2;
    }

    const fs::path exe_path = fs::path(argv[0]);
    const fs::path fonts_dir = find_repo_fonts_dir(exe_path);
    if (fonts_dir.empty()) {
        std::fprintf(stderr, "unable to locate repo fonts directory\n");
        return 3;
    }

    const fs::path source_font = fonts_dir / "DejaVuSans.ttf";
    if (!fs::is_regular_file(source_font)) {
        std::fprintf(stderr, "missing source font: %s\n", source_font.string().c_str());
        return 4;
    }

    const fs::path temp_dir = fs::temp_directory_path() / "mark2haru_failure_paths";
    fs::remove_all(temp_dir);
    fs::create_directories(temp_dir);

    if (!expect_font_load_failure(temp_dir, source_font, "head", 10, "head_short")) {
        return 5;
    }
    if (!expect_font_load_failure(temp_dir, source_font, "hmtx", 0, "hmtx_short")) {
        return 6;
    }
    if (!expect_font_load_failure(temp_dir, source_font, "cmap", 10, "cmap_short")) {
        return 7;
    }
    if (!expect_render_font_failure(temp_dir)) {
        return 8;
    }
    if (!expect_render_save_failure(temp_dir, fonts_dir)) {
        return 9;
    }
    if (!expect_render_locale_independent(temp_dir, fonts_dir)) {
        return 10;
    }

    return 0;
}
