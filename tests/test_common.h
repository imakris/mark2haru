#pragma once

#include <mark2haru/font_context.h>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace mark2haru_test {

// Resolves the directory containing the current executable, which the
// bundled-font loader walks up from to find a `fonts/` directory. Falls back
// to the current working directory when argv[0] is missing.
inline std::filesystem::path executable_dir(int argc, char** argv)
{
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return std::filesystem::current_path();
    }
    return std::filesystem::absolute(argv[0]).parent_path();
}

// Loads the briefutil default font family rooted at `exe_dir`. Returns
// nullptr on failure after writing a diagnostic to stderr; the caller picks
// the exit code.
inline std::shared_ptr<mark2haru::Measurement_context> load_default_metrics(
    const std::filesystem::path& exe_dir)
{
    auto metrics = std::make_shared<mark2haru::Measurement_context>(
        mark2haru::Font_family_config::briefutil_default(),
        exe_dir);
    if (!metrics->loaded()) {
        std::fprintf(stderr, "font load failed: %s\n", metrics->error().c_str());
        return nullptr;
    }
    return metrics;
}

} // namespace mark2haru_test
