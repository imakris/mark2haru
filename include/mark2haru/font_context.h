#pragma once

#include <mark2haru/ttf_font.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace mark2haru {

enum class PdfFont {
    Regular,
    Bold,
    Italic,
    BoldItalic,
    Mono,
};

struct FontSource {
    std::filesystem::path path;
    std::string base14_name;

    static FontSource from_path(const std::filesystem::path& p)
    {
        FontSource out;
        out.path = p;
        return out;
    }

    static FontSource from_base14(const std::string& name)
    {
        FontSource out;
        out.base14_name = name;
        return out;
    }

    bool empty() const
    {
        return path.empty() && base14_name.empty();
    }
};

struct FontFamilyConfig {
    FontSource regular;
    FontSource bold;
    FontSource italic;
    FontSource bold_italic;
    FontSource mono;

    static FontFamilyConfig briefutil_default();
};

class MeasurementContext {
public:
    MeasurementContext(const FontFamilyConfig& family = FontFamilyConfig::briefutil_default(),
                       const std::filesystem::path& font_root = {});

    bool loaded() const { return loaded_; }
    const std::string& error() const { return error_; }

    double measure_text_width(PdfFont font, const std::string& text, double size_pt) const;
    const TrueTypeFont& font_face(PdfFont font) const;
    const std::filesystem::path& font_path(PdfFont font) const;
    const std::string& font_tag_name(PdfFont font) const;

private:
    struct Slot {
        TrueTypeFont face;
        std::filesystem::path source_path;
        std::string tag_name;
    };

    std::array<Slot, 5> slots_{};
    std::string error_;
    bool loaded_ = false;

    static FontSource slot_source(const FontFamilyConfig& family, PdfFont font);
    static std::string default_base14_name(PdfFont font);
    static std::string slot_tag_name(PdfFont font);
    static std::filesystem::path resolve_font_path(const FontSource& source,
                                                   const std::filesystem::path& font_root,
                                                   PdfFont font);
};

} // namespace mark2haru
