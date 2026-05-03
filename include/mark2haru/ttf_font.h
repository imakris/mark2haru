#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace mark2haru
{

class True_type_font
{
public:
    bool load_from_file(const std::filesystem::path& path);

    bool loaded() const { return !m_file_bytes.empty(); }
    const std::filesystem::path& source_path() const { return m_source_path; }
    const std::vector<std::uint8_t>& bytes() const { return m_file_bytes; }

    std::uint16_t units_per_em() const { return m_units_per_em; }
    std::int16_t ascent()        const { return m_ascent;         }
    std::int16_t descent()       const { return m_descent;        }
    std::int16_t line_gap()      const { return m_line_gap;       }
    std::uint16_t num_glyphs()   const { return m_num_glyphs;     }
    std::int16_t x_min()         const { return m_x_min;          }
    std::int16_t y_min()         const { return m_y_min;          }
    std::int16_t x_max()         const { return m_x_max;          }
    std::int16_t y_max()         const { return m_y_max;          }
    std::int16_t cap_height()    const { return m_cap_height;     }
    double italic_angle()        const { return m_italic_angle;   }
    bool is_fixed_pitch()        const { return m_is_fixed_pitch; }

    std::uint16_t glyph_for_codepoint(std::uint32_t codepoint) const;
    std::uint16_t advance_width_for_gid(std::uint16_t gid) const;

private:
    struct table_record_t
    {
        std::size_t offset = 0;
        std::size_t length = 0;
    };

    struct cmap4_t
    {
        std::size_t offset = 0;
    };

    struct cmap12_t
    {
        std::size_t offset = 0;
    };

    std::filesystem::path m_source_path;
    std::vector<std::uint8_t> m_file_bytes;
    std::uint16_t m_units_per_em = 1000;
    std::int16_t m_ascent     = 0;
    std::int16_t m_descent    = 0;
    std::int16_t m_line_gap   = 0;
    std::int16_t m_x_min      = 0;
    std::int16_t m_y_min      = 0;
    std::int16_t m_x_max      = 0;
    std::int16_t m_y_max      = 0;
    std::int16_t m_cap_height = 0;
    double m_italic_angle = 0.0;
    bool m_is_fixed_pitch = false;
    std::uint16_t m_num_glyphs   = 0;
    std::uint16_t m_num_hmetrics = 0;
    std::vector<std::uint16_t> m_advance_widths;
    cmap4_t  m_cmap4;
    cmap12_t m_cmap12;
    mutable std::unordered_map<std::uint32_t, std::uint16_t> m_glyph_cache;

    std::uint16_t lookup_cmap4( std::uint32_t codepoint) const;
    std::uint16_t lookup_cmap12(std::uint32_t codepoint) const;
};

} // namespace mark2haru
