#include <mark2haru/png_image.h>

#include "io_helpers.h"
#include "miniz.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

// Reasonable caps to bound work and memory for malicious or accidentally
// pathological inputs. PNG IHDR allows up to 2^31-1 in each dimension; we
// reject anything larger than the cap below before allocating.
constexpr std::size_t k_max_image_dimension = 32768;
constexpr std::size_t k_max_image_pixels    = 64u * 1024u * 1024u; // 64M pixels
constexpr std::size_t k_max_idat_bytes      = 256u * 1024u * 1024u; // 256 MiB

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
        | static_cast<std::uint16_t>(bytes[offset + 1]));
}

// Adds two size_t values and reports overflow. Returns true on success;
// `result` is undefined on overflow.
bool checked_add(std::size_t a, std::size_t b, std::size_t& result)
{
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return false;
    }
    result = a + b;
    return true;
}

// Multiplies two size_t values and reports overflow.
bool checked_mul(std::size_t a, std::size_t b, std::size_t& result)
{
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    result = a * b;
    return true;
}

std::uint8_t paeth_predictor(std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

bool is_supported_bit_depth(std::uint8_t color_type, std::uint8_t bit_depth)
{
    switch (color_type) {
        case 0:  return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16;
        case 2:  return bit_depth == 8 || bit_depth == 16;
        case 3:  return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
        case 4:  return bit_depth == 8 || bit_depth == 16;
        case 6:  return bit_depth == 8 || bit_depth == 16;
        default:
            return false;
    }
}

int encoded_components(std::uint8_t color_type)
{
    switch (color_type) {
        case 0:  return 1;
        case 2:  return 3;
        case 3:  return 1;
        case 4:  return 2;
        case 6:  return 4;
        default:
            return 0;
    }
}

std::size_t encoded_bytes_per_pixel(std::uint8_t color_type, std::uint8_t bit_depth)
{
    const std::size_t components = static_cast<std::size_t>(encoded_components(color_type));
    if (components == 0) {
        return 0;
    }
    const std::size_t bytes = (components * static_cast<std::size_t>(bit_depth) + 7) / 8;
    return std::max<std::size_t>(1, bytes);
}

std::size_t encoded_row_bytes(int width_px, std::uint8_t color_type, std::uint8_t bit_depth)
{
    const std::size_t components = static_cast<std::size_t>(encoded_components(color_type));
    if (components == 0) {
        return 0;
    }
    if (bit_depth < 8) {
        return (static_cast<std::size_t>(width_px) * components * static_cast<std::size_t>(bit_depth) + 7) / 8;
    }
    return static_cast<std::size_t>(width_px) * components * (static_cast<std::size_t>(bit_depth) / 8);
}

std::uint16_t packed_sample(const std::vector<std::uint8_t>& row, std::size_t pixel_index, std::uint8_t bit_depth)
{
    const std::size_t bit_offset = pixel_index * static_cast<std::size_t>(bit_depth);
    const std::size_t byte_index = bit_offset / 8;
    const std::size_t shift = 8 - static_cast<std::size_t>(bit_depth) - (bit_offset % 8);
    const std::uint8_t mask = static_cast<std::uint8_t>((1u << bit_depth) - 1u);
    return static_cast<std::uint16_t>((row[byte_index] >> shift) & mask);
}

std::uint16_t read_sample(const std::vector<std::uint8_t>& row, std::size_t offset, std::uint8_t bit_depth)
{
    return bit_depth == 16 ? read_be16(row, offset) : row[offset];
}

std::uint8_t sample_to_u8(std::uint16_t sample, std::uint8_t bit_depth)
{
    if (bit_depth == 8) {
        return static_cast<std::uint8_t>(sample);
    }
    if (bit_depth == 16) {
        return static_cast<std::uint8_t>((sample + 128u) / 257u);
    }
    const std::uint16_t max_value = static_cast<std::uint16_t>((1u << bit_depth) - 1u);
    return static_cast<std::uint8_t>((static_cast<unsigned>(sample) * 255u + max_value / 2u) / max_value);
}

} // namespace

bool Png_image::decode_png(const std::vector<std::uint8_t>& file_bytes)
{
    static constexpr std::array<std::uint8_t, 8> signature = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (file_bytes.size() < signature.size()
        || !std::equal(signature.begin(), signature.end(), file_bytes.begin())) {
        m_error = "Not a PNG file";
        return false;
    }

    std::size_t pos = signature.size();
    std::vector<std::uint8_t> idat;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> palette_alpha;
    bool seen_ihdr = false;
    std::uint8_t bit_depth = 0;
    std::uint8_t color_type = 0;
    std::uint16_t transparency_gray = 0;
    std::array<std::uint16_t, 3> transparency_rgb = { 0, 0, 0 };
    bool has_gray_key = false;
    bool has_rgb_key = false;

    while (file_bytes.size() - pos >= 8) {
        const std::uint32_t chunk_len_u32 = read_be32(file_bytes, pos);
        const std::size_t chunk_len = static_cast<std::size_t>(chunk_len_u32);
        const std::string chunk_type(reinterpret_cast<const char*>(&file_bytes[pos + 4]), 4);
        pos += 8;
        // Need chunk_len bytes of payload + 4 bytes of CRC. Done with
        // overflow-safe arithmetic so a malicious chunk_len near SIZE_MAX
        // can't bypass the bound.
        std::size_t chunk_end = 0;
        if (!checked_add(chunk_len, 4, chunk_end)
            || chunk_end > file_bytes.size() - pos)
        {
            m_error = "Corrupt PNG chunk";
            return false;
        }

        const std::uint8_t* chunk_data = &file_bytes[pos];
        if (chunk_type == "IHDR") {
            if (seen_ihdr) {
                m_error = "Duplicate PNG header";
                return false;
            }
            if (chunk_len != 13) {
                m_error = "Invalid PNG header";
                return false;
            }
            const std::uint32_t w = read_be32(file_bytes, pos);
            const std::uint32_t h = read_be32(file_bytes, pos + 4);
            if (w == 0 || h == 0
                || w > k_max_image_dimension || h > k_max_image_dimension)
            {
                m_error = "PNG dimensions out of range";
                return false;
            }
            m_width_px = static_cast<int>(w);
            m_height_px = static_cast<int>(h);
            bit_depth = file_bytes[pos + 8];
            color_type = file_bytes[pos + 9];
            const std::uint8_t compression = file_bytes[pos + 10];
            const std::uint8_t filter = file_bytes[pos + 11];
            const std::uint8_t interlace = file_bytes[pos + 12];
            if (!is_supported_bit_depth(color_type, bit_depth)
                || compression != 0 || filter != 0 || interlace != 0) {
                m_error = "Unsupported PNG encoding";
                return false;
            }
            seen_ihdr = true;
        }
        else
        if (!seen_ihdr) {
            // Spec: IHDR must come first. Reject any non-IHDR ancillary or
            // critical chunk before it.
            m_error = "PNG chunk before IHDR";
            return false;
        }
        else
        if (chunk_type == "PLTE") {
            if (chunk_len == 0 || (chunk_len % 3) != 0) {
                m_error = "Invalid PNG palette";
                return false;
            }
            palette.assign(chunk_data, chunk_data + chunk_len);
        }
        else
        if (chunk_type == "tRNS") {
            if (color_type == 3) {
                palette_alpha.assign(chunk_data, chunk_data + chunk_len);
            }
            else
            if (color_type == 0 && chunk_len == 2) {
                transparency_gray = read_be16(file_bytes, pos);
                has_gray_key = true;
            }
            else
            if (color_type == 2 && chunk_len == 6) {
                transparency_rgb[0] = read_be16(file_bytes, pos);
                transparency_rgb[1] = read_be16(file_bytes, pos + 2);
                transparency_rgb[2] = read_be16(file_bytes, pos + 4);
                has_rgb_key = true;
            }
            else {
                m_error = "Invalid PNG transparency";
                return false;
            }
        }
        else
        if (chunk_type == "IDAT") {
            if (chunk_len > k_max_idat_bytes - idat.size()) {
                m_error = "PNG IDAT exceeds size cap";
                return false;
            }
            idat.insert(idat.end(), chunk_data, chunk_data + chunk_len);
        }
        else
        if (chunk_type == "IEND") {
            break;
        }

        pos += chunk_len + 4;
    }

    if (!seen_ihdr || m_width_px <= 0 || m_height_px <= 0) {
        m_error = "Missing PNG header";
        return false;
    }
    if (color_type == 3 && palette.empty()) {
        m_error = "Missing PNG palette";
        return false;
    }
    if (color_type == 3 && !palette_alpha.empty() && palette_alpha.size() > palette.size() / 3) {
        m_error = "Invalid PNG transparency";
        return false;
    }

    const std::size_t encoded_row_size = encoded_row_bytes(m_width_px, color_type, bit_depth);
    const std::size_t bpp = encoded_bytes_per_pixel(color_type, bit_depth);
    if (encoded_row_size == 0 || bpp == 0) {
        m_error = "Unsupported PNG encoding";
        return false;
    }

    // Each scanline is `encoded_row_size + 1` (one filter byte). The total
    // decoded size and the output pixel/alpha buffers are checked for
    // overflow against size_t and against the per-image cap.
    std::size_t pixel_count = 0;
    std::size_t decoded_size = 0;
    std::size_t row_with_filter = 0;
    if (!checked_mul(static_cast<std::size_t>(m_width_px),
                     static_cast<std::size_t>(m_height_px),
                     pixel_count)
        || pixel_count > k_max_image_pixels
        || !checked_add(encoded_row_size, 1, row_with_filter)
        || !checked_mul(static_cast<std::size_t>(m_height_px),
                        row_with_filter,
                        decoded_size))
    {
        m_error = "PNG too large to decode";
        return false;
    }
    if (decoded_size > std::numeric_limits<mz_ulong>::max()
        || idat.size() > std::numeric_limits<mz_ulong>::max())
    {
        m_error = "PNG too large to decode";
        return false;
    }

    std::vector<std::uint8_t> decoded(decoded_size);
    mz_ulong decoded_len = static_cast<mz_ulong>(decoded.size());
    mz_ulong idat_len = static_cast<mz_ulong>(idat.size());
    if (mz_uncompress2(
            reinterpret_cast<unsigned char*>(decoded.data()),
            &decoded_len,
            reinterpret_cast<const unsigned char*>(idat.data()),
            &idat_len) != Z_OK)
    {
        m_error = "Failed to decompress PNG image data";
        return false;
    }
    decoded.resize(static_cast<std::size_t>(decoded_len));

    const bool use_alpha = color_type == 4 || color_type == 6 || has_gray_key || has_rgb_key || !palette_alpha.empty();
    const int output_components = (color_type == 0 || color_type == 4) ? 1 : 3;

    std::size_t pixel_buffer_size = 0;
    if (!checked_mul(pixel_count, static_cast<std::size_t>(output_components), pixel_buffer_size)) {
        m_error = "PNG too large to decode";
        return false;
    }
    m_pixels.assign(pixel_buffer_size, 0);
    if (use_alpha) {
        m_alpha.assign(pixel_count, 255);
    }
    m_color_components = output_components;

    std::vector<std::uint8_t> prev(encoded_row_size, 0);
    std::vector<std::uint8_t> cur(encoded_row_size, 0);
    std::vector<std::uint8_t> raw(encoded_row_size, 0);

    std::size_t src = 0;
    for (int y = 0; y < m_height_px; ++y) {
        if (src >= decoded.size()) {
            m_error = "Truncated PNG image data";
            return false;
        }
        const std::uint8_t filter = decoded[src++];
        if (src + encoded_row_size > decoded.size()) {
            m_error = "Truncated PNG image row";
            return false;
        }

        std::copy(
            decoded.begin() + static_cast<std::ptrdiff_t>(src),
            decoded.begin() + static_cast<std::ptrdiff_t>(src + encoded_row_size),
            raw.begin());
        src += encoded_row_size;

        for (std::size_t i = 0; i < encoded_row_size; ++i) {
            const std::uint8_t left = i >= bpp ? cur[i - bpp] : 0;
            const std::uint8_t up = prev[i];
            const std::uint8_t up_left = i >= bpp ? prev[i - bpp] : 0;
            switch (filter) {
                case 0: cur[i] = raw[i]; break;
                case 1: cur[i] = static_cast<std::uint8_t>(raw[i] + left); break;
                case 2: cur[i] = static_cast<std::uint8_t>(raw[i] + up); break;
                case 3: cur[i] = static_cast<std::uint8_t>(raw[i] + ((static_cast<unsigned>(left) + static_cast<unsigned>(up)) / 2)); break;
                case 4: cur[i] = static_cast<std::uint8_t>(raw[i] + paeth_predictor(left, up, up_left)); break;
                default:
                    m_error = "Unsupported PNG filter";
                    return false;
            }
        }

        const std::size_t pixel_row_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width_px);
        const std::size_t output_row_offset = pixel_row_offset * static_cast<std::size_t>(output_components);

        auto write_alpha = [&](int x, std::uint8_t value) {
            if (!m_alpha.empty()) {
                m_alpha[pixel_row_offset + static_cast<std::size_t>(x)] = value;
            }
        };

        // Stride between same-color samples within one pixel (in bytes).
        // For sub-byte bit depths the per-pixel stride is meaningless: we
        // always read those via packed_sample(), and color_type 3 is the
        // only color_type that allows them.
        const std::size_t step = bit_depth == 16 ? 2u : 1u;
        const int channels = encoded_components(color_type);
        const std::size_t pixel_stride = static_cast<std::size_t>(channels) * step;

        if (color_type == 0) {
            for (int x = 0; x < m_width_px; ++x) {
                const std::size_t xz = static_cast<std::size_t>(x);
                const std::uint16_t sample = bit_depth < 8
                    ? packed_sample(cur, xz, bit_depth)
                    : read_sample(cur, xz * step, bit_depth);
                m_pixels[pixel_row_offset + xz] = sample_to_u8(sample, bit_depth);
                if (has_gray_key) {
                    write_alpha(x, sample == transparency_gray ? 0 : 255);
                }
            }
        }
        else
        if (color_type == 2) {
            for (int x = 0; x < m_width_px; ++x) {
                const std::size_t src_offset = static_cast<std::size_t>(x) * pixel_stride;
                const std::uint16_t r = read_sample(cur, src_offset,            bit_depth);
                const std::uint16_t g = read_sample(cur, src_offset + step,     bit_depth);
                const std::uint16_t b = read_sample(cur, src_offset + 2 * step, bit_depth);
                const std::size_t dst = output_row_offset + static_cast<std::size_t>(x) * 3u;
                m_pixels[dst    ] = sample_to_u8(r, bit_depth);
                m_pixels[dst + 1] = sample_to_u8(g, bit_depth);
                m_pixels[dst + 2] = sample_to_u8(b, bit_depth);
                if (has_rgb_key) {
                    write_alpha(x, (r == transparency_rgb[0] && g == transparency_rgb[1] && b == transparency_rgb[2]) ? 0 : 255);
                }
            }
        }
        else
        if (color_type == 3) {
            for (int x = 0; x < m_width_px; ++x) {
                const std::size_t xz = static_cast<std::size_t>(x);
                const std::uint16_t index = bit_depth < 8
                    ? packed_sample(cur, xz, bit_depth)
                    : cur[xz];
                const std::size_t palette_offset = static_cast<std::size_t>(index) * 3u;
                if (palette_offset + 2 >= palette.size()) {
                    m_error = "Invalid PNG palette index";
                    return false;
                }
                const std::size_t dst = output_row_offset + xz * 3u;
                m_pixels[dst    ] = palette[palette_offset    ];
                m_pixels[dst + 1] = palette[palette_offset + 1];
                m_pixels[dst + 2] = palette[palette_offset + 2];
                if (!palette_alpha.empty()) {
                    const std::uint8_t alpha = index < palette_alpha.size() ? palette_alpha[index] : 255;
                    write_alpha(x, alpha);
                }
            }
        }
        else
        if (color_type == 4) {
            for (int x = 0; x < m_width_px; ++x) {
                const std::size_t src_offset = static_cast<std::size_t>(x) * pixel_stride;
                const std::uint16_t gray  = read_sample(cur, src_offset,        bit_depth);
                const std::uint16_t alpha = read_sample(cur, src_offset + step, bit_depth);
                m_pixels[pixel_row_offset + static_cast<std::size_t>(x)] = sample_to_u8(gray, bit_depth);
                write_alpha(x, sample_to_u8(alpha, bit_depth));
            }
        }
        else
        if (color_type == 6) {
            for (int x = 0; x < m_width_px; ++x) {
                const std::size_t src_offset = static_cast<std::size_t>(x) * pixel_stride;
                const std::uint16_t r = read_sample(cur, src_offset,            bit_depth);
                const std::uint16_t g = read_sample(cur, src_offset + step,     bit_depth);
                const std::uint16_t b = read_sample(cur, src_offset + 2 * step, bit_depth);
                const std::uint16_t a = read_sample(cur, src_offset + 3 * step, bit_depth);
                const std::size_t dst = output_row_offset + static_cast<std::size_t>(x) * 3u;
                m_pixels[dst    ] = sample_to_u8(r, bit_depth);
                m_pixels[dst + 1] = sample_to_u8(g, bit_depth);
                m_pixels[dst + 2] = sample_to_u8(b, bit_depth);
                write_alpha(x, sample_to_u8(a, bit_depth));
            }
        }
        else {
            m_error = "Unsupported PNG color type";
            return false;
        }

        prev.swap(cur);
    }

    m_loaded = true;
    return true;
}

bool Png_image::load_from_file(const fs::path& path)
{
    m_loaded = false;
    m_error.clear();
    m_width_px = 0;
    m_height_px = 0;
    m_color_components = 0;
    m_pixels.clear();
    m_alpha.clear();

    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(path, bytes)) {
        m_error = "Failed to read PNG file";
        return false;
    }
    return decode_png(bytes);
}

} // namespace mark2haru
