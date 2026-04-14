#pragma once

#include "ttf_font.h"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mark2haru {

struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

enum class PdfFont {
    Regular,
    Bold,
    Italic,
    BoldItalic,
    Mono,
};

class PdfWriter {
public:
    PdfWriter(double page_width_pt, double page_height_pt,
              const std::filesystem::path& font_root = {});

    // Explicit init: resolves and parses all five font faces. Returns false on
    // any failure; call `font_error()` for the reason. The constructor no
    // longer performs I/O so callers can construct a writer cheaply and then
    // fail gracefully.
    bool init();
    bool fonts_loaded() const { return fonts_loaded_; }
    const std::string& font_error() const { return font_error_; }

    void begin_page();
    void set_stroke_color(const Color& c);
    void set_fill_color(const Color& c);
    void set_line_width(double width_pt);
    void stroke_line(double x1_pt, double y1_top_pt, double x2_pt, double y2_top_pt);
    void stroke_rect(double x_pt, double y_top_pt, double w_pt, double h_pt);
    void fill_rect(double x_pt, double y_top_pt, double w_pt, double h_pt);
    double measure_text_width(PdfFont font, const std::string& text, double size_pt) const;
    void draw_text(double x_pt, double y_top_pt, double size_pt, PdfFont font,
                   const std::string& text);
    bool save(const std::string& path, std::string* error_out = nullptr) const;

    bool page_empty() const;
    double page_width_pt() const { return page_width_pt_; }
    double page_height_pt() const { return page_height_pt_; }

private:
    struct Page {
        std::string content;
    };

    struct LoadedFont {
        TrueTypeFont face;
        std::string resource_name;
        std::string tag_name;
        std::unordered_map<std::uint16_t, std::uint32_t> gid_to_unicode;
        std::unordered_set<std::uint16_t> used_glyphs;
        bool used = false;
    };

    double page_width_pt_ = 0.0;
    double page_height_pt_ = 0.0;
    Color stroke_{};
    Color fill_{};
    double line_width_pt_ = 0.5;
    std::vector<Page> pages_;
    std::filesystem::path font_root_;
    std::array<LoadedFont, 5> fonts_{};
    bool fonts_loaded_ = false;
    std::string font_error_;

    std::string& current_content();
    void append_color(std::string& out, const Color& c, bool stroke) const;
    bool load_fonts();
    static std::string escape_pdf_text(const std::string& text);
    static std::string font_resource_name(PdfFont font);
    static std::string font_tag_name(PdfFont font);
    LoadedFont& font_slot(PdfFont font);
    const LoadedFont& font_slot(PdfFont font) const;
    static std::filesystem::path resolve_font_path(
        const std::filesystem::path& root,
        const std::vector<std::string>& candidates);
    static std::string utf8_to_hex_cid_string(const TrueTypeFont& font,
                                              LoadedFont& loaded,
                                              const std::string& text);
    static std::string make_to_unicode_cmap(const LoadedFont& font);
    static std::vector<PdfFont> used_fonts(const std::array<LoadedFont, 5>& fonts);
};

} // namespace mark2haru
