#include <mark2haru/pdf_writer.h>
#include <mark2haru/png_image.h>

#include "miniz.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

void write_u32_be(std::ofstream& out, std::uint32_t value)
{
    const std::array<char, 4> bytes = {
        static_cast<char>((value >> 24) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>(value & 0xFF),
    };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_chunk(std::ofstream& out, const char type[4], const std::vector<std::uint8_t>& data)
{
    write_u32_be(out, static_cast<std::uint32_t>(data.size()));
    out.write(type, 4);
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    write_u32_be(out, 0);
}

bool compress_bytes(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& compressed)
{
    mz_ulong compressed_len = compressBound(static_cast<uLong>(raw.size()));
    compressed.resize(static_cast<std::size_t>(compressed_len));
    if (compress2(
            reinterpret_cast<Bytef*>(compressed.data()),
            &compressed_len,
            reinterpret_cast<const Bytef*>(raw.data()),
            static_cast<uLong>(raw.size()),
            Z_BEST_COMPRESSION) != Z_OK) {
        return false;
    }
    compressed.resize(static_cast<std::size_t>(compressed_len));
    return true;
}

bool write_png(
    const fs::path& path,
    int width,
    int height,
    std::uint8_t bit_depth,
    std::uint8_t color_type,
    const std::vector<std::uint8_t>& raw_scanlines,
    const std::vector<std::uint8_t>* palette = nullptr,
    const std::vector<std::uint8_t>* trns = nullptr)
{
    std::vector<std::uint8_t> compressed;
    if (!compress_bytes(raw_scanlines, compressed)) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    static constexpr std::array<std::uint8_t, 8> signature = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out.write(reinterpret_cast<const char*>(signature.data()), static_cast<std::streamsize>(signature.size()));

    std::vector<std::uint8_t> ihdr;
    ihdr.reserve(13);
    ihdr.push_back(static_cast<std::uint8_t>((width >> 24) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>((width >> 16) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>((width >> 8) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>(width & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>((height >> 24) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>((height >> 16) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>((height >> 8) & 0xFF));
    ihdr.push_back(static_cast<std::uint8_t>(height & 0xFF));
    ihdr.push_back(bit_depth);
    ihdr.push_back(color_type);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    write_chunk(out, "IHDR", ihdr);

    if (palette != nullptr) {
        write_chunk(out, "PLTE", *palette);
    }
    if (trns != nullptr) {
        write_chunk(out, "tRNS", *trns);
    }
    write_chunk(out, "IDAT", compressed);
    write_chunk(out, "IEND", {});
    return static_cast<bool>(out);
}

bool starts_with_pdf_header(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    char header[4] = {};
    in.read(header, 4);
    return in.gcount() == 4 && header[0] == '%' && header[1] == 'P' && header[2] == 'D' && header[3] == 'F';
}

bool expect_png(
    const fs::path& path,
    int width,
    int height,
    int color_components,
    bool has_alpha,
    const std::vector<std::uint8_t>& pixels,
    const std::vector<std::uint8_t>& alpha)
{
    mark2haru::Png_image image;
    if (!image.load_from_file(path)) {
        std::cerr << "load failed: " << image.error() << "\n";
        return false;
    }
    if (!image.loaded()) {
        std::cerr << "image not marked loaded\n";
        return false;
    }
    if (image.width_px() != width || image.height_px() != height) {
        std::cerr << "unexpected image size\n";
        return false;
    }
    if (image.color_components() != color_components) {
        std::cerr << "unexpected color components\n";
        return false;
    }
    if (image.has_alpha() != has_alpha) {
        std::cerr << "unexpected alpha flag\n";
        return false;
    }
    if (image.pixels() != pixels) {
        std::cerr << "unexpected pixel payload\n";
        return false;
    }
    if (has_alpha && image.alpha() != alpha) {
        std::cerr << "unexpected alpha payload\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 1) {
        return 2;
    }

    const fs::path exe_dir  = fs::path(argv[0]).parent_path();
    const fs::path temp_dir = fs::temp_directory_path() / "mark2haru_png_test";
    fs::create_directories(temp_dir);

    const auto gray1_path = temp_dir / "gray1.png";
    const auto indexed_path = temp_dir / "indexed.png";
    const auto rgba16_path = temp_dir / "rgba16.png";
    const auto pdf_path = temp_dir / "mark2haru_png_test.pdf";

    if (!write_png(gray1_path, 1, 1, 1, 0, { 0x00, 0x80 })) {
        std::cerr << "failed to write gray1 png\n";
        return 3;
    }

    const std::vector<std::uint8_t> palette = {
        0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00,
        0x01, 0x23, 0x45,
    };
    const std::vector<std::uint8_t> palette_alpha = { 255, 200, 17 };
    if (!write_png(indexed_path, 1, 1, 2, 3, { 0x00, 0x80 }, &palette, &palette_alpha)) {
        std::cerr << "failed to write indexed png\n";
        return 4;
    }

    if (!write_png(
            rgba16_path, 1, 1, 16, 6,
            { 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 })) {
        std::cerr << "failed to write rgba16 png\n";
        return 5;
    }

    if (!expect_png(gray1_path, 1, 1, 1, false, { 0xFF }, {})) {
        return 6;
    }
    if (!expect_png(indexed_path, 1, 1, 3, true, { 0x01, 0x23, 0x45 }, { 17 })) {
        return 7;
    }
    if (!expect_png(rgba16_path, 1, 1, 3, true, { 0x12, 0x56, 0x9A }, { 0xDE })) {
        return 8;
    }

    auto metrics = std::make_shared<mark2haru::Measurement_context>(
        mark2haru::font_family_config_t::briefutil_default(),
        exe_dir);
    if (!metrics->loaded()) {
        std::cerr << metrics->error() << "\n";
        return 9;
    }

    mark2haru::Png_image image;
    if (!image.load_from_file(rgba16_path) || !image.loaded() || !image.has_alpha()) {
        std::cerr << "png load failed\n";
        return 10;
    }

    mark2haru::Pdf_writer writer(200.0, 200.0, metrics);
    if (!writer.fonts_loaded()) {
        std::cerr << writer.font_error() << "\n";
        return 11;
    }

    if (!writer.draw_png(10.0, 10.0, 32.0, 32.0, image)) {
        std::cerr << "draw png failed\n";
        return 12;
    }

    if (!writer.save(pdf_path)) {
        std::cerr << "save failed\n";
        return 13;
    }

    if (!starts_with_pdf_header(pdf_path)) {
        std::cerr << "pdf header missing\n";
        return 14;
    }

    return 0;
}
