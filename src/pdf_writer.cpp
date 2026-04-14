#include "pdf_writer.h"

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

std::vector<std::uint32_t> decode_utf8(const std::string& text)
{
    std::vector<std::uint32_t> cps;
    cps.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = '?';
        size_t advance = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            advance = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12)
                | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            advance = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18)
                | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12)
                | ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6)
                | (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            advance = 4;
        }
        cps.push_back(cp);
        i += advance;
    }
    return cps;
}

std::string utf16be_hex_from_codepoint(std::uint32_t codepoint)
{
    std::string out;
    if (codepoint <= 0xFFFF) {
        const std::uint16_t u = static_cast<std::uint16_t>(codepoint);
        out += hex_byte(static_cast<std::uint8_t>((u >> 8) & 0xFF));
        out += hex_byte(static_cast<std::uint8_t>(u & 0xFF));
    } else {
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

// Build a deterministic 6-uppercase-letter subset prefix from the sorted set
// of used glyph IDs. We do not perform true glyph subsetting yet, but
// emitting a tag now means the PDF is structurally ready for it and tools
// that expect a subset prefix (PDF/A validators, some archival tooling) are
// happy. Identical glyph sets produce identical prefixes, which also keeps
// the output reproducible.
std::string subset_tag_from_glyphs(const std::unordered_set<std::uint16_t>& used)
{
    std::uint64_t h = 0xcbf29ce484222325ull; // FNV-1a seed
    std::vector<std::uint16_t> sorted(used.begin(), used.end());
    std::sort(sorted.begin(), sorted.end());
    for (std::uint16_t gid : sorted) {
        h ^= static_cast<std::uint64_t>(gid);
        h *= 0x100000001b3ull;
    }
    std::string tag(6, 'A');
    for (int i = 0; i < 6; ++i) {
        tag[i] = static_cast<char>('A' + (h % 26));
        h /= 26;
    }
    return tag;
}

} // namespace

PdfWriter::PdfWriter(double page_width_pt, double page_height_pt,
                     std::shared_ptr<const MeasurementContext> metrics)
    : page_width_pt_(page_width_pt)
    , page_height_pt_(page_height_pt)
    , metrics_(std::move(metrics))
{
    begin_page();
    fonts_loaded_ = metrics_ && metrics_->loaded();
    if (!fonts_loaded_) {
        font_error_ = metrics_ ? metrics_->error() : "No measurement context";
    }
}

PdfWriter::PdfWriter(double page_width_pt, double page_height_pt,
                     const std::filesystem::path& font_root)
    : PdfWriter(page_width_pt, page_height_pt,
                std::make_shared<MeasurementContext>(FontFamilyConfig::briefutil_default(), font_root))
{
}

bool PdfWriter::fonts_loaded() const
{
    return fonts_loaded_;
}

bool PdfWriter::page_empty() const
{
    // A page is considered empty when no drawing commands have been emitted
    // and no images have been staged. Either condition flipping would mean
    // the renderer is about to finalise real output for this page.
    return pages_.empty()
        || (pages_.back().content.empty() && pages_.back().image_indices.empty());
}

std::string& PdfWriter::current_content()
{
    return current_page().content;
}

PdfWriter::Page& PdfWriter::current_page()
{
    if (pages_.empty()) {
        begin_page();
    }
    return pages_.back();
}

void PdfWriter::begin_page()
{
    pages_.push_back(Page{});
}

void PdfWriter::set_stroke_color(const Color& c)
{
    stroke_ = c;
}

void PdfWriter::set_fill_color(const Color& c)
{
    fill_ = c;
}

void PdfWriter::set_line_width(double width_pt)
{
    line_width_pt_ = width_pt;
}

void PdfWriter::append_color(std::string& out, const Color& c, bool stroke) const
{
    out += number_to_string(c.r);
    out.push_back(' ');
    out += number_to_string(c.g);
    out.push_back(' ');
    out += number_to_string(c.b);
    out.push_back(' ');
    out += stroke ? "RG\n" : "rg\n";
}

std::string PdfWriter::font_resource_name(PdfFont font)
{
    switch (font) {
    case PdfFont::Regular: return "/F1";
    case PdfFont::Bold: return "/F2";
    case PdfFont::Italic: return "/F3";
    case PdfFont::BoldItalic: return "/F4";
    case PdfFont::Mono: return "/F5";
    }
    return "/F1";
}

std::vector<PdfFont> PdfWriter::used_fonts(const std::array<LoadedFont, 5>& fonts)
{
    std::vector<PdfFont> out;
    for (size_t i = 0; i < fonts.size(); ++i) {
        if (!fonts[i].used_glyphs.empty()) {
            out.push_back(static_cast<PdfFont>(i));
        }
    }
    return out;
}

std::string PdfWriter::encode_flate(const std::string& input)
{
    if (input.empty()) {
        return {};
    }
    uLongf dest_len = compressBound(static_cast<uLong>(input.size()));
    std::string out(dest_len, '\0');
    const int rc = compress2(reinterpret_cast<Bytef*>(&out[0]), &dest_len,
                             reinterpret_cast<const Bytef*>(input.data()),
                             static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
    if (rc != Z_OK) {
        return input;
    }
    out.resize(dest_len);
    return out;
}

std::string PdfWriter::encode_flate(const std::vector<std::uint8_t>& input)
{
    return encode_flate(std::string(reinterpret_cast<const char*>(input.data()), input.size()));
}

double PdfWriter::measure_text_width(PdfFont font, const std::string& text, double size_pt) const
{
    if (!metrics_) {
        return 0.0;
    }
    return metrics_->measure_text_width(font, text, size_pt);
}

std::string PdfWriter::utf8_to_hex_cid_string(const TrueTypeFont& font,
                                              LoadedFont& loaded,
                                              const std::string& text)
{
    std::vector<std::uint16_t> gids;
    gids.reserve(text.size());
    for (std::uint32_t cp : decode_utf8(text)) {
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

void PdfWriter::draw_text(double x_pt, double y_top_pt, double size_pt, PdfFont font,
                          const std::string& text)
{
    if (!metrics_) {
        return;
    }

    auto& slot = fonts_[static_cast<std::size_t>(font)];
    slot.used = true;
    const auto& face = metrics_->font_face(font);
    const std::string encoded = utf8_to_hex_cid_string(face, slot, text);
    auto& out = current_content();
    out += "BT\n";
    out += font_resource_name(font);
    out.push_back(' ');
    out += number_to_string(size_pt);
    out += " Tf\n";
    out += number_to_string(x_pt) + " " + number_to_string(page_height_pt_ - y_top_pt - size_pt)
        + " Td\n";
    out += encoded;
    out += " Tj\nET\n";
}

bool PdfWriter::draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt, const PngImage& image)
{
    if (!image.loaded()) {
        return false;
    }

    const std::size_t image_index = images_.size();
    LoadedImage stored;
    stored.image = image;
    stored.resource_name = "/Im" + std::to_string(image_index + 1);
    images_.push_back(std::move(stored));
    current_page().image_indices.push_back(image_index);

    auto& out = current_content();
    out += "q\n";
    out += number_to_string(w_pt) + " 0 0 " + number_to_string(h_pt) + " "
        + number_to_string(x_pt) + " " + number_to_string(page_height_pt_ - y_top_pt - h_pt)
        + " cm\n";
    out += images_.back().resource_name + " Do\nQ\n";
    return true;
}

bool PdfWriter::draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt,
                         const std::filesystem::path& path)
{
    PngImage image;
    if (!image.load_from_file(path)) {
        return false;
    }
    return draw_png(x_pt, y_top_pt, w_pt, h_pt, image);
}

std::string PdfWriter::make_to_unicode_cmap(const LoadedFont& font)
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
    std::sort(pairs.begin(), pairs.end(),
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

void PdfWriter::stroke_line(double x1_pt, double y1_top_pt, double x2_pt, double y2_top_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, stroke_, true);
    out += number_to_string(line_width_pt_) + " w\n";
    out += number_to_string(x1_pt) + " " + number_to_string(page_height_pt_ - y1_top_pt) + " m\n";
    out += number_to_string(x2_pt) + " " + number_to_string(page_height_pt_ - y2_top_pt) + " l\nS\n";
    out += "Q\n";
}

void PdfWriter::stroke_rect(double x_pt, double y_top_pt, double w_pt, double h_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, stroke_, true);
    out += number_to_string(line_width_pt_) + " w\n";
    out += number_to_string(x_pt) + " " + number_to_string(page_height_pt_ - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nS\n";
    out += "Q\n";
}

void PdfWriter::fill_rect(double x_pt, double y_top_pt, double w_pt, double h_pt)
{
    auto& out = current_content();
    out += "q\n";
    append_color(out, fill_, false);
    out += number_to_string(x_pt) + " " + number_to_string(page_height_pt_ - y_top_pt - h_pt)
        + " " + number_to_string(w_pt) + " " + number_to_string(h_pt) + " re\nf\n";
    out += "Q\n";
}

bool PdfWriter::save(const std::filesystem::path& path) const
{
    if (!fonts_loaded_ || !metrics_) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const auto used_font_ids = used_fonts(fonts_);
    const int font_count = static_cast<int>(used_font_ids.size());
    const int page_count = static_cast<int>(pages_.size());
    const int catalog_obj = 1;
    const int pages_obj = 2;
    const int font_base_obj = 3;
    const int font_objects_per_face = 5;

    struct ImageObjectNumbers {
        int image_obj = 0;
        int mask_obj = 0;
    };
    std::vector<ImageObjectNumbers> image_objects(images_.size());
    int next_obj = font_base_obj + font_count * font_objects_per_face;
    for (std::size_t i = 0; i < images_.size(); ++i) {
        image_objects[i].image_obj = next_obj++;
        if (images_[i].image.has_alpha()) {
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
        const PdfFont font_id = used_font_ids[static_cast<std::size_t>(used_index)];
        const auto& slot = fonts_[static_cast<std::size_t>(font_id)];
        const auto& face = metrics_->font_face(font_id);
        const int type0_obj = font_base_obj + used_index * font_objects_per_face;
        const int file_obj = type0_obj + 1;
        const int desc_obj = type0_obj + 2;
        const int cid_obj = type0_obj + 3;
        const int unicode_obj = type0_obj + 4;

        // Compose a PDF BaseFont by prefixing a deterministic six-letter
        // subset tag onto whatever canonical tag the MeasurementContext
        // supplied. We don't ship a real subsetter yet (see the README's
        // follow-up list), but emitting the tag is cheap, keeps output
        // reproducible, and makes the PDF structurally ready for subsetting.
        const std::string base_font_name = subset_tag_from_glyphs(slot.used_glyphs)
            + "+" + metrics_->font_tag_name(font_id);

        const auto& bytes = face.bytes();
        const std::string font_compressed = encode_flate(bytes);
        const bool font_is_compressed = !font_compressed.empty() && font_compressed.size() < bytes.size();
        const std::string font_bytes = font_is_compressed
            ? font_compressed
            : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::ostringstream font_stream;
        font_stream << "<< /Length " << font_bytes.size();
        if (font_is_compressed) {
            font_stream << " /Filter /FlateDecode";
        }
        font_stream << " >>\nstream\n";
        font_stream.write(font_bytes.data(), static_cast<std::streamsize>(font_bytes.size()));
        font_stream << "\nendstream";
        objects[file_obj] = font_stream.str();

        const auto x_min = scale_1000(face.x_min(), face.units_per_em());
        const auto y_min = scale_1000(face.y_min(), face.units_per_em());
        const auto x_max = scale_1000(face.x_max(), face.units_per_em());
        const auto y_max = scale_1000(face.y_max(), face.units_per_em());

        std::uint16_t default_width = face.advance_width_for_gid(0);
        if (default_width == 0) {
            default_width = static_cast<std::uint16_t>(face.units_per_em());
        }
        const double default_width_1000 = scale_1000(static_cast<std::int16_t>(default_width),
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
        desc << "<< /Type /FontDescriptor /FontName /" << base_font_name
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
        cidfont << "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" << base_font_name
                << " /FontDescriptor " << desc_obj << " 0 R /CIDSystemInfo << /Registry (Adobe)"
                << " /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW "
                << number_to_string(default_width_1000) << " /W " << widths.str() << " >>";
        objects[cid_obj] = cidfont.str();

        const std::string cmap = make_to_unicode_cmap(slot);
        const std::string cmap_compressed = encode_flate(cmap);
        const bool cmap_is_compressed = !cmap_compressed.empty() && cmap_compressed.size() < cmap.size();
        const std::string cmap_bytes = cmap_is_compressed ? cmap_compressed : cmap;
        std::ostringstream cmap_stream;
        cmap_stream << "<< /Length " << cmap_bytes.size();
        if (cmap_is_compressed) {
            cmap_stream << " /Filter /FlateDecode";
        }
        cmap_stream << " >>\nstream\n";
        cmap_stream.write(cmap_bytes.data(), static_cast<std::streamsize>(cmap_bytes.size()));
        cmap_stream << "\nendstream";
        objects[unicode_obj] = cmap_stream.str();

        std::ostringstream type0_font;
        type0_font << "<< /Type /Font /Subtype /Type0 /BaseFont /" << base_font_name
                   << " /Encoding /Identity-H /DescendantFonts [ " << cid_obj
                   << " 0 R ] /ToUnicode " << unicode_obj << " 0 R >>";
        objects[type0_obj] = type0_font.str();
    }

    for (std::size_t i = 0; i < images_.size(); ++i) {
        const auto& image = images_[i].image;
        const auto& numbers = image_objects[i];
        const std::vector<std::uint8_t>& data = image.pixels();
        const std::string compressed = encode_flate(data);
        const bool use_compressed = !compressed.empty() && compressed.size() < data.size();
        const std::string payload = use_compressed
            ? compressed
            : std::string(reinterpret_cast<const char*>(data.data()), data.size());

        std::ostringstream image_stream;
        image_stream << "<< /Type /XObject /Subtype /Image /Width " << image.width_px()
                     << " /Height " << image.height_px()
                     << " /ColorSpace " << (image.color_components() == 1 ? "/DeviceGray" : "/DeviceRGB")
                     << " /BitsPerComponent 8";
        if (image.has_alpha()) {
            image_stream << " /SMask " << numbers.mask_obj << " 0 R";
        }
        image_stream << " /Length " << payload.size();
        if (use_compressed) {
            image_stream << " /Filter /FlateDecode";
        }
        image_stream << " >>\nstream\n";
        image_stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        image_stream << "\nendstream";
        objects[numbers.image_obj] = image_stream.str();

        if (image.has_alpha()) {
            const std::vector<std::uint8_t>& alpha = image.alpha();
            const std::string alpha_compressed = encode_flate(alpha);
            const bool alpha_use_compressed = !alpha_compressed.empty() && alpha_compressed.size() < alpha.size();
            const std::string alpha_payload = alpha_use_compressed
                ? alpha_compressed
                : std::string(reinterpret_cast<const char*>(alpha.data()), alpha.size());
            std::ostringstream alpha_stream;
            alpha_stream << "<< /Type /XObject /Subtype /Image /Width " << image.width_px()
                         << " /Height " << image.height_px()
                         << " /ColorSpace /DeviceGray /BitsPerComponent 8 /Length " << alpha_payload.size();
            if (alpha_use_compressed) {
                alpha_stream << " /Filter /FlateDecode";
            }
            alpha_stream << " >>\nstream\n";
            alpha_stream.write(alpha_payload.data(), static_cast<std::streamsize>(alpha_payload.size()));
            alpha_stream << "\nendstream";
            objects[numbers.mask_obj] = alpha_stream.str();
        }
    }

    for (int i = 0; i < page_count; ++i) {
        const int content_obj = content_base_obj + i;
        const int page_obj = page_base_obj + i;
        const Page& page = pages_[static_cast<std::size_t>(i)];

        const std::string content_compressed = encode_flate(page.content);
        const bool content_is_compressed = !content_compressed.empty() && content_compressed.size() < page.content.size();
        const std::string content_bytes = content_is_compressed ? content_compressed : page.content;
        std::ostringstream content;
        content << "<< /Length " << content_bytes.size();
        if (content_is_compressed) {
            content << " /Filter /FlateDecode";
        }
        content << " >>\nstream\n";
        content.write(content_bytes.data(), static_cast<std::streamsize>(content_bytes.size()));
        content << "\nendstream";
        objects[content_obj] = content.str();

        std::ostringstream resources;
        resources << "<< /Font << ";
        for (int used_index = 0; used_index < font_count; ++used_index) {
            const PdfFont font_id = used_font_ids[static_cast<std::size_t>(used_index)];
            resources << font_resource_name(font_id) << ' '
                      << (font_base_obj + used_index * font_objects_per_face) << " 0 R ";
        }
        resources << ">>";

        if (!page.image_indices.empty()) {
            resources << " /XObject << ";
            for (std::size_t image_index : page.image_indices) {
                resources << images_[image_index].resource_name << ' '
                          << image_objects[image_index].image_obj << " 0 R ";
            }
            resources << ">>";
        }
        resources << " >>";

        std::ostringstream page_obj_body;
        page_obj_body << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                      << number_to_string(page_width_pt_) << ' '
                      << number_to_string(page_height_pt_) << "] /Resources "
                      << resources.str()
                      << " /Contents " << content_obj << " 0 R >>";
        objects[page_obj] = page_obj_body.str();
    }

    // Build a stable /ID from a hash of the object bodies. Two identical
    // inputs produce identical /ID, and any content change flips it. This
    // is purely informational — the spec makes /ID optional — but some
    // archival tooling and PDF/A validators require it.
    auto build_id = [&]() {
        std::uint64_t h = 0xcbf29ce484222325ull;
        auto mix = [&](const std::string& s) {
            for (unsigned char c : s) {
                h ^= c;
                h *= 0x100000001b3ull;
            }
        };
        for (const auto& obj : objects) {
            mix(obj);
        }
        std::string hex_out;
        hex_out.reserve(32);
        for (int byte = 7; byte >= 0; --byte) {
            const std::uint8_t b = static_cast<std::uint8_t>((h >> (byte * 8)) & 0xFF);
            hex_out += hex_byte(b);
        }
        // Duplicate the 8-byte digest to produce a conventional 16-byte ID.
        hex_out += hex_out;
        return hex_out;
    };
    const std::string id_hex = build_id();

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
        << " /Root 1 0 R"
        << " /ID [<" << id_hex << "> <" << id_hex << ">]"
        << " >>\nstartxref\n" << xref_pos << "\n%%EOF\n";
    return static_cast<bool>(out);
}

} // namespace mark2haru
