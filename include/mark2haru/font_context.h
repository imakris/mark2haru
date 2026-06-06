#pragma once

#include <mark2haru/font_config.h>
#include <mark2haru/ttf_font.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

namespace mark2haru
{

enum class Pdf_font
{
    REGULAR,
    BOLD,
    ITALIC,
    BOLD_ITALIC,
    MONO,
    COUNT,
};

inline constexpr std::size_t pdf_font_count = static_cast<std::size_t>(Pdf_font::COUNT);

class Measurement_context
{
public:
    Measurement_context(
        const Font_family_config& family = Font_family_config::bundled_default(),
        const std::filesystem::path& font_root = {});

    bool               loaded() const { return m_loaded; }
    const std::string& error()  const { return m_error;  }

    /**
     * @brief Measure UTF-8 text using the selected logical font.
     *
     * Font faces cache glyph lookups internally, so concurrent access to the
     * same Measurement_context instance requires external synchronization.
     */
    double measure_text_width(Pdf_font font, const std::string& text, double size_pt) const;
    const True_type_font& font_face(Pdf_font font) const;
    const std::string& font_tag_name(Pdf_font font) const;

private:
    struct Slot
    {
        True_type_font face;
        std::string    tag_name;
    };

    std::array<Slot, pdf_font_count>  m_slots{};
    std::string                       m_error;
    bool                              m_loaded = false;

    static const Font_source& slot_source(
        const Font_family_config&      family,
        Pdf_font                       font);

    static std::filesystem::path resolve_font_path(
        const Font_source&             source,
        const std::filesystem::path&   font_root,
        Pdf_font                       font);
};

} // namespace mark2haru
