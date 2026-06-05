#include "miniz.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

void write_u32_be(std::ofstream& out, std::uint32_t value)
{
    const std::array<char, 4> bytes = {
        static_cast<char>((value >> 24) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >>  8) & 0xFF),
        static_cast<char>( value        & 0xFF),
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

    mz_ulong crc = mz_crc32(
        MZ_CRC32_INIT,
        reinterpret_cast<const unsigned char*>(type),
        4);
    if (!data.empty()) {
        crc = mz_crc32(crc, data.data(), data.size());
    }
    write_u32_be(out, static_cast<std::uint32_t>(crc));
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
            Z_BEST_COMPRESSION) != Z_OK)
    {
        return false;
    }
    compressed.resize(static_cast<std::size_t>(compressed_len));
    return true;
}

bool write_png(const fs::path& path)
{
    std::vector<std::uint8_t> compressed;
    if (!compress_bytes({ 0x00, 0x80 }, compressed)) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    static constexpr std::array<std::uint8_t, 8> signature = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    out.write(reinterpret_cast<const char*>(signature.data()), static_cast<std::streamsize>(signature.size()));

    std::vector<std::uint8_t> ihdr = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01,
        0x08,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    write_chunk(out, "IHDR", ihdr);
    write_chunk(out, "IDAT", compressed);
    write_chunk(out, "IEND", {});
    return static_cast<bool>(out);
}

bool write_text_file(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

bool file_contains(const fs::path& path, const std::string& needle)
{
    std::ifstream in(path, std::ios::binary);
    const std::string body{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    return body.find(needle) != std::string::npos;
}

bool setup_fixture(const fs::path& temp_dir)
{
    const fs::path input_dir = temp_dir / "input";
    const fs::path run_dir   = temp_dir / "run";

    fs::remove_all(temp_dir);
    fs::create_directories(input_dir / "assets");
    fs::create_directories(run_dir);

    if (!write_png(input_dir / "assets" / "inline.png")) {
        std::cerr << "failed to write test PNG\n";
        return false;
    }
    if (!write_text_file(input_dir / "doc.md", "![Alt](assets/inline.png)\n")) {
        std::cerr << "failed to write Markdown input\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: test_cli_relative_image <setup|check> <path>\n";
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "setup") {
        return setup_fixture(fs::path(argv[2])) ? 0 : 3;
    }
    if (mode == "check") {
        if (file_contains(fs::path(argv[2]), "/Subtype /Image")) {
            return 0;
        }
        std::cerr << "CLI output did not embed the relative image\n";
        return 4;
    }

    std::cerr << "unknown mode: " << mode << "\n";
    return 5;
}
