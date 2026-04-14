#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mark2haru {

class PngImage {
public:
    bool load_from_file(const std::filesystem::path& path);

    bool loaded() const { return loaded_; }
    const std::string& error() const { return error_; }

    int width_px() const { return width_px_; }
    int height_px() const { return height_px_; }
    int color_components() const { return color_components_; }
    bool has_alpha() const { return !alpha_.empty(); }

    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    const std::vector<std::uint8_t>& alpha() const { return alpha_; }

private:
    int width_px_ = 0;
    int height_px_ = 0;
    int color_components_ = 0;
    bool loaded_ = false;
    std::string error_;
    std::vector<std::uint8_t> pixels_;
    std::vector<std::uint8_t> alpha_;

    bool decode_png(const std::vector<std::uint8_t>& file_bytes);
};

} // namespace mark2haru
