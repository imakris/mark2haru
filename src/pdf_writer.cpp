#include "pdf_writer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

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

std::vector<std::uint32_t> decode_utf8(const std::string& text)
{
    std::vector<std::uint32_t> cps;
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

// PDF names are a permissive set of characters, but most fonts only need the
// alphanumeric subset and this keeps the escaping rules simple. The prefix
// guard prevents leading digits, which are legal in a PDF name but confuse
// some viewers when the font is referenced by base name.
std::string sanitize_pdf_name(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out.push_back(static_cast<char>(ch));
        }
    }
    if (out.empty()) {
        return "EmbeddedFont";
    }
    if (out.front() >= '0' && out.front() <= '9') {
        out.insert(out.begin(), 'F');
    }
    return out;
}

// File-scope Flate helper. Used both by PdfWriter::save's helpers below and by
// the stream builders. Returns an empty string for empty input so callers can
// cheaply decide whether Flate actually saved any bytes.
std::string flate_compress(const std::string& input)
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

} // namespace

PdfWriter::PdfWriter(double page_width_pt, double page_height_pt,
                     const std::filesystem::path& font_root)
    : page_width_pt_(page_width_pt)
    , page_height_pt_(page_height_pt)
    , font_root_(font_root)
{
    // No I/O in the constructor: callers must call init() and then check
    // fonts_loaded() / font_error(). begin_page() is deferred to the first
    // draw via current_content().
}

bool PdfWriter::init()
{
    fonts_loaded_ = load_fonts();
    return fonts_loaded_;
}

bool PdfWriter::page_empty() const
{
    return pages_.empty() || pages_.back().content.empty();
}

PdfWriter::LoadedFont& PdfWriter::font_slot(PdfFont font)
{
    return fonts_[static_cast<size_t>(font)];
}

const PdfWriter::LoadedFont& PdfWriter::font_slot(PdfFont font) const
{
    return fonts_[static_cast<size_t>(font)];
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

std::string PdfWriter::font_tag_name(PdfFont font)
{
    switch (font) {
    case PdfFont::Regular: return "Regular";
    case PdfFont::Bold: return "Bold";
    case PdfFont::Italic: return "Italic";
    case PdfFont::BoldItalic: return "BoldItalic";
    case PdfFont::Mono: return "Mono";
    }
    return "Regular";
}

std::filesystem::path PdfWriter::resolve_font_path(
    const std::filesystem::path& root,
    const std::vector<std::string>& candidates)
{
    auto try_dir = [&](const std::filesystem::path& dir) -> std::filesystem::path {
        for (const auto& candidate : candidates) {
            const auto p = dir / candidate;
            if (std::filesystem::exists(p)) {
                return p;
            }
        }
        return {};
    };

    if (!root.empty()) {
        if (std::filesystem::is_regular_file(root)) {
            return root;
        }
        if (auto p = try_dir(root); !p.empty()) {
            return p;
        }
        if (auto p = try_dir(root / "fonts"); !p.empty()) {
            return p;
        }
    }

    if (auto p = try_dir(std::filesystem::current_path()); !p.empty()) {
        return p;
    }
    if (auto p = try_dir(std::filesystem::current_path() / "fonts"); !p.empty()) {
        return p;
    }

    const std::vector<std::filesystem::path> system_roots = {
        // Windows
        R"(C:\Windows\Fonts)",
        R"(C:\Windows\Fonts\truetype)",
        // Linux
        "/usr/share/fonts",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/dejavu",
        "/usr/local/share/fonts",
        // macOS
        "/Library/Fonts",
        "/System/Library/Fonts",
        "/System/Library/Fonts/Supplemental",
    };
    for (const auto& dir : system_roots) {
        if (auto p = try_dir(dir); !p.empty()) {
            return p;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path home_path(home);
        const std::vector<std::filesystem::path> user_roots = {
            home_path / ".fonts",
            home_path / ".local/share/fonts",
            home_path / "Library/Fonts",
        };
        for (const auto& dir : user_roots) {
            if (auto p = try_dir(dir); !p.empty()) {
                return p;
            }
        }
    }
    return {};
}

bool PdfWriter::load_fonts()
{
    const std::array<std::vector<std::string>, 5> candidate_sets = {{
        { "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "arial.ttf", "Arial.ttf" },
        { "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf", "arialbd.ttf", "Arial Bold.ttf" },
        { "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "LiberationSans-Italic.ttf", "ariali.ttf", "Arial Italic.ttf" },
        { "DejaVuSans-BoldOblique.ttf", "NotoSans-BoldItalic.ttf", "LiberationSans-BoldItalic.ttf", "arialbi.ttf", "Arial Bold Italic.ttf" },
        { "DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf", "cour.ttf", "Courier New.ttf" },
    }};

    for (size_t i = 0; i < fonts_.size(); ++i) {
        auto& slot = fonts_[i];
        const auto path = resolve_font_path(font_root_, candidate_sets[i]);
        if (path.empty()) {
            std::ostringstream err;
            err << "Unable to locate a font file for " << font_tag_name(static_cast<PdfFont>(i))
                << ". Tried: ";
            for (size_t c = 0; c < candidate_sets[i].size(); ++c) {
                if (c != 0) {
                    err << ", ";
                }
                err << candidate_sets[i][c];
            }
            if (!font_root_.empty()) {
                err << " (search root: " << font_root_.string() << ")";
            }
            font_error_ = err.str();
            return false;
        }
        if (!slot.face.load_from_file(path)) {
            font_error_ = "Failed to parse font file: " + path.string();
            return false;
        }
        slot.resource_name = "/F" + std::to_string(i + 1);
        slot.tag_name = sanitize_pdf_name(path.stem().string());
    }

    return true;
}

std::string& PdfWriter::current_content()
{
    if (pages_.empty()) {
        begin_page();
    }
    return pages_.back().content;
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

double PdfWriter::measure_text_width(PdfFont font, const std::string& text, double size_pt) const
{
    const auto& slot = font_slot(font);
    double width_units = 0.0;
    for (std::uint32_t cp : decode_utf8(text)) {
        std::uint16_t gid = slot.face.glyph_for_codepoint(cp);
        if (gid == 0) {
            gid = slot.face.glyph_for_codepoint('?');
        }
        width_units += slot.face.advance_width_for_gid(gid);
    }
    if (slot.face.units_per_em() == 0) {
        return 0.0;
    }
    return width_units * size_pt / static_cast<double>(slot.face.units_per_em());
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
    auto& slot = font_slot(font);
    slot.used = true;
    const std::string encoded = utf8_to_hex_cid_string(slot.face, slot, text);
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

// Build a deterministic 6-uppercase-letter subset prefix from the sorted set of
// used glyph IDs. We do not perform true glyph subsetting yet, but emitting a
// tag now means the PDF is structurally ready for it and tools that require a
// subset prefix (PDF/A validators, some archival tooling) are happy.
static std::string subset_tag_from_glyphs(const std::unordered_set<std::uint16_t>& used)
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

// Emit a stream object dictionary with optional Flate compression. The Flate
// branch is only taken when the compressed payload is strictly smaller than
// the raw input.
static std::string build_stream_object(const std::string& raw)
{
    const std::string compressed = flate_compress(raw);
    const bool use_flate = !compressed.empty() && compressed.size() < raw.size();
    const std::string& payload = use_flate ? compressed : raw;
    std::ostringstream ss;
    ss << "<< /Length " << payload.size();
    if (use_flate) {
        ss << " /Filter /FlateDecode";
    }
    ss << " >>\nstream\n";
    ss.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    ss << "\nendstream";
    return ss.str();
}

static std::string build_stream_object(const std::vector<std::uint8_t>& raw)
{
    return build_stream_object(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
}

bool PdfWriter::save(const std::string& path, std::string* error_out) const
{
    auto fail = [&](const std::string& msg) {
        if (error_out) {
            *error_out = msg;
        }
        return false;
    };

    if (!fonts_loaded_) {
        return fail(font_error_.empty() ? std::string("fonts not loaded") : font_error_);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return fail("cannot open output file: " + path);
    }

    // Push-based object table: object number 0 is the special free entry, so
    // we seed `objects[0]` with an empty string and append real objects
    // starting at index 1. Every helper below returns the assigned object
    // number so cross-references stay consistent even if the structure
    // changes in the future.
    std::vector<std::string> objects;
    objects.emplace_back(); // slot 0 unused
    auto add_object = [&](std::string body) {
        objects.push_back(std::move(body));
        return static_cast<int>(objects.size() - 1);
    };

    // Reserve the catalog and pages dict numbers so their cross-references are
    // known before page objects are emitted.
    const int catalog_obj = add_object({});
    const int pages_obj = add_object({});

    const auto used_fonts = PdfWriter::used_fonts(fonts_);
    struct FontObjectRefs {
        PdfFont font_id = PdfFont::Regular;
        int type0_obj = 0;
    };
    std::vector<FontObjectRefs> font_refs;
    font_refs.reserve(used_fonts.size());

    for (const PdfFont font_id : used_fonts) {
        const auto& slot = fonts_[static_cast<std::size_t>(font_id)];
        const std::string base_name = subset_tag_from_glyphs(slot.used_glyphs) + "+" + slot.tag_name;

        // Emit stream objects first so they exist before the dicts that
        // reference them.
        const int file_obj = add_object(build_stream_object(slot.face.bytes()));

        const auto x_min = scale_1000(slot.face.x_min(), slot.face.units_per_em());
        const auto y_min = scale_1000(slot.face.y_min(), slot.face.units_per_em());
        const auto x_max = scale_1000(slot.face.x_max(), slot.face.units_per_em());
        const auto y_max = scale_1000(slot.face.y_max(), slot.face.units_per_em());

        std::uint16_t default_width = slot.face.advance_width_for_gid(0);
        if (default_width == 0) {
            default_width = static_cast<std::uint16_t>(slot.face.units_per_em());
        }
        const double default_width_1000 = scale_1000(static_cast<std::int16_t>(default_width),
                                                     slot.face.units_per_em());
        const double ascent = scale_1000(slot.face.ascent(), slot.face.units_per_em());
        const double descent = scale_1000(slot.face.descent(), slot.face.units_per_em());
        const double cap_height = slot.face.cap_height() != 0
            ? scale_1000(slot.face.cap_height(), slot.face.units_per_em())
            : ascent;
        const double italic_angle = slot.face.italic_angle();

        // FontDescriptor /Flags bits (PDF 1.7, 9.8.2):
        //   bit  1 (0x01) = fixed pitch
        //   bit  3 (0x04) = symbolic (contains glyphs outside Adobe standard Latin)
        //   bit  6 (0x20) = nonsymbolic (Adobe standard Latin only)
        //   bit  7 (0x40) = italic
        // We embed Identity-H CID fonts with large glyph sets, so symbolic
        // is the right bucket. Mark italic and fixed-pitch when appropriate.
        unsigned flags = 0x04; // symbolic
        if (slot.face.is_fixed_pitch()) {
            flags |= 0x01;
        }
        if (italic_angle != 0.0) {
            flags |= 0x40;
        }

        std::ostringstream desc;
        desc << "<< /Type /FontDescriptor /FontName /" << base_name
             << " /Flags " << flags
             << " /Ascent " << number_to_string(ascent)
             << " /Descent " << number_to_string(descent)
             << " /CapHeight " << number_to_string(cap_height)
             << " /ItalicAngle " << number_to_string(italic_angle)
             << " /StemV 80 /FontBBox ["
             << number_to_string(x_min) << ' ' << number_to_string(y_min) << ' '
             << number_to_string(x_max) << ' ' << number_to_string(y_max)
             << "] /FontFile2 " << file_obj << " 0 R >>";
        const int desc_obj = add_object(desc.str());

        std::ostringstream widths;
        widths << "[ ";
        std::vector<std::uint16_t> used = sorted_used_glyphs(slot.used_glyphs);
        if (used.empty()) {
            used.push_back(0);
        }
        for (std::uint16_t gid : used) {
            const std::uint16_t aw = slot.face.advance_width_for_gid(gid);
            const std::uint32_t w1000 = slot.face.units_per_em() == 0
                ? aw
                : static_cast<std::uint32_t>((static_cast<std::uint64_t>(aw) * 1000 + slot.face.units_per_em() / 2)
                                             / slot.face.units_per_em());
            widths << gid << " [" << w1000 << "] ";
        }
        widths << "]";

        std::ostringstream cidfont;
        cidfont << "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" << base_name
                << " /FontDescriptor " << desc_obj << " 0 R /CIDSystemInfo << /Registry (Adobe)"
                << " /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW "
                << number_to_string(default_width_1000) << " /W " << widths.str() << " >>";
        const int cid_obj = add_object(cidfont.str());

        const int unicode_obj = add_object(build_stream_object(make_to_unicode_cmap(slot)));

        std::ostringstream type0_font;
        type0_font << "<< /Type /Font /Subtype /Type0 /BaseFont /" << base_name
                   << " /Encoding /Identity-H /DescendantFonts [ " << cid_obj
                   << " 0 R ] /ToUnicode " << unicode_obj << " 0 R >>";
        const int type0_obj = add_object(type0_font.str());

        font_refs.push_back({ font_id, type0_obj });
    }

    // Build the shared resources dict once — pages share it by value since
    // every page can reference every used font.
    std::ostringstream resources;
    resources << "<< /Font << ";
    for (const auto& ref : font_refs) {
        resources << font_resource_name(ref.font_id) << ' ' << ref.type0_obj << " 0 R ";
    }
    resources << ">> >>";
    const std::string resources_body = resources.str();

    // Emit each page: content stream first, then page object referencing it.
    std::vector<int> page_obj_numbers;
    page_obj_numbers.reserve(pages_.size());
    for (const Page& page : pages_) {
        const int content_obj = add_object(build_stream_object(page.content));
        std::ostringstream page_obj_body;
        page_obj_body << "<< /Type /Page /Parent " << pages_obj
                      << " 0 R /MediaBox [0 0 "
                      << number_to_string(page_width_pt_) << ' '
                      << number_to_string(page_height_pt_) << "] /Resources "
                      << resources_body
                      << " /Contents " << content_obj << " 0 R >>";
        page_obj_numbers.push_back(add_object(page_obj_body.str()));
    }

    // Now backfill the reserved catalog and pages-dict bodies.
    {
        std::ostringstream cat;
        cat << "<< /Type /Catalog /Pages " << pages_obj << " 0 R >>";
        objects[static_cast<std::size_t>(catalog_obj)] = cat.str();
    }
    {
        std::ostringstream kids;
        kids << "[ ";
        for (int num : page_obj_numbers) {
            kids << num << " 0 R ";
        }
        kids << "]";
        std::ostringstream pd;
        pd << "<< /Type /Pages /Kids " << kids.str() << " /Count "
           << page_obj_numbers.size() << " >>";
        objects[static_cast<std::size_t>(pages_obj)] = pd.str();
    }

    const int total_objects = static_cast<int>(objects.size()) - 1;

    // Build a stable /ID from a hash of object bodies — two identical inputs
    // produce identical /ID, and any content change flips it.
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
        std::string hex;
        hex.reserve(32);
        for (int byte = 7; byte >= 0; --byte) {
            const std::uint8_t b = static_cast<std::uint8_t>((h >> (byte * 8)) & 0xFF);
            hex += hex_byte(b);
        }
        // Pad to 16 bytes by repeating for a more conventional 32-hex-char ID.
        hex += hex;
        return hex;
    };
    const std::string id_hex = build_id();

    const std::string header = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    std::vector<std::streamoff> offsets(static_cast<std::size_t>(total_objects + 1));
    for (int i = 1; i <= total_objects; ++i) {
        offsets[i] = out.tellp();
        out << i << " 0 obj\n" << objects[static_cast<std::size_t>(i)] << "\nendobj\n";
    }

    const std::streamoff xref_pos = out.tellp();
    out << "xref\n0 " << (total_objects + 1) << "\n";
    out << "0000000000 65535 f \n";
    for (int i = 1; i <= total_objects; ++i) {
        out << std::setw(10) << std::setfill('0') << offsets[i]
            << " 00000 n \n";
    }
    out << "trailer\n<< /Size " << (total_objects + 1)
        << " /Root " << catalog_obj << " 0 R"
        << " /ID [<" << id_hex << "> <" << id_hex << ">]"
        << " >>\nstartxref\n" << xref_pos << "\n%%EOF\n";
    if (!out) {
        return fail("write failed for output file: " + path);
    }
    return true;
}

} // namespace mark2haru
