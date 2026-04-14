#include <mark2haru/font_context.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

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

bool try_dir(
    const fs::path&                 dir,
    const std::vector<std::string>& candidates,
    fs::path&                       out)
{
    for (const auto& candidate : candidates) {
        const auto p = dir / candidate;
        if (fs::exists(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

std::vector<fs::path> search_roots(const fs::path& font_root)
{
    std::vector<fs::path> roots;
    if (!font_root.empty()) {
        roots.push_back(font_root);
        roots.push_back(font_root / "fonts");
    }
    roots.push_back(fs::current_path());
    roots.push_back(fs::current_path() / "fonts");

    const std::vector<fs::path> system_roots = {
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
        const fs::path home_path(home);
        roots.push_back(home_path / ".fonts");
        roots.push_back(home_path / ".local/share/fonts");
        roots.push_back(home_path / "Library/Fonts");
        free(home);
    }
#else
    if (const char* home = std::getenv("HOME")) {
        const fs::path home_path(home);
        roots.push_back(home_path / ".fonts");
        roots.push_back(home_path / ".local/share/fonts");
        roots.push_back(home_path / "Library/Fonts");
    }
#endif

    return roots;
}

} // namespace

font_family_config_t font_family_config_t::briefutil_default()
{
    font_family_config_t config;
    config.regular     = font_source_t::from_base14("Helvetica");
    config.bold        = font_source_t::from_base14("Helvetica-Bold");
    config.italic      = font_source_t::from_base14("Helvetica-Oblique");
    config.bold_italic = font_source_t::from_base14("Helvetica-BoldOblique");
    config.mono        = font_source_t::from_base14("Courier");
    return config;
}

font_source_t Measurement_context::slot_source(const font_family_config_t& family, Pdf_font font)
{
    switch (font) {
        case Pdf_font::REGULAR:
            return family.regular.empty() ? font_source_t::from_base14(default_base14_name(font)) : family.regular;
        case Pdf_font::BOLD:
            return family.bold.empty() ? font_source_t::from_base14(default_base14_name(font)) : family.bold;
        case Pdf_font::ITALIC:
            return family.italic.empty() ? font_source_t::from_base14(default_base14_name(font)) : family.italic;
        case Pdf_font::BOLD_ITALIC:
            return family.bold_italic.empty() ? font_source_t::from_base14(default_base14_name(font)) : family.bold_italic;
        case Pdf_font::MONO:
            return family.mono.empty() ? font_source_t::from_base14(default_base14_name(font)) : family.mono;
        default:
            return font_source_t::from_base14(default_base14_name(Pdf_font::REGULAR));
    }
}

std::string Measurement_context::default_base14_name(Pdf_font font)
{
    switch (font) {
        case Pdf_font::REGULAR:     return "Helvetica";
        case Pdf_font::BOLD:        return "Helvetica-Bold";
        case Pdf_font::ITALIC:      return "Helvetica-Oblique";
        case Pdf_font::BOLD_ITALIC: return "Helvetica-BoldOblique";
        case Pdf_font::MONO:        return "Courier";
        default:                    return "Helvetica";
    }
}

std::string Measurement_context::slot_tag_name(Pdf_font font)
{
    switch (font) {
        case Pdf_font::REGULAR:     return "MHRegular";
        case Pdf_font::BOLD:        return "MHBold";
        case Pdf_font::ITALIC:      return "MHItalic";
        case Pdf_font::BOLD_ITALIC: return "MHBoldItalic";
        case Pdf_font::MONO:        return "MHMono";
        default:                    return "MHRegular";
    }
}

fs::path Measurement_context::resolve_font_path(
    const font_source_t& source,
    const fs::path& font_root,
    Pdf_font font)
{
    const auto& candidates = candidate_sets()[static_cast<std::size_t>(font)];

    auto search_dir = [&](const fs::path& dir) -> fs::path {
        fs::path out;
        if (try_dir(dir, candidates, out)) {
            return out;
        }
        return {};
    };

    if (!source.path.empty()) {
        if (fs::is_regular_file(source.path)) {
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

Measurement_context::Measurement_context(
    const font_family_config_t& family,
    const fs::path& font_root)
{
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const Pdf_font font = static_cast<Pdf_font>(i);
        const font_source_t source = slot_source(family, font);
        const auto path = resolve_font_path(source, font_root, font);
        if (path.empty()) {
            m_error = "Unable to resolve font for " + default_base14_name(font);
            return;
        }
        auto& slot = m_slots[i];
        if (!slot.face.load_from_file(path)) {
            m_error = "Unable to load font file for " + default_base14_name(font);
            return;
        }
        slot.source_path = path;
        slot.tag_name = slot_tag_name(font);
    }

    m_loaded = true;
}

namespace {

std::vector<std::uint32_t> decode_utf8(const std::string& text)
{
    std::vector<std::uint32_t> cps;
    cps.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = '?';
        std::size_t advance = 1;
        if (c < 0x80) {
            cp = c;
        }
        else
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            advance = 2;
        }
        else
        if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12)
                | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            advance = 3;
        }
        else
        if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
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

const True_type_font& Measurement_context::font_face(Pdf_font font) const
{
    return m_slots[static_cast<std::size_t>(font)].face;
}

const fs::path& Measurement_context::font_path(Pdf_font font) const
{
    return m_slots[static_cast<std::size_t>(font)].source_path;
}

const std::string& Measurement_context::font_tag_name(Pdf_font font) const
{
    return m_slots[static_cast<std::size_t>(font)].tag_name;
}

double Measurement_context::measure_text_width(Pdf_font font, const std::string& text, double size_pt) const
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
