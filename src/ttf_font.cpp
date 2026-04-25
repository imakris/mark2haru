#include <mark2haru/ttf_font.h>

#include "utf8_decode.h"

#include <algorithm>
#include <cstring>

namespace mark2haru {

namespace {

namespace fs = std::filesystem;

// Bounds-checking view over a byte buffer. Every read uses overflow-safe
// `has(off, n)` and returns 0 on out-of-range, so a callers' explicit
// bounds check upstream is defense in depth, not a correctness requirement.
struct Bytes_view
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    bool has(std::size_t off, std::size_t n) const
    {
        return off <= size && size - off >= n;
    }

    std::uint16_t u16(std::size_t off) const
    {
        if (!has(off, 2)) {
            return 0;
        }
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[off    ]) << 8) |
             static_cast<std::uint16_t>(data[off + 1]));
    }

    std::int16_t i16(std::size_t off) const
    {
        return static_cast<std::int16_t>(u16(off));
    }

    std::uint32_t u32(std::size_t off) const
    {
        if (!has(off, 4)) {
            return 0;
        }
        return  (static_cast<std::uint32_t>(data[off    ]) << 24)
              | (static_cast<std::uint32_t>(data[off + 1]) << 16)
              | (static_cast<std::uint32_t>(data[off + 2]) <<  8)
              |  static_cast<std::uint32_t>(data[off + 3]);
    }

    bool tag_equals(std::size_t off, const char tag[4]) const
    {
        return has(off, 4) && std::memcmp(data + off, tag, 4) == 0;
    }
};

} // namespace

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

    const Bytes_view bv{ m_file_bytes.data(), m_file_bytes.size() };

    const std::uint16_t num_tables = bv.u16(4);
    std::unordered_map<std::string, table_record_t> tables;
    for (std::uint16_t i = 0; i < num_tables; ++i) {
        const std::size_t entry = static_cast<std::size_t>(12) + static_cast<std::size_t>(i) * 16;
        if (!bv.has(entry, 16)) {
            m_file_bytes.clear();
            return false;
        }
        std::string tag(reinterpret_cast<const char*>(bv.data + entry), 4);
        tables[tag] = {
            static_cast<std::size_t>(bv.u32(entry +  8)),
            static_cast<std::size_t>(bv.u32(entry + 12)),
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
    const auto* os2  = get("OS/2");
    if (!head || !hhea || !maxp || !hmtx || !cmap) {
        m_file_bytes.clear();
        return false;
    }

    // Verifies a `[relative_offset, relative_offset+size)` window fits both
    // inside the table region declared by `rec` and inside the file.
    // Overflow-safe in both bv.size() (size_t) and rec offsets (size_t).
    const auto field_in_table = [&bv](
        const table_record_t* rec,
        std::size_t           relative_offset,
        std::size_t           size)
    {
        if (relative_offset > rec->length) {
            return false;
        }
        if (size > rec->length - relative_offset) {
            return false;
        }
        return bv.has(rec->offset + relative_offset, size);
    };
    if (!field_in_table(head, 12, 4) ||
        !field_in_table(head, 18, 2) ||
        !field_in_table(head, 36, 8) ||
        !field_in_table(hhea,  4, 6) ||
        !field_in_table(hhea, 34, 2) ||
        !field_in_table(maxp,  4, 2) ||
        !field_in_table(cmap,  2, 2))
    {
        m_file_bytes.clear();
        return false;
    }
    // Also verify the table records themselves fit; field_in_table only
    // checks specific fields, but we want the full hmtx/cmap windows to
    // be valid because we'll iterate across them.
    if (!bv.has(hmtx->offset, hmtx->length) ||
        !bv.has(cmap->offset, cmap->length))
    {
        m_file_bytes.clear();
        return false;
    }
    if (bv.u32(head->offset + 12) != 0x5F0F3CF5u) {
        m_file_bytes.clear();
        return false;
    }

    m_units_per_em = bv.u16(head->offset + 18);
    m_x_min        = bv.i16(head->offset + 36);
    m_y_min        = bv.i16(head->offset + 38);
    m_x_max        = bv.i16(head->offset + 40);
    m_y_max        = bv.i16(head->offset + 42);
    m_ascent       = bv.i16(hhea->offset +  4);
    m_descent      = bv.i16(hhea->offset +  6);
    m_line_gap     = bv.i16(hhea->offset +  8);
    m_num_hmetrics = bv.u16(hhea->offset + 34);
    m_num_glyphs   = bv.u16(maxp->offset +  4);

    if (post &&
        field_in_table(post, 4, 4) &&
        field_in_table(post, 12, 4))
    {
        // italicAngle: Fixed (16.16) signed
        const std::int16_t  angle_int  = bv.i16(post->offset + 4);
        const std::uint16_t angle_frac = bv.u16(post->offset + 6);
        m_italic_angle = static_cast<double>(angle_int)
            + static_cast<double>(angle_frac) / 65536.0;
        // isFixedPitch: uint32, nonzero means monospaced
        m_is_fixed_pitch = bv.u32(post->offset + 12) != 0;
    }

    // OS/2 table gives sCapHeight from version 2 onward (offset 88).
    // For earlier versions we fall back to ascent at the caller site.
    if (os2 &&
        field_in_table(os2, 0, 2) &&
        field_in_table(os2, 88, 2))
    {
        if (bv.u16(os2->offset) >= 2) {
            m_cap_height = bv.i16(os2->offset + 88);
        }
    }

    m_advance_widths.assign(m_num_glyphs, 0);
    std::size_t pos = hmtx->offset;
    const std::size_t hmtx_end = hmtx->offset + hmtx->length;
    std::uint16_t last_advance = 0;
    for (std::uint16_t gid = 0; gid < m_num_glyphs; ++gid) {
        if (gid < m_num_hmetrics) {
            if (pos > hmtx_end || hmtx_end - pos < 4) {
                m_file_bytes.clear();
                return false;
            }
            last_advance = bv.u16(pos);
            m_advance_widths[gid] = last_advance;
            pos += 4;
        }
        else {
            m_advance_widths[gid] = last_advance;
        }
    }

    m_cmap4.offset = 0;
    m_cmap12.offset = 0;
    if (!field_in_table(cmap, 2, 2)) {
        m_file_bytes.clear();
        return false;
    }
    const std::uint16_t sub_count = bv.u16(cmap->offset + 2);
    const std::size_t cmap_end = cmap->offset + cmap->length;
    for (std::uint16_t i = 0; i < sub_count; ++i) {
        const std::size_t subtable = cmap->offset + 4 + static_cast<std::size_t>(i) * 8;
        if (subtable > cmap_end || cmap_end - subtable < 8) {
            m_file_bytes.clear();
            return false;
        }
        const std::uint16_t platform_id = bv.u16(subtable);
        const std::uint32_t rel_offset = bv.u32(subtable + 4);
        const std::size_t table = cmap->offset + static_cast<std::size_t>(rel_offset);
        if (table > cmap_end || cmap_end - table < 2) {
            continue;
        }
        const std::uint16_t format = bv.u16(table);
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
    const Bytes_view bv{ m_file_bytes.data(), m_file_bytes.size() };
    if (!bv.has(m_cmap12.offset, 16)) {
        return 0;
    }
    const std::uint32_t n_groups = bv.u32(m_cmap12.offset + 12);
    for (std::uint32_t i = 0; i < n_groups; ++i) {
        const std::size_t group = m_cmap12.offset + 16 + static_cast<std::size_t>(i) * 12;
        if (!bv.has(group, 12)) {
            break;
        }
        const std::uint32_t start_char = bv.u32(group);
        const std::uint32_t end_char   = bv.u32(group + 4);
        const std::uint32_t start_gid  = bv.u32(group + 8);
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
    const Bytes_view bv{ m_file_bytes.data(), m_file_bytes.size() };
    if (!bv.has(m_cmap4.offset, 16)) {
        return 0;
    }
    const std::uint16_t seg_count_x2 = bv.u16(m_cmap4.offset + 6);
    const std::uint16_t seg_count = seg_count_x2 / 2;

    const std::size_t end_codes        = m_cmap4.offset + 14;
    const std::size_t start_codes      = end_codes  + static_cast<std::size_t>(seg_count) * 2 + 2;
    const std::size_t id_deltas        = start_codes + static_cast<std::size_t>(seg_count) * 2;
    const std::size_t id_range_offsets = id_deltas  + static_cast<std::size_t>(seg_count) * 2;
    if (!bv.has(id_range_offsets, static_cast<std::size_t>(seg_count) * 2)) {
        return 0;
    }
    for (std::uint16_t i = 0; i < seg_count; ++i) {
        const std::uint32_t end_code   = bv.u16(end_codes        + static_cast<std::size_t>(i) * 2);
        const std::uint32_t start_code = bv.u16(start_codes      + static_cast<std::size_t>(i) * 2);
        const std::uint16_t delta      = bv.u16(id_deltas        + static_cast<std::size_t>(i) * 2);
        const std::uint16_t range_off  = bv.u16(id_range_offsets + static_cast<std::size_t>(i) * 2);

        if (codepoint < start_code || codepoint > end_code) {
            continue;
        }
        if (range_off == 0) {
            return static_cast<std::uint16_t>((codepoint + delta) & 0xFFFF);
        }
        const std::size_t glyph_offset = id_range_offsets
            + static_cast<std::size_t>(i) * 2
            + static_cast<std::size_t>(range_off)
            + static_cast<std::size_t>(codepoint - start_code) * 2;
        if (!bv.has(glyph_offset, 2)) {
            return 0;
        }
        const std::uint16_t glyph = bv.u16(glyph_offset);
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

} // namespace mark2haru
