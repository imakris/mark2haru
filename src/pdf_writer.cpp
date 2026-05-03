#include <mark2haru/pdf_writer.h>

#include "utf8_decode.h"

#include "miniz.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

// Locale-independent number formatting: std::ostringstream and std::printf
// both honour the global LC_NUMERIC, which on a non-C locale would emit
// `1,5` instead of `1.5` and produce an unparseable PDF. std::to_chars is
// guaranteed to use '.' for the decimal separator regardless of locale.
template <class T>
std::string number_to_string(T value)
{
    const double v = static_cast<double>(value);
    if (!std::isfinite(v)) {
        return "0";
    }
    char buf[64];
    const auto result = std::to_chars(
        buf, buf + sizeof(buf),
        v, std::chars_format::fixed, 3);
    if (result.ec != std::errc{}) {
        return "0";
    }
    std::string out(buf, result.ptr);

    if (out.find('.') != std::string::npos) {
        const std::size_t last = out.find_last_not_of('0');
        if (last != std::string::npos) {
            out.erase(last + 1);
        }
        if (!out.empty() && out.back() == '.') {
            out.pop_back();
        }
    }
    if (out == "-0") {
        out = "0";
    }
    if (out.empty()) {
        out = "0";
    }
    return out;
}

double clamp_unit(double v)
{
    if (!std::isfinite(v)) {
        return 0.0;
    }
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

std::string hex_byte(std::uint8_t byte)
{
    static constexpr char k_digits[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(k_digits[(byte >> 4) & 0xF]);
    out.push_back(k_digits[byte & 0xF]);
    return out;
}

std::string utf16be_hex_from_codepoint(std::uint32_t codepoint)
{
    std::string out;
    if (codepoint <= 0xFFFF) {
        const std::uint16_t u = static_cast<std::uint16_t>(codepoint);
        out += hex_byte(static_cast<std::uint8_t>((u >> 8) & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>(u & 0xFF));
    }
    else {
        const std::uint32_t cp = codepoint - 0x10000;
        const std::uint16_t high = static_cast<std::uint16_t>(0xD800 + (cp >> 10));
        const std::uint16_t low = static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF));
        out += hex_byte(static_cast<std::uint8_t>((high >> 8) & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>(high & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>((low >> 8) & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>(low & 0xFF));
    }
    return out;
}

std::string pdf_hex_string(const std::vector<std::uint16_t>& gids)
{
    std::string out;
    out.reserve(gids.size() * 4 + 2);
    out.push_back('<');
    for (std::uint16_t gid : gids) {
        out += hex_byte(static_cast<std::uint8_t>((gid >> 8) & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>(gid & 0xFF));
    }
    out.push_back('>');
    return out;
}

double scale_1000(std::int16_t value, std::uint16_t units_per_em)
{
    if (units_per_em == 0) {
        return static_cast<double>(value);
    }
    return static_cast<double>(value) * 1000.0 / static_cast<double>(units_per_em);
}

std::vector<std::uint16_t> sorted_used_glyphs(const std::unordered_set<std::uint16_t>& set)
{
    std::vector<std::uint16_t> out(set.begin(), set.end());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

Pdf_writer::Pdf_writer(
    double page_width_pt,
    double page_height_pt,
    std::shared_ptr<const Measurement_context> metrics)
:
    m_page_width_pt(page_width_pt),
    m_page_height_pt(page_height_pt),
    m_metrics(std::move(metrics))
{
    begin_page();
    m_fonts_loaded = m_metrics && m_metrics->loaded();
    if (!m_fonts_loaded) {
        m_font_error = m_metrics ? m_metrics->error() : "No measurement context";
    }
}

bool Pdf_writer::fonts_loaded() const
{
    return m_fonts_loaded;
}

std::string& Pdf_writer::current_content()
{
    return current_page().content;
}

Pdf_writer::Page& Pdf_writer::current_page()
{
    if (m_pages.empty()) {
        begin_page();
    }
    return m_pages.back();
}

void Pdf_writer::begin_page()
{
    m_pages.push_back(Page{});
}

void Pdf_writer::append_color(std::string& out, const color_t& color, bool stroke)
{
    out += number_to_string(clamp_unit(color.r));
    out.push_back(' ');
    out += number_to_string(clamp_unit(color.g));
    out.push_back(' ');
    out += number_to_string(clamp_unit(color.b));
    out.push_back(' ');
    out += stroke ? "RG\n" : "rg\n";
}

std::string Pdf_writer::font_resource_name(Pdf_font font)
{
    switch (font) {
        case Pdf_font::REGULAR:     return "/F1";
        case Pdf_font::BOLD:        return "/F2";
        case Pdf_font::ITALIC:      return "/F3";
        case Pdf_font::BOLD_ITALIC: return "/F4";
        case Pdf_font::MONO:        return "/F5";
        default:                    return "/F1";
    }
}

std::vector<Pdf_font> Pdf_writer::used_fonts(const std::array<Loaded_font, 5>& fonts)
{
    std::vector<Pdf_font> out;
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        if (!fonts[i].used_glyphs.empty()) {
            out.push_back(static_cast<Pdf_font>(i));
        }
    }
    return out;
}

namespace {

std::string flate_encode_raw(const unsigned char* data, std::size_t size)
{
    if (size == 0) {
        return {};
    }
    uLongf dest_len = compressBound(static_cast<uLong>(size));
    std::string out(dest_len, '\0');
    const int rc = compress2(
        reinterpret_cast<Bytef*>(&out[0]),
        &dest_len,
        reinterpret_cast<const Bytef*>(data),
        static_cast<uLong>(size),
        Z_BEST_COMPRESSION);
    if (rc != Z_OK) {
        return std::string(reinterpret_cast<const char*>(data), size);
    }
    out.resize(dest_len);
    return out;
}

} // namespace

namespace {

// Builds `<< /Length N [/Filter /FlateDecode] [extra] >>\nstream\n...payload...\nendstream`.
// Compresses when the result is strictly smaller than the raw payload.
std::string build_stream_object(
    const unsigned char* data,
    std::size_t          size,
    const std::string&   extra_dict_entries = {})
{
    const std::string raw(reinterpret_cast<const char*>(data), size);
    const std::string compressed = flate_encode_raw(data, size);
    const bool use_compressed = !compressed.empty() && compressed.size() < raw.size();
    const std::string& payload = use_compressed ? compressed : raw;

    std::string out = "<< /Length " + std::to_string(payload.size());
    if (use_compressed) {
        out += " /Filter /FlateDecode";
    }
    out += extra_dict_entries;
    out += " >>\nstream\n";
    out.append(payload);
    out += "\nendstream";
    return out;
}

std::string build_stream_object(const std::string& payload, const std::string& extra = {})
{
    return build_stream_object(
        reinterpret_cast<const unsigned char*>(payload.data()),
        payload.size(),
        extra);
}

std::string build_stream_object(const std::vector<std::uint8_t>& payload, const std::string& extra = {})
{
    return build_stream_object(payload.data(), payload.size(), extra);
}

// Allocates 1-based PDF object IDs on demand and lets callers fill in the
// body any time before write(). Eliminates the manual "next_obj++" arithmetic
// and the implicit invariant that body order matches reservation order.
class Pdf_object_table
{
public:
    int reserve()
    {
        ++m_count;
        return m_count;
    }

    void emit(int id, std::string body)
    {
        const auto idx = static_cast<std::size_t>(id);
        if (idx >= m_bodies.size()) {
            m_bodies.resize(idx + 1);
        }
        m_bodies[idx] = std::move(body);
    }

    bool write(std::ostream& out, int catalog_id) const
    {
        const std::string header = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        if (!out) {
            return false;
        }

        std::vector<std::streamoff> offsets(static_cast<std::size_t>(m_count + 1), 0);
        for (int i = 1; i <= m_count; ++i) {
            const std::streamoff pos = out.tellp();
            if (pos < 0) {
                return false;
            }
            offsets[static_cast<std::size_t>(i)] = pos;

            const std::string& body = m_bodies[static_cast<std::size_t>(i)];
            out << i << " 0 obj\n";
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
            out << "\nendobj\n";
            if (!out) {
                return false;
            }
        }

        const std::streamoff xref_pos = out.tellp();
        if (xref_pos < 0) {
            return false;
        }
        out << "xref\n0 " << (m_count + 1) << "\n";
        out << "0000000000 65535 f \n";
        for (int i = 1; i <= m_count; ++i) {
            out << std::setw(10) << std::setfill('0')
                << offsets[static_cast<std::size_t>(i)] << " 00000 n \n";
        }
        out << "trailer\n<< /Size " << (m_count + 1)
            << " /Root " << catalog_id << " 0 R >>\nstartxref\n"
            << xref_pos << "\n%%EOF\n";
        return static_cast<bool>(out);
    }

private:
    int m_count = 0;
    std::vector<std::string> m_bodies = std::vector<std::string>(1); // index 0 unused (free entry)
};

} // namespace

std::string Pdf_writer::utf8_to_hex_cid_string(
    const True_type_font& font,
    Loaded_font& loaded,
    const std::string& text)
{
    std::vector<std::uint16_t> gids;
    gids.reserve(text.size());
    for (std::uint32_t cp : utf8::decode(text)) {
        std::uint16_t gid = font.glyph_for_codepoint(cp);
        std::uint32_t recorded_cp = cp;
        if (gid == 0) {
            const std::uint16_t fallback = font.glyph_for_codepoint('?');
            if (fallback != 0) {
                gid = fallback;
                recorded_cp = '?';
            }
        }
        gids.push_back(gid);
        if (gid != 0) {
            loaded.gid_to_unicode.emplace(gid, recorded_cp);
            loaded.used_glyphs.insert(gid);
        }
    }

    return pdf_hex_string(gids);
}

void Pdf_writer::draw_text(
    double x_pt,
    double y_top_pt,
    double size_pt,
    Pdf_font font,
    const std::string& text)
{
    draw_text(x_pt, y_top_pt, size_pt, font, text, { 0.0, 0.0, 0.0 });
}

void Pdf_writer::draw_text(
    double x_pt,
    double y_top_pt,
    double size_pt,
    Pdf_font font,
    const std::string& text,
    const color_t& color)
{
    if (!m_metrics) {
        return;
    }

    auto& slot = m_fonts[static_cast<std::size_t>(font)];
    const auto& face = m_metrics->font_face(font);
    const std::string encoded = utf8_to_hex_cid_string(face, slot, text);
    auto& out = current_content();
    out += "q\n";
    append_color(out, color, false);
    out += "BT\n";
    out += font_resource_name(font);
    out.push_back(' ');
    out += number_to_string(size_pt);
    out += " Tf\n";
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - size_pt)
        + " Td\n";
    out += encoded;
    out += " Tj\nET\nQ\n";
}

bool Pdf_writer::draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt, const Png_image& image)
{
    if (!image.loaded()) {
        return false;
    }

    const std::size_t image_index = m_images.size();
    Loaded_image stored;
    stored.image = image;
    stored.resource_name = "/Im" + std::to_string(image_index + 1);
    m_images.push_back(std::move(stored));
    current_page().image_indices.push_back(image_index);

    auto& out = current_content();
    out += "q\n";
    out += number_to_string(w_pt) + " 0 0 " + number_to_string(h_pt) + " "
        + number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - h_pt)
        + " cm\n";
    out += m_images.back().resource_name + " Do\nQ\n";
    return true;
}

bool Pdf_writer::draw_png(
    double x_pt,
    double y_top_pt,
    double w_pt,
    double h_pt,
    const fs::path& path)
{
    Png_image image;
    if (!image.load_from_file(path)) {
        return false;
    }
    return draw_png(x_pt, y_top_pt, w_pt, h_pt, image);
}

std::string Pdf_writer::make_to_unicode_cmap(const Loaded_font& font)
{
    std::ostringstream cmap;
    cmap << "/CIDInit /ProcSet findresource begin\n"
        << "12 dict begin\n"
        << "begincmap\n"
        << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        << "/CMapName /Adobe-Identity-UCS def\n"
        << "/CMapType 2 def\n"
        << "1 begincodespacerange\n"
        << "<0000> <FFFF>\n"
        << "endcodespacerange\n";

    std::vector<std::pair<std::uint16_t, std::uint32_t>> pairs;
    pairs.reserve(font.gid_to_unicode.size());
    for (const auto& kv : font.gid_to_unicode) {
        pairs.push_back(kv);
    }
    std::sort(
        pairs.begin(),
        pairs.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    const std::size_t chunk = 64;
    for (std::size_t i = 0; i < pairs.size(); i += chunk) {
        const std::size_t count = std::min(chunk, pairs.size() - i);
        cmap << count << " beginbfchar\n";
        for (std::size_t j = 0; j < count; ++j) {
            const auto& [gid, cp] = pairs[i + j];
            cmap << '<' << hex_byte(static_cast<std::uint8_t>((gid >> 8) & 0xFF))
                << hex_byte(static_cast<std::uint8_t>(gid & 0xFF)) << "> <"
                << utf16be_hex_from_codepoint(cp) << ">\n";
        }
        cmap << "endbfchar\n";
    }

    cmap << "endcmap\n"
        << "CMapName currentdict /CMap defineresource pop\n"
        << "end\n"
        << "end";
    return cmap.str();
}

void Pdf_writer::stroke_rect(
    double x_pt, double y_top_pt, double w_pt, double h_pt,
    const color_t& color, double line_width_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, color, true);
    out += number_to_string(line_width_pt) + " w\n";
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nS\n";
    out += "Q\n";
}

void Pdf_writer::stroke_line(
    double x1_pt,
    double y1_top_pt,
    double x2_pt,
    double y2_top_pt,
    const color_t& color,
    double line_width_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, color, true);
    out += number_to_string(line_width_pt) + " w\n";
    out += number_to_string(x1_pt) + " " + number_to_string(m_page_height_pt - y1_top_pt)
        + " m\n";
    out += number_to_string(x2_pt) + " " + number_to_string(m_page_height_pt - y2_top_pt)
        + " l\nS\n";
    out += "Q\n";
}

void Pdf_writer::fill_rect(
    double x_pt, double y_top_pt, double w_pt, double h_pt,
    const color_t& color)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, color, false);
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nf\n";
    out += "Q\n";
}

bool Pdf_writer::save(const fs::path& path) const
{
    if (!m_fonts_loaded || !m_metrics) {
        return false;
    }

    Pdf_object_table table;

    const int catalog_id = table.reserve();
    const int pages_id = table.reserve();

    const auto used_font_ids = used_fonts(m_fonts);

    struct font_object_ids_t
    {
        int type0 = 0;
        int file = 0;
        int descriptor = 0;
        int cid = 0;
        int to_unicode = 0;
    };
    std::vector<font_object_ids_t> font_objects(used_font_ids.size());
    for (auto& f : font_objects) {
        f.type0      = table.reserve();
        f.file       = table.reserve();
        f.descriptor = table.reserve();
        f.cid        = table.reserve();
        f.to_unicode = table.reserve();
    }

    struct image_object_ids_t
    {
        int image = 0;
        int mask = 0;
    };
    std::vector<image_object_ids_t> image_objects(m_images.size());
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        image_objects[i].image = table.reserve();
        if (m_images[i].image.has_alpha()) {
            image_objects[i].mask = table.reserve();
        }
    }

    struct page_object_ids_t
    {
        int content = 0;
        int page = 0;
    };
    std::vector<page_object_ids_t> page_objects(m_pages.size());
    for (auto& p : page_objects) {
        p.content = table.reserve();
        p.page    = table.reserve();
    }

    table.emit(
        catalog_id,
        "<< /Type /Catalog /Pages " + std::to_string(pages_id) + " 0 R >>");

    {
        std::ostringstream pages_body;
        pages_body << "<< /Type /Pages /Kids [ ";
        for (const auto& p : page_objects) {
            pages_body << p.page << " 0 R ";
        }
        pages_body << "] /Count " << page_objects.size() << " >>";
        table.emit(pages_id, pages_body.str());
    }

    for (std::size_t font_index = 0; font_index < used_font_ids.size(); ++font_index) {
        const Pdf_font font_id = used_font_ids[font_index];
        const auto& slot = m_fonts[static_cast<std::size_t>(font_id)];
        const auto& face = m_metrics->font_face(font_id);
        const auto& ids = font_objects[font_index];

        table.emit(ids.file, build_stream_object(face.bytes()));

        const auto x_min = scale_1000(face.x_min(), face.units_per_em());
        const auto y_min = scale_1000(face.y_min(), face.units_per_em());
        const auto x_max = scale_1000(face.x_max(), face.units_per_em());
        const auto y_max = scale_1000(face.y_max(), face.units_per_em());

        std::uint16_t default_width = face.advance_width_for_gid(0);
        if (default_width == 0) {
            default_width = static_cast<std::uint16_t>(face.units_per_em());
        }
        const double default_width_1000 = scale_1000(
            static_cast<std::int16_t>(default_width),
            face.units_per_em());
        const double ascent = scale_1000(face.ascent(), face.units_per_em());
        const double descent = scale_1000(face.descent(), face.units_per_em());
        const double cap_height = face.cap_height() != 0
            ? scale_1000(face.cap_height(), face.units_per_em())
            : ascent;
        const double italic_angle = face.italic_angle();

        unsigned flags = 0x04;
        if (face.is_fixed_pitch()) {
            flags |= 0x01;
        }
        if (italic_angle != 0.0) {
            flags |= 0x40;
        }

        std::ostringstream descriptor;
        descriptor << "<< /Type /FontDescriptor /FontName /" << m_metrics->font_tag_name(font_id)
            << " /Flags " << flags
            << " /Ascent " << number_to_string(ascent)
            << " /Descent " << number_to_string(descent)
            << " /CapHeight " << number_to_string(cap_height)
            << " /ItalicAngle " << number_to_string(italic_angle)
            << " /StemV 80 /FontBBox ["
            << number_to_string(x_min) << ' ' << number_to_string(y_min) << ' '
            << number_to_string(x_max) << ' ' << number_to_string(y_max)
            << "] /FontFile2 " << ids.file << " 0 R >>";
        table.emit(ids.descriptor, descriptor.str());

        std::ostringstream widths;
        widths << "[ ";
        std::vector<std::uint16_t> used = sorted_used_glyphs(slot.used_glyphs);
        if (used.empty()) {
            used.push_back(0);
        }
        for (std::uint16_t gid : used) {
            const std::uint16_t aw = face.advance_width_for_gid(gid);
            const std::uint32_t w1000 = face.units_per_em() == 0
                ? aw
                : static_cast<std::uint32_t>((static_cast<std::uint64_t>(aw) * 1000 + face.units_per_em() / 2)
                                             / face.units_per_em());
            widths << gid << " [" << w1000 << "] ";
        }
        widths << "]";

        std::ostringstream cidfont;
        cidfont << "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" << m_metrics->font_tag_name(font_id)
            << " /FontDescriptor " << ids.descriptor << " 0 R /CIDSystemInfo << /Registry (Adobe)"
            << " /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW "
            << number_to_string(default_width_1000) << " /W " << widths.str() << " >>";
        table.emit(ids.cid, cidfont.str());

        table.emit(ids.to_unicode, build_stream_object(make_to_unicode_cmap(slot)));

        std::ostringstream type0;
        type0 << "<< /Type /Font /Subtype /Type0 /BaseFont /" << m_metrics->font_tag_name(font_id)
            << " /Encoding /Identity-H /DescendantFonts [ " << ids.cid
            << " 0 R ] /ToUnicode " << ids.to_unicode << " 0 R >>";
        table.emit(ids.type0, type0.str());
    }

    auto image_dict = [](int w, int h, const char* colorspace, int smask_id) {
        std::ostringstream out;
        out << " /Type /XObject /Subtype /Image /Width " << w
            << " /Height " << h
            << " /ColorSpace " << colorspace
            << " /BitsPerComponent 8";
        if (smask_id != 0) {
            out << " /SMask " << smask_id << " 0 R";
        }
        return out.str();
    };

    for (std::size_t i = 0; i < m_images.size(); ++i) {
        const auto& image = m_images[i].image;
        const auto& ids = image_objects[i];
        const char* cs = image.color_components() == 1 ? "/DeviceGray" : "/DeviceRGB";

        table.emit(
            ids.image,
            build_stream_object(
                image.pixels(),
                image_dict(image.width_px(), image.height_px(), cs,
                           image.has_alpha() ? ids.mask : 0)));

        if (image.has_alpha()) {
            table.emit(
                ids.mask,
                build_stream_object(
                    image.alpha(),
                    image_dict(image.width_px(), image.height_px(), "/DeviceGray", 0)));
        }
    }

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        const Page& page = m_pages[i];
        const auto& ids = page_objects[i];

        table.emit(ids.content, build_stream_object(page.content));

        std::ostringstream resources;
        resources << "<< /Font << ";
        for (std::size_t font_index = 0; font_index < used_font_ids.size(); ++font_index) {
            resources << font_resource_name(used_font_ids[font_index]) << ' '
                << font_objects[font_index].type0 << " 0 R ";
        }
        resources << ">>";

        if (!page.image_indices.empty()) {
            resources << " /XObject << ";
            for (std::size_t image_index : page.image_indices) {
                resources << m_images[image_index].resource_name << ' '
                    << image_objects[image_index].image << " 0 R ";
            }
            resources << ">>";
        }
        resources << " >>";

        std::ostringstream page_body;
        page_body << "<< /Type /Page /Parent " << pages_id << " 0 R /MediaBox [0 0 "
            << number_to_string(m_page_width_pt) << ' '
            << number_to_string(m_page_height_pt) << "] /Resources "
            << resources.str()
            << " /Contents " << ids.content << " 0 R >>";
        table.emit(ids.page, page_body.str());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    return table.write(out, catalog_id);
}

} // namespace mark2haru
