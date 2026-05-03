#include <mark2haru/font_context.h>
#include <mark2haru/pdf_writer.h>

#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>(),
    };
}

std::string inflate_stream(const std::string& compressed)
{
    mz_ulong out_len = 4096;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::string out(out_len, '\0');
        mz_ulong actual_len = out_len;
        const int rc = mz_uncompress(
            reinterpret_cast<unsigned char*>(out.data()),
            &actual_len,
            reinterpret_cast<const unsigned char*>(compressed.data()),
            static_cast<mz_ulong>(compressed.size()));
        if (rc == MZ_OK) {
            out.resize(actual_len);
            return out;
        }
        out_len *= 2;
    }
    return {};
}

std::string decoded_pdf_streams(const std::string& pdf)
{
    std::string decoded;
    std::size_t scan = 0;
    while (true) {
        const std::size_t stream_pos = pdf.find("stream\n", scan);
        if (stream_pos == std::string::npos) {
            break;
        }

        const std::size_t payload_pos = stream_pos + 7;
        const std::size_t end_pos = pdf.find("\nendstream", payload_pos);
        if (end_pos == std::string::npos) {
            break;
        }

        const std::size_t dict_start = pdf.rfind("<<", stream_pos);
        const std::string dict = dict_start == std::string::npos
            ? std::string()
            : pdf.substr(dict_start, stream_pos - dict_start);
        const std::string payload = pdf.substr(payload_pos, end_pos - payload_pos);
        if (dict.find("/FlateDecode") != std::string::npos) {
            decoded += inflate_stream(payload);
        }
        else {
            decoded += payload;
        }
        decoded.push_back('\n');
        scan = end_pos + 10;
    }
    return decoded;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char* argv[])
{
    const fs::path exe_dir = argc > 0
        ? fs::absolute(argv[0]).parent_path()
        : fs::current_path();
    const fs::path out_path = exe_dir / "test_pdf_writer.pdf";

    auto metrics = std::make_shared<mark2haru::Measurement_context>(
        mark2haru::Font_family_config::briefutil_default(),
        exe_dir);
    if (!metrics->loaded()) {
        std::cerr << metrics->error() << "\n";
        return 1;
    }

    mark2haru::Pdf_writer writer(200.0, 200.0, metrics);
    if (!writer.fonts_loaded()) {
        std::cerr << writer.font_error() << "\n";
        return 2;
    }

    writer.draw_text(
        10.0,
        20.0,
        12.0,
        mark2haru::Pdf_font::REGULAR,
        "colored text",
        { 0.25, 0.5, 0.75 });
    writer.stroke_line(
        10.0,
        50.0,
        90.0,
        70.0,
        { 0.8, 0.1, 0.2 },
        2.5);

    if (!writer.save(out_path)) {
        std::cerr << "save failed\n";
        return 3;
    }

    const std::string streams = decoded_pdf_streams(read_file(out_path));
    if (!contains(streams, "0.25 0.5 0.75 rg")) {
        std::cerr << "colored text fill color operator missing\n";
        return 4;
    }
    if (!contains(streams, "0.8 0.1 0.2 RG")) {
        std::cerr << "line stroke color operator missing\n";
        return 5;
    }
    if (!contains(streams, "2.5 w")) {
        std::cerr << "line width operator missing\n";
        return 6;
    }
    if (!contains(streams, "10 150 m\n90 130 l\nS")) {
        std::cerr << "line geometry operators missing\n";
        return 7;
    }

    return 0;
}
