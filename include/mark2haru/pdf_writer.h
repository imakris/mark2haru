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

namespace mark2haru
{

struct color_t
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

class Pdf_writer
{
public:
    Pdf_writer(
        double page_width_pt,
        double page_height_pt,
        std::shared_ptr<const Measurement_context> metrics);
    Pdf_writer(
        double page_width_pt,
        double page_height_pt,
        const std::filesystem::path& font_root = {});

    bool fonts_loaded() const;
    const std::string& font_error() const { return m_font_error; }

    void begin_page();
    void set_stroke_color(const color_t& color);
    void set_fill_color(const color_t& color);
    void set_line_width(double width_pt);
    void stroke_line(double x1_pt, double y1_top_pt, double x2_pt, double y2_top_pt);
    void stroke_rect(double x_pt, double y_top_pt, double w_pt, double h_pt);
    void fill_rect(double x_pt, double y_top_pt, double w_pt, double h_pt);
    double measure_text_width(Pdf_font font, const std::string& text, double size_pt) const;
    void draw_text(
        double x_pt,
        double y_top_pt,
        double size_pt,
        Pdf_font font,
        const std::string& text);
    bool draw_png(double x_pt, double y_top_pt, double w_pt, double h_pt, const Png_image& image);
    bool draw_png(
        double x_pt, double y_top_pt, double w_pt, double h_pt,
        const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    double page_width_pt()  const { return m_page_width_pt;  }
    double page_height_pt() const { return m_page_height_pt; }

private:
    struct page_t
    {
        std::string content;
        std::vector<std::size_t> image_indices;
    };

    struct loaded_font_t
    {
        std::unordered_map<std::uint16_t, std::uint32_t> gid_to_unicode;
        std::unordered_set<std::uint16_t> used_glyphs;
        bool used = false;
    };

    struct loaded_image_t
    {
        Png_image image;
        std::string resource_name;
    };

    double m_page_width_pt  = 0.0;
    double m_page_height_pt = 0.0;
    color_t m_stroke{};
    color_t m_fill{};
    double m_line_width_pt = 0.5;
    std::vector<page_t> m_pages;
    std::shared_ptr<const Measurement_context> m_metrics;
    std::array<loaded_font_t, 5> m_fonts{};
    std::vector<loaded_image_t> m_images;
    bool m_fonts_loaded = false;
    std::string m_font_error;

    std::string& current_content();
    page_t& current_page();
    void append_color(std::string& out, const color_t& color, bool stroke) const;
    static std::string font_resource_name(Pdf_font font);
    static std::string utf8_to_hex_cid_string(
        const True_type_font& font,
        loaded_font_t& loaded,
        const std::string& text);
    static std::string make_to_unicode_cmap(const loaded_font_t& font);
    static std::vector<Pdf_font> used_fonts(const std::array<loaded_font_t, 5>& fonts);
    static std::string encode_flate(const std::string& input);
    static std::string encode_flate(const std::vector<std::uint8_t>& input);
};

} // namespace mark2haru
