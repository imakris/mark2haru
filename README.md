# mark2haru

[![Build and test](https://github.com/imakris/mark2haru/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/imakris/mark2haru/actions/workflows/build-and-test.yml)

`mark2haru` is a small standalone Markdown-to-PDF renderer for neutral brief-style documents.
It is derived from the Markdown/PDF path used in `briefutil`, but it deliberately removes
letter-generation semantics, profile handling, Qt UI code, and branding.

The name is a legacy nod to the original `briefutil` PDF path, which started life on top of
libHaru. This project no longer depends on libHaru (or any external PDF library) — it contains
its own minimal PDF writer and TrueType parser, and links only the bundled `miniz` for Flate
compression.

## What it supports

- paragraphs with source-line reflow (soft line breaks join as spaces)
- ATX headings (`#` through `######`)
- bulleted and numbered lists with continuation lines
- fenced code blocks
- simple pipe tables that split across pages (headers repeat)
- inline `**bold**`, `*italic*`, `***bold italic***`, and `` `code` ``
- links rendered as `text (url)` so the destination is not lost
- explicit page breaks via `<!-- pagebreak -->`

## What it does not try to do

- Markdown image syntax
- full CommonMark compatibility
- font subsetting (see below)

The renderer embeds TrueType fonts into the PDF and emits Unicode text using `Identity-H`
composite fonts. Input stays UTF-8, and Western European characters plus wider Unicode covered
by the bundled fonts render correctly without depending on the viewer's installed fonts.

Only the font *variants* actually used by a document are embedded — a plain brief that uses only
regular text does not pay for bold, italic, or mono font data. Each variant that *is* used
embeds the full TrueType file, not a per-glyph subset: the `/W` widths array is trimmed to the
used glyph set, but the `glyf`/`loca`/`cmap` tables are unchanged. True subsetting is a possible
follow-up; it would significantly shrink the output for short documents.

Bundled fonts:

- `DejaVuSans.ttf`
- `DejaVuSans-Bold.ttf`
- `DejaVuSans-Oblique.ttf`
- `DejaVuSans-BoldOblique.ttf`
- `DejaVuSansMono.ttf`

These are copied next to the executable at build time and embedded into each generated PDF when
that face is used. If you want a different font family, replace the files in `mark2haru/fonts/`
with compatible TrueType faces using the same names.

If the renderer cannot resolve or load the configured fonts, rendering fails instead of silently
falling back to viewer-installed fonts. The library overload that accepts an error string reports
that reason to the caller.

PDF content streams, font streams, and Unicode CMaps are Flate-compressed when compression makes
the object smaller. The project uses the lightweight bundled `miniz` implementation, so it does
not depend on system zlib.

## CI Status

| Platform | Build + test | Consumer install smoke | Notes |
| :-- | :--: | :--: | :-- |
| Linux | GitHub Actions | GitHub Actions | Configures with CMake, builds in Release, runs `ctest`, and validates an installed downstream consumer. |
| macOS | GitHub Actions | GitHub Actions | Configures with CMake, builds in Release, runs `ctest`, and validates an installed downstream consumer. |
| Windows | GitHub Actions | GitHub Actions | Uses the same CMake/CTest flow with the Release config on the hosted MSVC runner. |

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Release
```

On Windows the same two commands work from a Developer Command Prompt or PowerShell.

Any C++17-capable compiler and CMake 3.20+ should work.

## Test

After building, the test suite covers:

- smoke rendering of the bundled examples, with PDF-header verification
- Markdown parser cases for escapes, links, lists, code blocks, tables, and explicit page breaks
- negative-path checks for malformed font tables and render/write failure reporting

Run the full suite with:

```sh
ctest --test-dir build --output-on-failure
```

## Run

Render the sample brief:

```sh
./build/mark2haru examples/neutral_brief.md build/neutral_brief.pdf
```

Or render your own file:

```sh
./build/mark2haru input.md output.pdf
```

On Windows the binary is `build\mark2haru.exe`.

## Library Use

The simplest library entry point is:

```cpp
bool render_markdown_to_pdf(
    const std::string& markdown,
    const std::filesystem::path& output_path,
    const Render_options& options = {});
```

If you want the failure reason, use the overload with an output string:

```cpp
std::string error;
if (!mark2haru::render_markdown_to_pdf(markdown, output_path, options, error)) {
    // inspect `error`
}
```

`Render_options` lets you control page geometry, body text sizing, line spacing, and the font
root directory used during font resolution.

## Example

`examples/neutral_brief.md` shows a neutral EBICS-style brief with headings, paragraphs,
lists, and a table.

`examples/western_european.md` is a focused sample with umlauts and accents for verification.
It also includes Greek and Cyrillic to exercise Unicode text beyond Windows-1252.

## Assumptions

- The output is intended for short-to-medium briefs rather than long books.
- The layout engine uses simple greedy wrapping and basic pagination.
- Tables use equal-width columns. Long tables split across pages and repeat the header row.
- Markdown image syntax is not implemented, even though the lower-level PDF writer can place PNG
  images when used directly.
- Font coverage is defined by the bundled DejaVu family. Code points with no glyph in the
  selected face fall back to `?` if the font has it, otherwise to the font's `.notdef` glyph.
- Stream compression is size-aware rather than forced; very small objects may remain uncompressed
  if that produces a smaller file overall.

## Contributing

Contributions should follow the Varinomics coding style guideline. The canonical guideline is
published by Varinomics at <https://github.com/Varinomics/varinomics-standards>; general project
information is available at <https://varinomics.com/>.

## Attribution

- Bundled DejaVu fonts are distributed under the DejaVu / Bitstream Vera license. See the
  upstream project at <https://dejavu-fonts.github.io/> for full terms.
- `third_party/miniz` is vendored from <https://github.com/richgel999/miniz> (MIT/zlib-style
  license).
