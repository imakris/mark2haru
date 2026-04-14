#include "font_context.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace mark2haru {
namespace {

const std::array<std::vector<std::string>, 5>& candidate_sets()
{
    static const std::array<std::vector<std::string>, 5> candidates = {{
        { "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "Arial.ttf", "arial.ttf" },
        { "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf", "Arial Bold.ttf", "arialbd.ttf" },
        { "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "LiberationSans-Italic.ttf", "Arial Italic.ttf", "ariali.ttf" },
        { "DejaVuSans-BoldOblique.ttf", "NotoSans-BoldItalic.ttf", "LiberationSans-BoldItalic.ttf", "Arial Bold Italic.ttf", "arialbi.ttf" },
        { "DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf", "Courier New.ttf", "cour.ttf" },
    }};
    return candidates;
}

bool try_dir(const std::filesystem::path& dir,
             const std::vector<std::string>& candidates,
             std::filesystem::path& out)
{
    for (const auto& candidate : candidates) {
        const auto p = dir / candidate;
        if (std::filesystem::exists(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

std::vector<std::filesystem::path> search_roots(const std::filesystem::path& font_root)
{
    std::vector<std::filesystem::path> roots;
    if (!font_root.empty()) {
        roots.push_back(font_root);
        roots.push_back(font_root / "fonts");
    }
    roots.push_back(std::filesystem::current_path());
    roots.push_back(std::filesystem::current_path() / "fonts");

    const std::vector<std::filesystem::path> system_roots = {
        R"(C:\Windows\Fonts)",
        R"(C:\Windows\Fonts\truetype)",
        "/usr/share/fonts",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/dejavu",
        "/usr/local/share/fonts",
        "/Library/Fonts",
        "/System/Library/Fonts",
        "/System/Library/Fonts/Supplemental",
    };
    roots.insert(roots.end(), system_roots.begin(), system_roots.end());

#if defined(_MSC_VER)
    char* home = nullptr;
    size_t home_len = 0;
    if (_dupenv_s(&home, &home_len, "HOME") == 0 && home != nullptr) {
        const std::filesystem::path home_path(home);
        roots.push_back(home_path / ".fonts");
        roots.push_back(home_path / ".local/share/fonts");
        roots.push_back(home_path / "Library/Fonts");
        free(home);
    }
#else
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path home_path(home);
        roots.push_back(home_path / ".fonts");
        roots.push_back(home_path / ".local/share/fonts");
        roots.push_back(home_path / "Library/Fonts");
    }
#endif

    return roots;
}

} // namespace

FontFamilyConfig FontFamilyConfig::briefutil_default()
{
    FontFamilyConfig config;
    config.regular = FontSource::from_base14("Helvetica");
    config.bold = FontSource::from_base14("Helvetica-Bold");
    config.italic = FontSource::from_base14("Helvetica-Oblique");
    config.bold_italic = FontSource::from_base14("Helvetica-BoldOblique");
    config.mono = FontSource::from_base14("Courier");
    return config;
}

FontSource MeasurementContext::slot_source(const FontFamilyConfig& family, PdfFont font)
{
    switch (font) {
    case PdfFont::Regular:
        return family.regular.empty() ? FontSource::from_base14(default_base14_name(font)) : family.regular;
    case PdfFont::Bold:
        return family.bold.empty() ? FontSource::from_base14(default_base14_name(font)) : family.bold;
    case PdfFont::Italic:
        return family.italic.empty() ? FontSource::from_base14(default_base14_name(font)) : family.italic;
    case PdfFont::BoldItalic:
        return family.bold_italic.empty() ? FontSource::from_base14(default_base14_name(font)) : family.bold_italic;
    case PdfFont::Mono:
        return family.mono.empty() ? FontSource::from_base14(default_base14_name(font)) : family.mono;
    }
    return FontSource::from_base14(default_base14_name(PdfFont::Regular));
}

std::string MeasurementContext::default_base14_name(PdfFont font)
{
    switch (font) {
    case PdfFont::Regular: return "Helvetica";
    case PdfFont::Bold: return "Helvetica-Bold";
    case PdfFont::Italic: return "Helvetica-Oblique";
    case PdfFont::BoldItalic: return "Helvetica-BoldOblique";
    case PdfFont::Mono: return "Courier";
    }
    return "Helvetica";
}

std::string MeasurementContext::slot_tag_name(PdfFont font)
{
    switch (font) {
    case PdfFont::Regular: return "MHRegular";
    case PdfFont::Bold: return "MHBold";
    case PdfFont::Italic: return "MHItalic";
    case PdfFont::BoldItalic: return "MHBoldItalic";
    case PdfFont::Mono: return "MHMono";
    }
    return "MHRegular";
}

std::filesystem::path MeasurementContext::resolve_font_path(const FontSource& source,
                                                            const std::filesystem::path& font_root,
                                                            PdfFont font)
{
    const auto& candidates = candidate_sets()[static_cast<std::size_t>(font)];

    auto search_dir = [&](const std::filesystem::path& dir) -> std::filesystem::path {
        std::filesystem::path out;
        if (try_dir(dir, candidates, out)) {
            return out;
        }
        return {};
    };

    if (!source.path.empty()) {
        if (std::filesystem::is_regular_file(source.path)) {
            return source.path;
        }
        if (auto resolved = search_dir(source.path); !resolved.empty()) {
            return resolved;
        }
        if (auto resolved = search_dir(source.path / "fonts"); !resolved.empty()) {
            return resolved;
        }
    }

    for (const auto& root : search_roots(font_root)) {
        if (auto resolved = search_dir(root); !resolved.empty()) {
            return resolved;
        }
    }
    return {};
}

MeasurementContext::MeasurementContext(const FontFamilyConfig& family, const std::filesystem::path& font_root)
{
    for (size_t i = 0; i < slots_.size(); ++i) {
        const PdfFont font = static_cast<PdfFont>(i);
        const FontSource source = slot_source(family, font);
        const auto path = resolve_font_path(source, font_root, font);
        if (path.empty()) {
            error_ = "Unable to resolve font for " + default_base14_name(font);
            return;
        }
        auto& slot = slots_[i];
        if (!slot.face.load_from_file(path)) {
            error_ = "Unable to load font file for " + default_base14_name(font);
            return;
        }
        slot.source_path = path;
        slot.tag_name = slot_tag_name(font);
    }

    loaded_ = true;
}

namespace {

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

} // namespace

const TrueTypeFont& MeasurementContext::font_face(PdfFont font) const
{
    return slots_[static_cast<std::size_t>(font)].face;
}

const std::filesystem::path& MeasurementContext::font_path(PdfFont font) const
{
    return slots_[static_cast<std::size_t>(font)].source_path;
}

const std::string& MeasurementContext::font_tag_name(PdfFont font) const
{
    return slots_[static_cast<std::size_t>(font)].tag_name;
}

double MeasurementContext::measure_text_width(PdfFont font, const std::string& text, double size_pt) const
{
    const auto& face = font_face(font);
    double width_units = 0.0;
    for (std::uint32_t cp : decode_utf8(text)) {
        std::uint16_t gid = face.glyph_for_codepoint(cp);
        if (gid == 0) {
            gid = face.glyph_for_codepoint('?');
        }
        width_units += face.advance_width_for_gid(gid);
    }
    if (face.units_per_em() == 0) {
        return 0.0;
    }
    return width_units * size_pt / static_cast<double>(face.units_per_em());
}

} // namespace mark2haru
