#include "ttf_font.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace mark2haru {
namespace {

struct FontHeader {
    std::uint16_t num_tables = 0;
    std::uint16_t search_range = 0;
    std::uint16_t entry_selector = 0;
    std::uint16_t range_shift = 0;
};

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

} // namespace

std::uint16_t TrueTypeFont::read_u16(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::int16_t TrueTypeFont::read_i16(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return static_cast<std::int16_t>(read_u16(data, offset));
}

std::uint32_t TrueTypeFont::read_u32(const std::vector<std::uint8_t>& data, std::uint32_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24)
        | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
        | static_cast<std::uint32_t>(data[offset + 3]);
}

bool TrueTypeFont::load_from_file(const std::filesystem::path& path)
{
    source_path_ = path;
    if (!read_file_bytes(path, file_bytes_)) {
        file_bytes_.clear();
        return false;
    }

    if (file_bytes_.size() < 12) {
        file_bytes_.clear();
        return false;
    }

    const std::uint16_t num_tables = read_u16(file_bytes_, 4);
    std::uint32_t table_dir = 12;
    std::unordered_map<std::string, TableRecord> tables;
    for (std::uint16_t i = 0; i < num_tables; ++i, table_dir += 16) {
        if (table_dir + 16 > file_bytes_.size()) {
            file_bytes_.clear();
            return false;
        }
        std::string tag(reinterpret_cast<const char*>(&file_bytes_[table_dir]), 4);
        tables[tag] = {
            read_u32(file_bytes_, table_dir + 8),
            read_u32(file_bytes_, table_dir + 12),
        };
    }

    auto get = [&](const char* tag) -> const TableRecord* {
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
        file_bytes_.clear();
        return false;
    }

    // Validate the 'head' magic number (0x5F0F3CF5) before trusting any of
    // its fields. A well-formed head is at least 54 bytes; we only read
    // through offset 42, but the magic check is the cheapest way to reject
    // garbage that happens to have a valid-looking table directory.
    if (head->offset + 54 > file_bytes_.size()) {
        file_bytes_.clear();
        return false;
    }
    if (read_u32(file_bytes_, head->offset + 12) != 0x5F0F3CF5u) {
        file_bytes_.clear();
        return false;
    }

    units_per_em_ = read_u16(file_bytes_, head->offset + 18);
    x_min_ = read_i16(file_bytes_, head->offset + 36);
    y_min_ = read_i16(file_bytes_, head->offset + 38);
    x_max_ = read_i16(file_bytes_, head->offset + 40);
    y_max_ = read_i16(file_bytes_, head->offset + 42);
    ascent_ = read_i16(file_bytes_, hhea->offset + 4);
    descent_ = read_i16(file_bytes_, hhea->offset + 6);
    line_gap_ = read_i16(file_bytes_, hhea->offset + 8);
    num_hmetrics_ = read_u16(file_bytes_, hhea->offset + 34);
    num_glyphs_ = read_u16(file_bytes_, maxp->offset + 4);

    if (post && post->offset + 16 <= file_bytes_.size()) {
        // italicAngle: Fixed (16.16) signed. Reading the integer half as
        // signed and the fraction half as unsigned, then summing, is the
        // correct decoding under two's complement: for -11.25 the raw bytes
        // are 0xFFF4_C000 which yields int16=-12 and frac=0.75, summing to
        // -11.25. This looks surprising but is faithful to the format.
        const std::int16_t angle_int = read_i16(file_bytes_, post->offset + 4);
        const std::uint16_t angle_frac = read_u16(file_bytes_, post->offset + 6);
        italic_angle_ = static_cast<double>(angle_int)
            + static_cast<double>(angle_frac) / 65536.0;
        // isFixedPitch: uint32, nonzero means monospaced
        const std::uint32_t fixed_pitch = read_u32(file_bytes_, post->offset + 12);
        is_fixed_pitch_ = fixed_pitch != 0;
    }

    // OS/2 table gives sCapHeight from version 2 onward (offset 88).
    // For earlier versions we fall back to ascent at the caller site.
    if (os2 && os2->offset + 90 <= file_bytes_.size()) {
        const std::uint16_t version = read_u16(file_bytes_, os2->offset + 0);
        if (version >= 2) {
            cap_height_ = read_i16(file_bytes_, os2->offset + 88);
        }
    }

    advance_widths_.assign(num_glyphs_, 0);
    std::uint32_t pos = hmtx->offset;
    std::uint16_t last_advance = 0;
    for (std::uint16_t gid = 0; gid < num_glyphs_; ++gid) {
        if (gid < num_hmetrics_) {
            if (pos + 4 > file_bytes_.size()) {
                file_bytes_.clear();
                return false;
            }
            last_advance = read_u16(file_bytes_, pos);
            advance_widths_[gid] = last_advance;
            pos += 4;
        } else {
            advance_widths_[gid] = last_advance;
        }
    }

    cmap4_.offset = 0;
    cmap12_.offset = 0;
    const std::uint16_t sub_count = read_u16(file_bytes_, cmap->offset + 2);
    std::uint32_t subtable = cmap->offset + 4;
    for (std::uint16_t i = 0; i < sub_count; ++i, subtable += 8) {
        if (subtable + 8 > file_bytes_.size()) {
            file_bytes_.clear();
            return false;
        }
        const std::uint16_t platform_id = read_u16(file_bytes_, subtable);
        const std::uint32_t offset = read_u32(file_bytes_, subtable + 4);
        const std::uint32_t table = cmap->offset + offset;
        if (table + 2 > file_bytes_.size()) {
            continue;
        }
        const std::uint16_t format = read_u16(file_bytes_, table);
        if (format == 12 && cmap12_.offset == 0) {
            if (platform_id == 0 || platform_id == 3) {
                cmap12_.offset = table;
            }
        } else if (format == 4 && cmap4_.offset == 0) {
            if (platform_id == 0 || platform_id == 3) {
                cmap4_.offset = table;
            }
        }
    }

    if (cmap12_.offset == 0 && cmap4_.offset == 0) {
        file_bytes_.clear();
        return false;
    }

    glyph_cache_.clear();
    return true;
}

std::uint16_t TrueTypeFont::lookup_cmap12(std::uint32_t codepoint) const
{
    if (cmap12_.offset == 0) {
        return 0;
    }
    if (cmap12_.offset + 16 > file_bytes_.size()) {
        return 0;
    }
    const std::uint32_t n_groups = read_u32(file_bytes_, cmap12_.offset + 12);
    std::uint32_t group = cmap12_.offset + 16;
    for (std::uint32_t i = 0; i < n_groups; ++i, group += 12) {
        if (group + 12 > file_bytes_.size()) {
            break;
        }
        const std::uint32_t start_char = read_u32(file_bytes_, group);
        const std::uint32_t end_char = read_u32(file_bytes_, group + 4);
        const std::uint32_t start_gid = read_u32(file_bytes_, group + 8);
        if (codepoint < start_char) {
            break;
        }
        if (codepoint <= end_char) {
            return static_cast<std::uint16_t>(start_gid + (codepoint - start_char));
        }
    }
    return 0;
}

std::uint16_t TrueTypeFont::lookup_cmap4(std::uint32_t codepoint) const
{
    if (cmap4_.offset == 0 || codepoint > 0xFFFF) {
        return 0;
    }
    if (cmap4_.offset + 16 > file_bytes_.size()) {
        return 0;
    }
    const std::uint16_t seg_count_x2 = read_u16(file_bytes_, cmap4_.offset + 6);
    const std::uint16_t seg_count = seg_count_x2 / 2;
    const std::uint32_t end_codes = cmap4_.offset + 14;
    const std::uint32_t start_codes = end_codes + seg_count * 2 + 2;
    const std::uint32_t id_deltas = start_codes + seg_count * 2;
    const std::uint32_t id_range_offsets = id_deltas + seg_count * 2;
    // Bounds check the four parallel arrays before iterating. A malformed
    // cmap4 subtable with a bogus seg_count could otherwise walk past the
    // end of the font buffer while reading id_range_offsets.
    if (id_range_offsets + static_cast<std::uint32_t>(seg_count) * 2 > file_bytes_.size()) {
        return 0;
    }
    // NOTE: Linear scan is intentional — a brief-sized document touches at
    // most a few hundred code points, and DejaVu Sans's cmap4 has ~150
    // segments. A binary search would be faster for large scripts but adds
    // code nobody exercises; revisit if profiling shows otherwise.
    for (std::uint16_t i = 0; i < seg_count; ++i) {
        const std::uint32_t end_code = read_u16(file_bytes_, end_codes + i * 2);
        const std::uint32_t start_code = read_u16(file_bytes_, start_codes + i * 2);
        const std::uint16_t delta = read_u16(file_bytes_, id_deltas + i * 2);
        const std::uint16_t range_offset = read_u16(file_bytes_, id_range_offsets + i * 2);

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
        if (glyph_offset + 2 > file_bytes_.size()) {
            return 0;
        }
        const std::uint16_t glyph = read_u16(file_bytes_, glyph_offset);
        if (glyph == 0) {
            return 0;
        }
        return static_cast<std::uint16_t>((glyph + delta) & 0xFFFF);
    }
    return 0;
}

std::uint16_t TrueTypeFont::glyph_for_codepoint(std::uint32_t codepoint) const
{
    auto it = glyph_cache_.find(codepoint);
    if (it != glyph_cache_.end()) {
        return it->second;
    }

    std::uint16_t gid = lookup_cmap12(codepoint);
    if (gid == 0) {
        gid = lookup_cmap4(codepoint);
    }
    // Cap the cache so pathological input (e.g. an entire Unicode sweep)
    // cannot grow it without bound. Clearing wholesale is acceptable because
    // every entry is recomputed on demand and lookups stay O(seg_count).
    if (glyph_cache_.size() >= 4096) {
        glyph_cache_.clear();
    }
    glyph_cache_[codepoint] = gid;
    return gid;
}

std::uint16_t TrueTypeFont::advance_width_for_gid(std::uint16_t gid) const
{
    if (advance_widths_.empty()) {
        return 0;
    }
    if (gid < advance_widths_.size()) {
        return advance_widths_[gid];
    }
    return advance_widths_.back();
}

double TrueTypeFont::advance_width_pt(std::uint32_t codepoint, double size_pt) const
{
    // NOTE: advance widths come from hmtx only. GPOS kerning and the legacy
    // 'kern' table are intentionally not consulted — briefs look fine without
    // kerning, and the parser is deliberately kept small. If you need tight
    // typographic fidelity, this is the first place to extend.
    const std::uint16_t gid = glyph_for_codepoint(codepoint);
    const std::uint16_t adv = advance_width_for_gid(gid);
    if (units_per_em_ == 0) {
        return 0.0;
    }
    return static_cast<double>(adv) * size_pt / static_cast<double>(units_per_em_);
}

} // namespace mark2haru
