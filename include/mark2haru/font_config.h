#pragma once

#include <filesystem>

namespace mark2haru
{

struct Font_source
{
    std::filesystem::path  path;

    static Font_source from_path(const std::filesystem::path& path)
    {
        Font_source out;
        out.path = path;
        return out;
    }

    bool empty() const
    {
        return path.empty();
    }
};

struct Font_family_config
{
    Font_source    regular;
    Font_source    bold;
    Font_source    italic;
    Font_source    bold_italic;
    Font_source    mono;

    static Font_family_config bundled_default();
};

} // namespace mark2haru
