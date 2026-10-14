# mark2haru

[![CI Linux](https://github.com/imakris/mark2haru/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/imakris/mark2haru/actions/workflows/ci-linux.yml) [![CI macOS](https://github.com/imakris/mark2haru/actions/workflows/ci-macos.yml/badge.svg?branch=master)](https://github.com/imakris/mark2haru/actions/workflows/ci-macos.yml) [![CI Windows](https://github.com/imakris/mark2haru/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/imakris/mark2haru/actions/workflows/ci-windows.yml)

mark2haru is a small C++ library and command-line tool that turns Markdown briefs
into PDF. It handles headings, lists, tables, code, and PNG images.

It is intended for short-to-medium briefs and accepts a subset of Markdown.
Input is UTF-8; character coverage depends on the selected fonts. Rendering fails
if required fonts cannot be resolved or loaded.

## Build and run

You need a C++17 compiler and CMake 3.21 or later. Fonts and compression code are
bundled. From the repository root, using your compiler's developer shell on Windows:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Render the sample brief:

```sh
./build/mark2haru examples/neutral_brief.md build/neutral_brief.pdf
```

On Windows, use `build\Release\mark2haru.exe` with a multi-configuration generator,
or `build\mark2haru.exe` with a single-configuration generator.

## Use the library

Link your application to the CMake target `mark2haru::mark2haru`:

```cpp
#include <mark2haru/render.h>

std::string error;
bool rendered = mark2haru::render_markdown_to_pdf("# Brief\n\nHello.", "brief.pdf", {}, error);
```

The call returns `false` on failure and supplies the reason in `error`. See the
[usage reference](docs/usage.md) for CMake integration, font locations, and rendering
details; [render.h](include/mark2haru/render.h) defines the options and defaults.

[neutral_brief.md](examples/neutral_brief.md) shows a brief with headings, lists,
and a table. [western_european.md](examples/western_european.md) includes accented,
Greek, and Cyrillic text.

## License

mark2haru uses the [BSD 2-Clause license](LICENSE). Bundled fonts have the
[DejaVu / Bitstream Vera license](fonts/LICENSE.DejaVu), and bundled compression
code has the [miniz license notices](third_party/miniz/LICENSE).
