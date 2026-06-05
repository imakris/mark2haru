#pragma once

#include <mark2haru/font_context.h>

#include <array>
#include <cstddef>

namespace mark2haru
{

inline constexpr std::size_t k_pdf_font_candidate_count = 5;

struct pdf_font_descriptor_t
{
    Pdf_font                                             font;
    const char*                                          tag_name;
    const char*                                          resource_name;
    const char*                                          display_name;
    std::array<const char*, k_pdf_font_candidate_count>  candidates;
};

inline constexpr std::size_t pdf_font_index(Pdf_font font)
{
    const std::size_t index = static_cast<std::size_t>(font);
    return index < pdf_font_count ? index : 0;
}

inline constexpr std::array<pdf_font_descriptor_t, pdf_font_count> k_pdf_font_descriptors = {{
    {
        Pdf_font::REGULAR,
        "MHRegular",
        "/F1",
        "regular font",
        { "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf",
          "Arial.ttf", "arial.ttf" }
    },
    {
        Pdf_font::BOLD,
        "MHBold",
        "/F2",
        "bold font",
        { "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf",
          "Arial Bold.ttf", "arialbd.ttf" }
    },
    {
        Pdf_font::ITALIC,
        "MHItalic",
        "/F3",
        "italic font",
        { "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "LiberationSans-Italic.ttf",
          "Arial Italic.ttf", "ariali.ttf" }
    },
    {
        Pdf_font::BOLD_ITALIC,
        "MHBoldItalic",
        "/F4",
        "bold italic font",
        { "DejaVuSans-BoldOblique.ttf", "NotoSans-BoldItalic.ttf",
          "LiberationSans-BoldItalic.ttf", "Arial Bold Italic.ttf", "arialbi.ttf" }
    },
    {
        Pdf_font::MONO,
        "MHMono",
        "/F5",
        "monospace font",
        { "DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf",
          "Courier New.ttf", "cour.ttf" }
    },
}};

inline const pdf_font_descriptor_t& pdf_font_descriptor(Pdf_font font)
{
    return k_pdf_font_descriptors[pdf_font_index(font)];
}

} // namespace mark2haru
