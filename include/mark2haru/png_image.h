#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mark2haru
{

class Png_image
{
public:
    bool load_from_file(const std::filesystem::path& path);

    bool loaded() const { return m_loaded; }
    const std::string& error() const { return m_error; }

    int width_px()         const { return m_width_px;         }
    int height_px()        const { return m_height_px;        }
    int color_components() const { return m_color_components; }
    bool has_alpha()       const { return !m_alpha.empty();   }

    const std::vector<std::uint8_t>& pixels() const { return m_pixels; }
    const std::vector<std::uint8_t>& alpha()  const { return m_alpha;  }

private:
    int m_width_px = 0;
    int m_height_px = 0;
    int m_color_components = 0;
    bool m_loaded = false;
    std::string m_error;
    std::vector<std::uint8_t> m_pixels;
    std::vector<std::uint8_t> m_alpha;

    bool decode_png(const std::vector<std::uint8_t>& file_bytes);
};

} // namespace mark2haru
