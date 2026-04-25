#include <mark2haru/pdf_writer.h>

#include "utf8_decode.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

#include "miniz.h"

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

template <class T>
std::string number_to_string(T value)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss << std::setprecision(3) << value;
    std::string out = ss.str();
    if (out.find('.') != std::string::npos) {
        const size_t last = out.find_last_not_of('0');
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

std::string hex_byte(std::uint8_t byte)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(digits[(byte >> 4) & 0xF]);
    out.push_back(digits[byte & 0xF]);
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

Pdf_writer::page_t& Pdf_writer::current_page()
{
    if (m_pages.empty()) {
        begin_page();
    }
    return m_pages.back();
}

void Pdf_writer::begin_page()
{
    m_pages.push_back(page_t{});
}

void Pdf_writer::set_stroke_color(const color_t& color)
{
    m_stroke = color;
}

void Pdf_writer::set_fill_color(const color_t& color)
{
    m_fill = color;
}

void Pdf_writer::set_line_width(double width_pt)
{
    m_line_width_pt = width_pt;
}

void Pdf_writer::append_color(std::string& out, const color_t& color, bool stroke) const
{
    out += number_to_string(color.r);
    out.push_back(' ');
    out += number_to_string(color.g);
    out.push_back(' ');
    out += number_to_string(color.b);
    out.push_back(' ');
    out += stroke ? "RG\n" : "rg\n";
}

std::string Pdf_writer::font_resource_name(Pdf_font font)
{
    return std::string("/F") + static_cast<char>('1' + static_cast<int>(font));
}

std::vector<Pdf_font> Pdf_writer::used_fonts(const std::array<loaded_font_t, 5>& fonts)
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

} // namespace

std::string Pdf_writer::utf8_to_hex_cid_string(
    const True_type_font& font,
    loaded_font_t& loaded,
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
    if (!m_metrics) {
        return;
    }

    auto& slot = m_fonts[static_cast<std::size_t>(font)];
    slot.used = true;
    const auto& face = m_metrics->font_face(font);
    const std::string encoded = utf8_to_hex_cid_string(face, slot, text);
    auto& out = current_content();
    out += "BT\n";
    out += font_resource_name(font);
    out.push_back(' ');
    out += number_to_string(size_pt);
    out += " Tf\n";
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - size_pt)
        + " Td\n";
    out += encoded;
    out += " Tj\nET\n";
}

bool Pdf_writer::draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt, const Png_image& image)
{
    if (!image.loaded()) {
        return false;
    }

    const std::size_t image_index = m_images.size();
    loaded_image_t stored;
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

std::string Pdf_writer::make_to_unicode_cmap(const loaded_font_t& font)
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

void Pdf_writer::stroke_rect(double x_pt, double y_top_pt, double w_pt, double h_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, m_stroke, true);
    out += number_to_string(m_line_width_pt) + " w\n";
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nS\n";
    out += "Q\n";
}

void Pdf_writer::fill_rect(double x_pt, double y_top_pt, double w_pt, double h_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, m_fill, false);
    out += number_to_string(x_pt) + " " + number_to_string(m_page_height_pt - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nf\n";
    out += "Q\n";
}

bool Pdf_writer::save(const fs::path& path) const
{
    if (!m_fonts_loaded || !m_metrics) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const auto used_font_ids = used_fonts(m_fonts);
    const int font_count = static_cast<int>(used_font_ids.size());
    const int page_count = static_cast<int>(m_pages.size());
    const int catalog_obj = 1;
    const int pages_obj = 2;
    const int font_base_obj = 3;
    const int font_objects_per_face = 5;

    struct image_object_numbers_t {
        int image_obj = 0;
        int mask_obj = 0;
    };
    std::vector<image_object_numbers_t> image_objects(m_images.size());
    int next_obj = font_base_obj + font_count * font_objects_per_face;
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        image_objects[i].image_obj = next_obj++;
        if (m_images[i].image.has_alpha()) {
            image_objects[i].mask_obj = next_obj++;
        }
    }
    const int content_base_obj = next_obj;
    const int page_base_obj = content_base_obj + page_count;
    const int total_objects = page_base_obj + page_count - 1;

    std::vector<std::string> objects(static_cast<std::size_t>(total_objects + 1));
    objects[catalog_obj] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::ostringstream kids;
    kids << "[ ";
    for (int i = 0; i < page_count; ++i) {
        kids << (page_base_obj + i) << " 0 R ";
    }
    kids << "]";
    objects[pages_obj] = "<< /Type /Pages /Kids " + kids.str() + " /Count "
        + std::to_string(page_count) + " >>";

    for (int used_index = 0; used_index < font_count; ++used_index) {
        const Pdf_font font_id = used_font_ids[static_cast<std::size_t>(used_index)];
        const auto& slot = m_fonts[static_cast<std::size_t>(font_id)];
        const auto& face = m_metrics->font_face(font_id);
        const int type0_obj = font_base_obj + used_index * font_objects_per_face;
        const int file_obj = type0_obj + 1;
        const int desc_obj = type0_obj + 2;
        const int cid_obj = type0_obj + 3;
        const int unicode_obj = type0_obj + 4;

        objects[file_obj] = build_stream_object(face.bytes());

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

        std::ostringstream desc;
        desc << "<< /Type /FontDescriptor /FontName /" << m_metrics->font_tag_name(font_id)
            << " /Flags " << flags
            << " /Ascent " << number_to_string(ascent)
            << " /Descent " << number_to_string(descent)
            << " /CapHeight " << number_to_string(cap_height)
            << " /ItalicAngle " << number_to_string(italic_angle)
            << " /StemV 80 /FontBBox ["
            << number_to_string(x_min) << ' ' << number_to_string(y_min) << ' '
            << number_to_string(x_max) << ' ' << number_to_string(y_max)
            << "] /FontFile2 " << file_obj << " 0 R >>";
        objects[desc_obj] = desc.str();

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
            << " /FontDescriptor " << desc_obj << " 0 R /CIDSystemInfo << /Registry (Adobe)"
            << " /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW "
            << number_to_string(default_width_1000) << " /W " << widths.str() << " >>";
        objects[cid_obj] = cidfont.str();

        objects[unicode_obj] = build_stream_object(make_to_unicode_cmap(slot));

        std::ostringstream type0_font;
        type0_font << "<< /Type /Font /Subtype /Type0 /BaseFont /" << m_metrics->font_tag_name(font_id)
            << " /Encoding /Identity-H /DescendantFonts [ " << cid_obj
            << " 0 R ] /ToUnicode " << unicode_obj << " 0 R >>";
        objects[type0_obj] = type0_font.str();
    }

    for (std::size_t i = 0; i < m_images.size(); ++i) {
        const auto& image = m_images[i].image;
        const auto& numbers = image_objects[i];

        std::ostringstream image_dict;
        image_dict
            << " /Type /XObject /Subtype /Image /Width " << image.width_px()
            << " /Height " << image.height_px() << " /ColorSpace "
            << (image.color_components() == 1 ? "/DeviceGray" : "/DeviceRGB")
            << " /BitsPerComponent 8";
        if (image.has_alpha()) {
            image_dict << " /SMask " << numbers.mask_obj << " 0 R";
        }
        objects[numbers.image_obj] = build_stream_object(image.pixels(), image_dict.str());

        if (image.has_alpha()) {
            std::ostringstream alpha_dict;
            alpha_dict
                << " /Type /XObject /Subtype /Image /Width " << image.width_px()
                << " /Height " << image.height_px()
                << " /ColorSpace /DeviceGray /BitsPerComponent 8";
            objects[numbers.mask_obj] = build_stream_object(image.alpha(), alpha_dict.str());
        }
    }

    for (int i = 0; i < page_count; ++i) {
        const int content_obj = content_base_obj + i;
        const int page_obj = page_base_obj + i;
        const page_t& page = m_pages[static_cast<std::size_t>(i)];

        objects[content_obj] = build_stream_object(page.content);

        std::ostringstream resources;
        resources << "<< /Font << ";
        for (int used_index = 0; used_index < font_count; ++used_index) {
            const Pdf_font font_id = used_font_ids[static_cast<std::size_t>(used_index)];
            resources << font_resource_name(font_id) << ' '
                << (font_base_obj + used_index * font_objects_per_face) << " 0 R ";
        }
        resources << ">>";

        if (!page.image_indices.empty()) {
            resources << " /XObject << ";
            for (std::size_t image_index : page.image_indices) {
                resources << m_images[image_index].resource_name << ' '
                    << image_objects[image_index].image_obj << " 0 R ";
            }
            resources << ">>";
        }
        resources << " >>";

        std::ostringstream page_obj_body;
        page_obj_body << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
            << number_to_string(m_page_width_pt) << ' '
            << number_to_string(m_page_height_pt) << "] /Resources "
            << resources.str()
            << " /Contents " << content_obj << " 0 R >>";
        objects[page_obj] = page_obj_body.str();
    }

    const std::string header = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    std::vector<std::streamoff> offsets(static_cast<std::size_t>(total_objects + 1));
    for (int i = 1; i <= total_objects; ++i) {
        offsets[i] = out.tellp();
        out << i << " 0 obj\n" << objects[i] << "\nendobj\n";
    }

    const std::streamoff xref_pos = out.tellp();
    out << "xref\n0 " << (total_objects + 1) << "\n";
    out << "0000000000 65535 f \n";
    for (int i = 1; i <= total_objects; ++i) {
        out << std::setw(10) << std::setfill('0') << offsets[i]
            << " 00000 n \n";
    }
    out << "trailer\n<< /Size " << (total_objects + 1)
        << " /Root 1 0 R >>\nstartxref\n" << xref_pos << "\n%%EOF\n";
    return static_cast<bool>(out);
}

} // namespace mark2haru
