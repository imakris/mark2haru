#include <mark2haru/ttf_font.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace mark2haru {
namespace {

namespace fs = std::filesystem;

bool read_file_bytes(const fs::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

} // namespace

std::uint16_t True_type_font::read_u16(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::int16_t True_type_font::read_i16(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return static_cast<std::int16_t>(read_u16(data, offset));
}

std::uint32_t True_type_font::read_u32(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return (static_cast<std::uint32_t>(data[offset    ]) << 24)
        | (static_cast<std::uint32_t>( data[offset + 1]) << 16)
        | (static_cast<std::uint32_t>( data[offset + 2]) <<  8)
        | static_cast<std::uint32_t>(  data[offset + 3]);
}

bool True_type_font::load_from_file(const fs::path& path)
{
    m_source_path = path;
    if (!read_file_bytes(path, m_file_bytes)) {
        m_file_bytes.clear();
        return false;
    }

    if (m_file_bytes.size() < 12) {
        m_file_bytes.clear();
        return false;
    }

    const std::uint16_t num_tables = read_u16(m_file_bytes, 4);
    std::uint32_t table_dir = 12;
    std::unordered_map<std::string, table_record_t> tables;
    for (std::uint16_t i = 0; i < num_tables; ++i, table_dir += 16) {
        if (table_dir + 16 > m_file_bytes.size()) {
            m_file_bytes.clear();
            return false;
        }
        std::string tag(reinterpret_cast<const char*>(&m_file_bytes[table_dir]), 4);
        tables[tag] = {
            read_u32(m_file_bytes, table_dir + 8),
            read_u32(m_file_bytes, table_dir + 12),
        };
    }

    auto get = [&](const char* tag) -> const table_record_t* {
        auto it = tables.find(std::string(tag, 4));
        return it == tables.end() ? nullptr : &it->second;
    };

    const auto* head = get("head");
    const auto* hhea = get("hhea");
    const auto* maxp = get("maxp");
    const auto* hmtx = get("hmtx");
    const auto* cmap = get("cmap");
    const auto* post = get("post");
    const auto* os2 = get("OS/2");
    if (!head || !hhea || !maxp || !hmtx || !cmap) {
        m_file_bytes.clear();
        return false;
    }

    // Minimum tail we touch in each table. We only dereference the fields
    // listed below, so the per-table bound is the last byte we read + 1.
    //   head: units_per_em @ 18 (u16), bbox @ 36..43 (4*i16), magic @ 12 (u32)
    //   hhea: ascent @ 4, descent @ 6, line_gap @ 8, num_hmetrics @ 34
    //   maxp: num_glyphs @ 4 (u16)
    //   cmap: num_sub_tables @ 2 (u16); each record is 8 bytes starting at 4
    const auto in_bounds = [this](const table_record_t* rec, std::uint32_t need) {
        return rec->offset + need <= m_file_bytes.size();
    };
    if (!in_bounds(head, 54) || !in_bounds(hhea, 36) || !in_bounds(maxp, 6)
        || !in_bounds(cmap, 4)) {
        m_file_bytes.clear();
        return false;
    }
    if (read_u32(m_file_bytes, head->offset + 12) != 0x5F0F3CF5u) {
        m_file_bytes.clear();
        return false;
    }

    m_units_per_em = read_u16(m_file_bytes, head->offset + 18);
    m_x_min        = read_i16(m_file_bytes, head->offset + 36);
    m_y_min        = read_i16(m_file_bytes, head->offset + 38);
    m_x_max        = read_i16(m_file_bytes, head->offset + 40);
    m_y_max        = read_i16(m_file_bytes, head->offset + 42);
    m_ascent       = read_i16(m_file_bytes, hhea->offset +  4);
    m_descent      = read_i16(m_file_bytes, hhea->offset +  6);
    m_line_gap     = read_i16(m_file_bytes, hhea->offset +  8);
    m_num_hmetrics = read_u16(m_file_bytes, hhea->offset + 34);
    m_num_glyphs   = read_u16(m_file_bytes, maxp->offset +  4);

    if (post && post->offset + 16 <= m_file_bytes.size()) {
        // italicAngle: Fixed (16.16) signed
        const std::int16_t angle_int = read_i16(m_file_bytes, post->offset + 4);
        const std::uint16_t angle_frac = read_u16(m_file_bytes, post->offset + 6);
        m_italic_angle = static_cast<double>(angle_int)
            + static_cast<double>(angle_frac) / 65536.0;
        // isFixedPitch: uint32, nonzero means monospaced
        const std::uint32_t fixed_pitch = read_u32(m_file_bytes, post->offset + 12);
        m_is_fixed_pitch = fixed_pitch != 0;
    }

    // OS/2 table gives sCapHeight from version 2 onward (offset 88).
    // For earlier versions we fall back to ascent at the caller site.
    if (os2 && os2->offset + 90 <= m_file_bytes.size()) {
        const std::uint16_t version = read_u16(m_file_bytes, os2->offset + 0);
        if (version >= 2) {
            m_cap_height = read_i16(m_file_bytes, os2->offset + 88);
        }
    }

    m_advance_widths.assign(m_num_glyphs, 0);
    std::uint32_t pos = hmtx->offset;
    std::uint16_t last_advance = 0;
    for (std::uint16_t gid = 0; gid < m_num_glyphs; ++gid) {
        if (gid < m_num_hmetrics) {
            if (pos + 4 > m_file_bytes.size()) {
                m_file_bytes.clear();
                return false;
            }
            last_advance = read_u16(m_file_bytes, pos);
            m_advance_widths[gid] = last_advance;
            pos += 4;
        }
        else {
            m_advance_widths[gid] = last_advance;
        }
    }

    m_cmap4.offset = 0;
    m_cmap12.offset = 0;
    const std::uint16_t sub_count = read_u16(m_file_bytes, cmap->offset + 2);
    std::uint32_t subtable = cmap->offset + 4;
    for (std::uint16_t i = 0; i < sub_count; ++i, subtable += 8) {
        if (subtable + 8 > m_file_bytes.size()) {
            m_file_bytes.clear();
            return false;
        }
        const std::uint16_t platform_id = read_u16(m_file_bytes, subtable);
        const std::uint32_t offset = read_u32(m_file_bytes, subtable + 4);
        const std::uint32_t table = cmap->offset + offset;
        if (table + 2 > m_file_bytes.size()) {
            continue;
        }
        const std::uint16_t format = read_u16(m_file_bytes, table);
        if (format == 12 && m_cmap12.offset == 0) {
            if (platform_id == 0 || platform_id == 3) {
                m_cmap12.offset = table;
            }
        }
        else
        if (format == 4 && m_cmap4.offset == 0) {
            if (platform_id == 0 || platform_id == 3) {
                m_cmap4.offset = table;
            }
        }
    }

    if (m_cmap12.offset == 0 && m_cmap4.offset == 0) {
        m_file_bytes.clear();
        return false;
    }

    m_glyph_cache.clear();
    return true;
}

std::uint16_t True_type_font::lookup_cmap12(std::uint32_t codepoint) const
{
    if (m_cmap12.offset == 0) {
        return 0;
    }
    if (m_cmap12.offset + 16 > m_file_bytes.size()) {
        return 0;
    }
    const std::uint32_t n_groups = read_u32(m_file_bytes, m_cmap12.offset + 12);
    std::uint32_t group = m_cmap12.offset + 16;
    for (std::uint32_t i = 0; i < n_groups; ++i, group += 12) {
        if (group + 12 > m_file_bytes.size()) {
            break;
        }
        const std::uint32_t start_char = read_u32(m_file_bytes, group);
        const std::uint32_t end_char = read_u32(m_file_bytes, group + 4);
        const std::uint32_t start_gid = read_u32(m_file_bytes, group + 8);
        if (codepoint < start_char) {
            break;
        }
        if (codepoint <= end_char) {
            return static_cast<std::uint16_t>(start_gid + (codepoint - start_char));
        }
    }
    return 0;
}

std::uint16_t True_type_font::lookup_cmap4(std::uint32_t codepoint) const
{
    if (m_cmap4.offset == 0 || codepoint > 0xFFFF) {
        return 0;
    }
    if (m_cmap4.offset + 16 > m_file_bytes.size()) {
        return 0;
    }
    const std::uint16_t seg_count_x2 = read_u16(m_file_bytes, m_cmap4.offset + 6);
    const std::uint16_t seg_count = seg_count_x2 / 2;
    const std::uint32_t end_codes = m_cmap4.offset + 14;
    const std::uint32_t start_codes = end_codes + seg_count * 2 + 2;
    const std::uint32_t id_deltas = start_codes + seg_count * 2;
    const std::uint32_t id_range_offsets = id_deltas + seg_count * 2;
    if (id_range_offsets + static_cast<std::uint32_t>(seg_count) * 2 > m_file_bytes.size()) {
        return 0;
    }
    for (std::uint16_t i = 0; i < seg_count; ++i) {
        const std::uint32_t end_code = read_u16(m_file_bytes, end_codes + i * 2);
        const std::uint32_t start_code = read_u16(m_file_bytes, start_codes + i * 2);
        const std::uint16_t delta = read_u16(m_file_bytes, id_deltas + i * 2);
        const std::uint16_t range_offset = read_u16(m_file_bytes, id_range_offsets + i * 2);

        if (codepoint < start_code) {
            continue;
        }
        if (codepoint > end_code) {
            continue;
        }
        if (range_offset == 0) {
            return static_cast<std::uint16_t>((codepoint + delta) & 0xFFFF);
        }

        const std::uint32_t glyph_offset = id_range_offsets + i * 2
            + range_offset + (codepoint - start_code) * 2;
        if (glyph_offset + 2 > m_file_bytes.size()) {
            return 0;
        }
        const std::uint16_t glyph = read_u16(m_file_bytes, glyph_offset);
        if (glyph == 0) {
            return 0;
        }
        return static_cast<std::uint16_t>((glyph + delta) & 0xFFFF);
    }
    return 0;
}

std::uint16_t True_type_font::glyph_for_codepoint(std::uint32_t codepoint) const
{
    auto it = m_glyph_cache.find(codepoint);
    if (it != m_glyph_cache.end()) {
        return it->second;
    }

    std::uint16_t gid = lookup_cmap12(codepoint);
    if (gid == 0) {
        gid = lookup_cmap4(codepoint);
    }
    if (m_glyph_cache.size() >= 4096) {
        m_glyph_cache.clear();
    }
    m_glyph_cache[codepoint] = gid;
    return gid;
}

std::uint16_t True_type_font::advance_width_for_gid(std::uint16_t gid) const
{
    if (m_advance_widths.empty()) {
        return 0;
    }
    if (gid < m_advance_widths.size()) {
        return m_advance_widths[gid];
    }
    return m_advance_widths.back();
}

double True_type_font::advance_width_pt(std::uint32_t codepoint, double size_pt) const
{
    const std::uint16_t gid = glyph_for_codepoint(codepoint);
    const std::uint16_t adv = advance_width_for_gid(gid);
    if (m_units_per_em == 0) {
        return 0.0;
    }
    return static_cast<double>(adv) * size_pt / static_cast<double>(m_units_per_em);
}

} // namespace mark2haru
