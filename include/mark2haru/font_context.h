#pragma once

#include <mark2haru/ttf_font.h>

#include <array>
#include <filesystem>
#include <memory>
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
};

struct Font_source
{
    std::filesystem::path path;
    std::string base14_name;

    static Font_source from_path(const std::filesystem::path& path)
    {
        Font_source out;
        out.path = path;
        return out;
    }

    static Font_source from_base14(const std::string& name)
    {
        Font_source out;
        out.base14_name = name;
        return out;
    }

    bool empty() const
    {
        return path.empty() && base14_name.empty();
    }
};

struct Font_family_config
{
    Font_source regular;
    Font_source bold;
    Font_source italic;
    Font_source bold_italic;
    Font_source mono;

    static Font_family_config briefutil_default();
};

class Measurement_context
{
public:
    Measurement_context(
        const Font_family_config& family = Font_family_config::briefutil_default(),
        const std::filesystem::path& font_root = {});

    bool loaded()              const { return m_loaded; }
    const std::string& error() const { return m_error;  }

    double measure_text_width(Pdf_font font, const std::string& text, double size_pt) const;
    const True_type_font& font_face(Pdf_font font) const;
    const std::string& font_tag_name(Pdf_font font) const;

private:
    struct Slot
    {
        True_type_font face;
        std::string tag_name;
    };

    std::array<Slot, 5> m_slots{};
    std::string m_error;
    bool m_loaded = false;

    static Font_source slot_source(const Font_family_config& family, Pdf_font font);
    static std::string default_base14_name(Pdf_font font);
    static std::string slot_tag_name(Pdf_font font);
    static std::filesystem::path resolve_font_path(
        const Font_source& source,
        const std::filesystem::path& font_root,
        Pdf_font font);
};

} // namespace mark2haru
