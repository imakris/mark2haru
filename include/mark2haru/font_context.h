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

struct font_source_t
{
    std::filesystem::path path;
    std::string base14_name;

    static font_source_t from_path(const std::filesystem::path& path)
    {
        font_source_t out;
        out.path = path;
        return out;
    }

    static font_source_t from_base14(const std::string& name)
    {
        font_source_t out;
        out.base14_name = name;
        return out;
    }

    bool empty() const
    {
        return path.empty() && base14_name.empty();
    }
};

struct font_family_config_t
{
    font_source_t regular;
    font_source_t bold;
    font_source_t italic;
    font_source_t bold_italic;
    font_source_t mono;

    static font_family_config_t briefutil_default();
};

class Measurement_context
{
public:
    Measurement_context(
        const font_family_config_t& family = font_family_config_t::briefutil_default(),
        const std::filesystem::path& font_root = {});

    bool loaded()              const { return m_loaded; }
    const std::string& error() const { return m_error;  }

    double measure_text_width(Pdf_font font, const std::string& text, double size_pt) const;
    const True_type_font& font_face(Pdf_font font) const;
    const std::string& font_tag_name(Pdf_font font) const;

private:
    struct slot_t
    {
        True_type_font face;
        std::string tag_name;
    };

    std::array<slot_t, 5> m_slots{};
    std::string m_error;
    bool m_loaded = false;

    static font_source_t slot_source(const font_family_config_t& family, Pdf_font font);
    static std::string default_base14_name(Pdf_font font);
    static std::string slot_tag_name(Pdf_font font);
    static std::filesystem::path resolve_font_path(
        const font_source_t& source,
        const std::filesystem::path& font_root,
        Pdf_font font);
};

} // namespace mark2haru
