#include <mark2haru/pdf_writer.h>

#include "miniz.h"
#include "test_common.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>(),
    };
}

std::string inflate_stream(const std::string& compressed)
{
    mz_ulong out_len = 4096;
    for (;;) {
        std::string out(out_len, '\0');
        mz_ulong actual_len = out_len;
        const int rc = mz_uncompress(
            reinterpret_cast<unsigned char*>(out.data()),
            &actual_len,
            reinterpret_cast<const unsigned char*>(compressed.data()),
            static_cast<mz_ulong>(compressed.size()));
        if (rc == MZ_OK) {
            out.resize(actual_len);
            return out;
        }
        if (rc != MZ_BUF_ERROR) {
            return {};
        }
        out_len *= 2;
    }
}

std::string decoded_pdf_streams(const std::string& pdf)
{
    std::string decoded;
    std::size_t scan = 0;
    while (true) {
        const std::size_t stream_pos = pdf.find("stream\n", scan);
        if (stream_pos == std::string::npos) {
            break;
        }

        const std::size_t payload_pos = stream_pos + 7;
        const std::size_t end_pos     = pdf.find("\nendstream", payload_pos);
        if (end_pos == std::string::npos) {
            break;
        }

        const std::size_t dict_start = pdf.rfind("<<", stream_pos);
        const std::string dict = dict_start == std::string::npos
            ? std::string()
            : pdf.substr(dict_start, stream_pos - dict_start);
        const std::string payload = pdf.substr(payload_pos, end_pos - payload_pos);
        if (dict.find("/FlateDecode") != std::string::npos) {
            decoded += inflate_stream(payload);
        }
        else {
            decoded += payload;
        }
        decoded.push_back('\n');
        scan = end_pos + 10;
    }
    return decoded;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

struct cmap_group_t
{
    std::uint32_t first;
    std::uint32_t last;
    std::uint32_t gid;
};

void write_u32(std::string& bytes, std::size_t offset, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<char>(value >> (24 - 8 * i));
    }
}

std::uint32_t read_u32(const std::string& bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    }
    return value;
}

std::uint32_t checksum(const std::string& bytes)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        sum += read_u32(bytes, i);
    }
    return sum;
}

// The bundled face has no glyph aliases. Replace only its cmap, retaining
// real glyph outlines and metrics so the writer's two mappings can be checked
// independently. Format 12 also exercises supplementary Unicode scalars.
bool write_alias_font(
    const mark2haru::True_type_font& face,
    const fs::path&                 path,
    const std::vector<cmap_group_t>& groups)
{
    const auto& source = face.bytes();
    std::string bytes(source.begin(), source.end());
    std::string cmap(28 + groups.size() * 12, '\0');
    cmap[3] = 1; // One Windows Unicode full-repertoire subtable.
    cmap[5] = 3;
    cmap[7] = 10;
    write_u32(cmap, 8, 12);
    cmap[13] = 12;
    write_u32(cmap, 16, static_cast<std::uint32_t>(cmap.size() - 12));
    write_u32(cmap, 24, static_cast<std::uint32_t>(groups.size()));
    for (std::size_t i = 0; i < groups.size(); ++i) {
        write_u32(cmap, 28 + i * 12,     groups[i].first);
        write_u32(cmap, 28 + i * 12 + 4, groups[i].last);
        write_u32(cmap, 28 + i * 12 + 8, groups[i].gid);
    }

    const auto read_u16 = [&bytes](std::size_t offset) {
        return (static_cast<unsigned char>(bytes[offset]) << 8) |
                static_cast<unsigned char>(bytes[offset + 1]);
    };
    const int count = read_u16(4);
    std::size_t cmap_record = 0;
    std::size_t head_offset = 0;
    for (int i = 0; i < count; ++i) {
        const std::size_t record = 12 + i * 16;
        if (bytes.compare(record, 4, "head") == 0) {
            head_offset = read_u32(bytes, record + 8);
        }
        if (bytes.compare(record, 4, "cmap") == 0) {
            cmap_record = record;
        }
    }
    if (cmap_record == 0 || head_offset == 0) {
        return false;
    }
    bytes.resize((bytes.size() + 3) & ~std::size_t(3), '\0');
    write_u32(bytes, cmap_record + 4, checksum(cmap));
    write_u32(bytes, cmap_record + 8, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, cmap_record + 12, static_cast<std::uint32_t>(cmap.size()));
    bytes += cmap;
    write_u32(bytes, head_offset + 8, 0);
    write_u32(bytes, head_offset + 8, 0xB1B0AFBA - checksum(bytes));
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

std::shared_ptr<mark2haru::Measurement_context> load_alias_metrics(
    const fs::path& path, const fs::path& font_root)
{
    auto family = mark2haru::Font_family_config::bundled_default();
    family.regular = mark2haru::Font_source::from_path(path);
    return std::make_shared<mark2haru::Measurement_context>(family, font_root);
}

std::string referenced_stream(const std::string& pdf, const std::string& key)
{
    const auto key_pos = pdf.find(key);
    if (key_pos == std::string::npos) {
        return {};
    }
    std::istringstream reference(pdf.substr(key_pos + key.size()));
    int object_id = 0;
    reference >> object_id;
    const auto object_pos = pdf.find("\n" + std::to_string(object_id) + " 0 obj\n");
    if (object_pos == std::string::npos) {
        return {};
    }
    const auto object_end = pdf.find("\nendobj", object_pos);
    std::string stream = decoded_pdf_streams(pdf.substr(object_pos, object_end - object_pos));
    if (!stream.empty()) {
        stream.pop_back(); // decoded_pdf_streams separates streams with a newline.
    }
    return stream;
}

bool check_unicode_mapping(
    const fs::path& exe_dir, const mark2haru::True_type_font& face)
{
    const auto space_gid = face.glyph_for_codepoint(' ');
    const auto query_gid = face.glyph_for_codepoint('?');
    const auto omega_gid = face.glyph_for_codepoint(0x03A9);
    const fs::path font_path = exe_dir / "test_alias.ttf";
    if (!write_alias_font(face, font_path, {
            { 0x0020,  0x0020,  space_gid },
            { 0x003F,  0x003F,  query_gid },
            { 0x00A0,  0x00A0,  space_gid },
            { 0x03A9,  0x03A9,  omega_gid },
            { 0x2126,  0x2126,  omega_gid },
            { 0x1F600, 0x1F600, omega_gid },
        }))
    {
        std::cerr << "could not create alias font fixture\n";
        return false;
    }
    auto metrics = load_alias_metrics(font_path, exe_dir);
    const auto& alias = metrics->font_face(mark2haru::Pdf_font::REGULAR);
    if (!metrics->loaded() || alias.glyph_for_codepoint(0x2126) != omega_gid ||
        alias.glyph_for_codepoint(0x1F600) != omega_gid || alias.glyph_for_codepoint(0x10FFFF) != 0)
    {
        std::cerr << "alias font fixture does not have the required mappings\n";
        return false;
    }

    mark2haru::Pdf_writer writer(200, 200, metrics);
    writer.draw_text(10, 20, 12, mark2haru::Pdf_font::REGULAR, u8"\u03A9\u2126\u03A9 \u00A0\U0001F600?");
    writer.begin_page();
    writer.draw_text(10, 20, 12, mark2haru::Pdf_font::REGULAR, u8"\u2126\u03A9\U0010FFFF?");
    const fs::path output = exe_dir / "test_pdf_unicode.pdf";
    if (!writer.save(output)) {
        std::cerr << "alias PDF save failed\n";
        return false;
    }
    const std::string pdf     = read_file(output);
    const std::string streams = decoded_pdf_streams(pdf);
    const std::string cmap    = referenced_stream(pdf, "/ToUnicode ");
    const std::string gid_map = referenced_stream(pdf, "/CIDToGIDMap ");
    std::string expected_map(2, '\0');
    for (auto gid : { omega_gid, omega_gid, space_gid, space_gid, omega_gid, query_gid }) {
        expected_map.push_back(static_cast<char>(gid >> 8));
        expected_map.push_back(static_cast<char>(gid & 0xFF));
    }
    if (!contains(streams, "<0001000200010003000400050006> Tj") ||
        !contains(streams, "<0002000100060006> Tj") ||
        !contains(cmap, "6 beginbfchar\n<0001> <03A9>\n<0002> <2126>\n") ||
        !contains(cmap, "<0003> <0020>\n<0004> <00A0>\n<0005> <D83DDE00>\n<0006> <003F>\n") ||
        gid_map != expected_map)
    {
        std::cerr << "alias characters, repeated CIDs, supplementary text or '?' substitution changed\n";
        return false;
    }
    std::ostringstream widths;
    widths << "/W [ ";
    std::size_t cid = 1;
    for (auto gid : { omega_gid, omega_gid, space_gid, space_gid, omega_gid, query_gid }) {
        const auto width = (static_cast<std::uint64_t>(face.advance_width_for_gid(gid)) * 1000 +
            face.units_per_em() / 2) / face.units_per_em();
        widths << cid++ << " [" << width << "] ";
    }
    widths << ']';
    if (!contains(pdf, widths.str())) {
        std::cerr << "CID widths no longer match the rendered glyphs\n";
        return false;
    }
    return true;
}

std::string supplementary_utf8(std::uint32_t cp)
{
    return {
        static_cast<char>(0xF0 |  (cp >> 18)),
        static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
        static_cast<char>(0x80 | ((cp >>  6) & 0x3F)),
        static_cast<char>(0x80 | ( cp        & 0x3F)),
    };
}

bool check_cid_capacity(const fs::path& exe_dir, const mark2haru::True_type_font& face)
{
    // Sixteen blocks reuse valid glyphs to reach the CID limit without
    // requiring a font with more glyphs than TrueType can represent.
    if (face.num_glyphs() <= 4096) {
        std::cerr << "capacity fixture requires at least 4097 glyphs\n";
        return false;
    }
    std::vector<cmap_group_t> groups;
    for (std::uint32_t i = 0; i < 16; ++i) {
        groups.push_back({ 0x10000 + i * 4096, 0x10FFF + i * 4096, 1 });
    }
    const fs::path font_path = exe_dir / "test_capacity.ttf";
    if (!write_alias_font(face, font_path, groups)) {
        return false;
    }
    auto metrics = load_alias_metrics(font_path, exe_dir);
    if (!metrics->loaded()) {
        return false;
    }
    std::string text;
    for (std::uint32_t cp = 0x10000; cp < 0x1FFFF; ++cp) {
        text += supplementary_utf8(cp);
    }
    mark2haru::Pdf_writer writer(200, 200, metrics);
    writer.draw_text(10, 20, 12, mark2haru::Pdf_font::REGULAR, text);
    writer.draw_text(10, 40, 12, mark2haru::Pdf_font::REGULAR, supplementary_utf8(0x1FFFE));
    const fs::path output = exe_dir / "test_pdf_capacity.pdf";
    if (!writer.save(output)) {
        std::cerr << "the last valid CID, including reuse at capacity, must save\n";
        return false;
    }
    const std::string before = read_file(output);
    const std::string cmap   = referenced_stream(before, "/ToUnicode ");
    if (!contains(cmap, "<FFFF> <D83FDFFE>")) {
        std::cerr << "last CID or supplementary Unicode mapping wrapped\n";
        return false;
    }
    writer.draw_text(10, 60, 12, mark2haru::Pdf_font::REGULAR, supplementary_utf8(0x1FFFF));
    if (writer.save(output) || read_file(output) != before) {
        std::cerr << "CID exhaustion must fail without replacing the destination\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    const fs::path exe_dir  = mark2haru_test::executable_dir(argc, argv);
    const fs::path out_path = exe_dir / "test_pdf_writer.pdf";

    auto metrics = mark2haru_test::load_default_metrics(exe_dir);
    if (!metrics) {
        return 1;
    }

    mark2haru::Pdf_writer writer(200.0, 200.0, metrics);
    if (!writer.fonts_loaded()) {
        std::cerr << writer.font_error() << "\n";
        return 2;
    }

    writer.draw_text(
        10.0,
        20.0,
        12.0,
        mark2haru::Pdf_font::REGULAR,
        "colored text",
        { 0.25, 0.5, 0.75 });
    writer.stroke_line(
        10.0,
        50.0,
        90.0,
        70.0,
        { 0.8, 0.1, 0.2 },
        2.5);

    if (!writer.save(out_path)) {
        std::cerr << "save failed\n";
        return 3;
    }

    const std::string streams = decoded_pdf_streams(read_file(out_path));
    if (!contains(streams, "0.25 0.5 0.75 rg")) {
        std::cerr << "colored text fill color operator missing\n";
        return 4;
    }
    if (!contains(streams, "0.8 0.1 0.2 RG")) {
        std::cerr << "line stroke color operator missing\n";
        return 5;
    }
    if (!contains(streams, "2.5 w")) {
        std::cerr << "line width operator missing\n";
        return 6;
    }
    if (!contains(streams, "10 150 m\n90 130 l\nS")) {
        std::cerr << "line geometry operators missing\n";
        return 7;
    }

    if (!check_unicode_mapping(exe_dir, metrics->font_face(mark2haru::Pdf_font::REGULAR))) {
        return 8;
    }
    if (!check_cid_capacity(exe_dir, metrics->font_face(mark2haru::Pdf_font::REGULAR))) {
        return 9;
    }
    return 0;
}
