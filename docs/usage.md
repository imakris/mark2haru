# Usage reference

See the [README](../README.md) for building and a first render. The public
[rendering header](../include/mark2haru/render.h) owns the API, options, and defaults.

## Markdown and layout

The renderer accepts these forms:

- Paragraphs with source-line reflow: soft line breaks join as spaces.
- ATX headings (`#` through `######`).
- Bulleted and numbered lists with continuation lines.
- Fenced code blocks and inline `**bold**`, `*italic*`, `***bold italic***`, and
  `` `code` ``.
- Pipe tables.
- Standalone PNG image blocks (`![alt](path.png)`).
- Links, rendered as `text (url)`.
- Thematic breaks (`---`, `***`, `___`, including spaced forms such as `- - -`).
- Explicit page breaks on a line containing `<!-- pagebreak -->`.

This subset does not provide full CommonMark compatibility, inline images inside
paragraphs, or table column alignment from `:---`, `---:`, and `:---:` markers.

Layout uses greedy wrapping and automatic pagination. Tables use content-aware
column widths, split across pages, and repeat their header row.

`Render_options` controls page dimensions and margins in points, body text size,
line spacing, image paths, and fonts. See the rendering header for field names and
defaults. Relative image paths resolve against `asset_base_dir`; the CLI sets this
to the input Markdown file's directory.

## Fonts

The bundled DejaVu faces are in [fonts](../fonts): `DejaVuSans.ttf`,
`DejaVuSans-Bold.ttf`, `DejaVuSans-Oblique.ttf`, `DejaVuSans-BoldOblique.ttf`, and
`DejaVuSansMono.ttf`. Builds copy these files into a `fonts` directory beside the
CLI. Keep all five available: all font slots must resolve and load, even when a
document uses only regular text.

Only the font variants used in the document are embedded in the PDF. Each used
variant embeds its full TrueType file; fonts are not subsetted by glyph. The PDF
therefore does not depend on fonts installed on the viewer's machine.

Coverage depends on the selected face. A code point with no glyph falls back to
`?` if available, otherwise to the font's `.notdef` glyph. If a required font
cannot be resolved or loaded, rendering fails; the overload accepting an error
string reports the reason.

Set `Render_options::font_root_dir` to the directory containing your fonts or its
parent. For another family, use the explicit face paths in `font_family`, defined
in [font_config.h](../include/mark2haru/font_config.h), or replace the bundled files
with compatible TrueType faces using the same names.

## CMake integration

For a source-tree dependency, use `add_subdirectory` and link to
`mark2haru::mark2haru`. To use an installed package, install the build:

```sh
cmake --install build --config Release --prefix /path/to/install
```

Make the installation prefix available through `CMAKE_PREFIX_PATH` in your
consumer project, then use:

```cmake
find_package(mark2haru CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE mark2haru::mark2haru)
```

The package exports `mark2haru_FONT_ROOT`, pointing to its installed data directory
(`share/mark2haru` by default). Pass that value to `Render_options::font_root_dir`
to use the installed bundled fonts. The installed CLI locates them itself.

## Tests and contributions

Run the suite from a build with tests enabled:

```sh
ctest --test-dir build -C Release --output-on-failure
```

Contributions follow the [Varinomics coding standards](https://github.com/Varinomics/varinomics-standards).
