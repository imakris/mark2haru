#pragma once

#include <mark2haru/font_context.h>
#include <mark2haru/png_image.h>

#include <array>
#include <filesystem>
#include <memory>
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

class PdfWriter {
public:
    PdfWriter(double page_width_pt, double page_height_pt,
              std::shared_ptr<const MeasurementContext> metrics);
    PdfWriter(double page_width_pt, double page_height_pt,
              const std::filesystem::path& font_root = {});

    bool fonts_loaded() const;
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
    bool draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt, const PngImage& image);
    bool draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt,
                  const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    double page_width_pt() const { return page_width_pt_; }
    double page_height_pt() const { return page_height_pt_; }

private:
    struct Page {
        std::string content;
        std::vector<std::size_t> image_indices;
    };

    struct LoadedFont {
        std::unordered_map<std::uint16_t, std::uint32_t> gid_to_unicode;
        std::unordered_set<std::uint16_t> used_glyphs;
        bool used = false;
    };

    struct LoadedImage {
        PngImage image;
        std::string resource_name;
    };

    double page_width_pt_ = 0.0;
    double page_height_pt_ = 0.0;
    Color stroke_{};
    Color fill_{};
    double line_width_pt_ = 0.5;
    std::vector<Page> pages_;
    std::shared_ptr<const MeasurementContext> metrics_;
    std::array<LoadedFont, 5> fonts_{};
    std::vector<LoadedImage> images_;
    bool fonts_loaded_ = false;
    std::string font_error_;

    std::string& current_content();
    Page& current_page();
    void append_color(std::string& out, const Color& c, bool stroke) const;
    static std::string font_resource_name(PdfFont font);
    static std::string utf8_to_hex_cid_string(const TrueTypeFont& font,
                                              LoadedFont& loaded,
                                              const std::string& text);
    static std::string make_to_unicode_cmap(const LoadedFont& font);
    static std::vector<PdfFont> used_fonts(const std::array<LoadedFont, 5>& fonts);
    static std::string encode_flate(const std::string& input);
    static std::string encode_flate(const std::vector<std::uint8_t>& input);
};

} // namespace mark2haru
