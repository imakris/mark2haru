#include <mark2haru/font_context.h>

#include "utf8_decode.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

struct slot_descriptor_t
{
    const char* base14;
    const char* tag;
    std::array<const char*, 5> candidates;
};

constexpr std::array<slot_descriptor_t, 5> slot_descriptors = {{
    { "Helvetica",             "MHRegular",
      { "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "Arial.ttf", "arial.ttf" } },
    { "Helvetica-Bold",        "MHBold",
      { "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf", "Arial Bold.ttf", "arialbd.ttf" } },
    { "Helvetica-Oblique",     "MHItalic",
      { "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "LiberationSans-Italic.ttf", "Arial Italic.ttf", "ariali.ttf" } },
    { "Helvetica-BoldOblique", "MHBoldItalic",
      { "DejaVuSans-BoldOblique.ttf", "NotoSans-BoldItalic.ttf", "LiberationSans-BoldItalic.ttf", "Arial Bold Italic.ttf", "arialbi.ttf" } },
    { "Courier",               "MHMono",
      { "DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf", "Courier New.ttf", "cour.ttf" } },
}};

const slot_descriptor_t& slot_descriptor(Pdf_font font)
{
    return slot_descriptors[static_cast<std::size_t>(font)];
}

bool try_dir(
    const fs::path&                       dir,
    const std::array<const char*, 5>&     candidates,
    fs::path&                             out)
{
    for (const char* candidate : candidates) {
        const auto p = dir / candidate;
        std::error_code ec;
        if (fs::exists(p, ec) && !ec) {
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
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        roots.push_back(cwd);
        roots.push_back(cwd / "fonts");
    }

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

    // _dupenv_s on MSVC, std::getenv elsewhere — both yield a HOME string
    // (or empty on failure) that we then treat identically.
    std::string home_str;
#if defined(_MSC_VER)
    {
        char* raw = nullptr;
        size_t raw_len = 0;
        if (_dupenv_s(&raw, &raw_len, "HOME") == 0 && raw != nullptr) {
            home_str = raw;
            free(raw);
        }
    }
#else
    if (const char* raw = std::getenv("HOME")) {
        home_str = raw;
    }
#endif
    if (!home_str.empty()) {
        const fs::path home_path(home_str);
        roots.push_back(home_path / ".fonts");
        roots.push_back(home_path / ".local/share/fonts");
        roots.push_back(home_path / "Library/Fonts");
    }

    return roots;
}

} // namespace

font_family_config_t font_family_config_t::briefutil_default()
{
    return {};
}

font_source_t Measurement_context::slot_source(const font_family_config_t& family, Pdf_font font)
{
    const font_source_t* slots[] = {
        &family.regular, &family.bold, &family.italic, &family.bold_italic, &family.mono,
    };
    const font_source_t& configured = *slots[static_cast<std::size_t>(font)];
    return configured.empty()
        ? font_source_t::from_base14(slot_descriptor(font).base14)
        : configured;
}

std::string Measurement_context::default_base14_name(Pdf_font font)
{
    return slot_descriptor(font).base14;
}

std::string Measurement_context::slot_tag_name(Pdf_font font)
{
    return slot_descriptor(font).tag;
}

fs::path Measurement_context::resolve_font_path(
    const font_source_t& source,
    const fs::path& font_root,
    Pdf_font font)
{
    const auto& candidates = slot_descriptor(font).candidates;

    auto search_dir = [&](const fs::path& dir) -> fs::path {
        fs::path out;
        if (try_dir(dir, candidates, out)) {
            return out;
        }
        return {};
    };

    if (!source.path.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(source.path, ec) && !ec) {
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
        slot.tag_name = slot_tag_name(font);
    }

    m_loaded = true;
}

const True_type_font& Measurement_context::font_face(Pdf_font font) const
{
    return m_slots[static_cast<std::size_t>(font)].face;
}

const std::string& Measurement_context::font_tag_name(Pdf_font font) const
{
    return m_slots[static_cast<std::size_t>(font)].tag_name;
}

double Measurement_context::measure_text_width(Pdf_font font, const std::string& text, double size_pt) const
{
    const auto& face = font_face(font);
    double width_units = 0.0;
    for (std::uint32_t cp : utf8::decode(text)) {
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
