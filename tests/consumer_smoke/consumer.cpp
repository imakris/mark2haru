// Smoke test for the installed package: link the exported target, load the
// installed fonts through the public headers, and open a writer with them.
// Takes the install prefix as its argument so it does not depend on being run
// from any particular directory.

#include <mark2haru/mark2haru.h>

#include <cstdio>
#include <filesystem>
#include <memory>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: consumer_smoke <install-prefix>\n");
        return 3;
    }

    mark2haru::Render_options options;
    options.font_root_dir = std::filesystem::path(argv[1]) / "share" / "mark2haru";

    auto metrics = std::make_shared<mark2haru::Measurement_context>(
        options.font_family,
        options.font_root_dir);
    if (!metrics->loaded()) {
        std::fprintf(stderr, "installed fonts did not load: %s\n", metrics->error().c_str());
        return 1;
    }

    mark2haru::Pdf_writer writer(100.0, 100.0, metrics);
    if (!writer.fonts_loaded()) {
        std::fprintf(stderr, "writer rejected the installed fonts: %s\n",
            writer.font_error().c_str());
        return 2;
    }
    return 0;
}
