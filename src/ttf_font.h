#pragma once

#include <cstdint>
#include <filesystem>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace mark2haru {

class TrueTypeFont {
public:
    bool load_from_file(const std::filesystem::path& path);

    bool loaded() const { return !file_bytes_.empty(); }
    const std::string& source_path() const { return source_path_; }
    const std::vector<std::uint8_t>& bytes() const { return file_bytes_; }

    std::uint16_t units_per_em() const { return units_per_em_; }
    std::int16_t ascent() const { return ascent_; }
    std::int16_t descent() const { return descent_; }
    std::int16_t line_gap() const { return line_gap_; }
    std::uint16_t num_glyphs() const { return num_glyphs_; }
    std::int16_t x_min() const { return x_min_; }
    std::int16_t y_min() const { return y_min_; }
    std::int16_t x_max() const { return x_max_; }
    std::int16_t y_max() const { return y_max_; }

    std::uint16_t glyph_for_codepoint(std::uint32_t codepoint) const;
    std::uint16_t advance_width_for_gid(std::uint16_t gid) const;
    double advance_width_pt(std::uint32_t codepoint, double size_pt) const;

    static std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::uint32_t offset);
    static std::int16_t read_i16(const std::vector<std::uint8_t>& data, std::uint32_t offset);
    static std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::uint32_t offset);

private:
    struct TableRecord {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    struct Cmap4 {
        std::uint32_t offset = 0;
    };

    struct Cmap12 {
        std::uint32_t offset = 0;
    };

    std::string source_path_;
    std::vector<std::uint8_t> file_bytes_;
    std::uint16_t units_per_em_ = 1000;
    std::int16_t ascent_ = 0;
    std::int16_t descent_ = 0;
    std::int16_t line_gap_ = 0;
    std::int16_t x_min_ = 0;
    std::int16_t y_min_ = 0;
    std::int16_t x_max_ = 0;
    std::int16_t y_max_ = 0;
    std::uint16_t num_glyphs_ = 0;
    std::uint16_t num_hmetrics_ = 0;
    std::vector<std::uint16_t> advance_widths_;
    Cmap4 cmap4_;
    Cmap12 cmap12_;
    mutable std::unordered_map<std::uint32_t, std::uint16_t> glyph_cache_;

    const TableRecord* find_table(const char tag[4]) const;
    std::uint16_t lookup_cmap4(std::uint32_t codepoint) const;
    std::uint16_t lookup_cmap12(std::uint32_t codepoint) const;
};

} // namespace mark2haru
