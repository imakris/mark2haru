# mark2haru

`mark2haru` is a small standalone Markdown-to-PDF renderer for neutral brief-style documents.
It is derived from the Markdown/PDF path used in `briefutil`, but it deliberately removes
letter-generation semantics, profile handling, Qt UI code, and branding.

## What it supports

- paragraphs
- ATX headings (`#` through `######`)
- bulleted and numbered lists
- fenced code blocks
- simple pipe tables
- inline `**bold**`, `*italic*`, `***bold italic***`, and `` `code` ``
- links rendered as `text (url)` so the destination is not lost
- explicit page breaks via `<!-- pagebreak -->`

## What it does not try to do

- images
- full CommonMark compatibility
- letter templates, sender profiles, or UI workflow

The renderer now embeds TrueType fonts into the PDF and emits Unicode text using `Identity-H`
composite fonts. Input stays UTF-8, and Western European characters plus wider Unicode covered
by the bundled fonts render correctly without depending on the viewer's installed fonts.
Only the font variants actually used by a document are embedded, so a plain brief that uses only
regular text does not pay for bold, italic, or mono font data.

Bundled fonts:

- `DejaVuSans.ttf`
- `DejaVuSans-Bold.ttf`
- `DejaVuSans-Oblique.ttf`
- `DejaVuSans-BoldOblique.ttf`
- `DejaVuSansMono.ttf`

These are copied next to the executable at build time and embedded into each generated PDF when
that face is used. If you want a different font family, replace the files in `mark2haru/fonts/`
with compatible TrueType faces using the same names.

PDF content streams, font streams, and Unicode CMaps are Flate-compressed when compression makes
the object smaller. The project uses the lightweight bundled `miniz` implementation, so it does
not depend on system zlib.

## Build

From the repository root:

```powershell
cmake -S mark2haru -B mark2haru/build
cmake --build mark2haru/build --config Release
```

Any C++17-capable compiler and CMake 3.20+ should work.

## Run

Render the sample brief:

```powershell
mark2haru\build\mark2haru.exe mark2haru\examples\neutral_brief.md mark2haru\build\neutral_brief.pdf
```

Or render your own file:

```powershell
mark2haru\build\mark2haru.exe input.md output.pdf
```

## Example

`examples/neutral_brief.md` shows a neutral EBICS-style brief with headings, paragraphs,
lists, and a table.

`examples/western_european.md` is a focused sample with umlauts and accents for verification.
It also includes Greek and Cyrillic to exercise Unicode text beyond Windows-1252.

## Assumptions

- The output is intended for short-to-medium briefs rather than long books.
- The layout engine uses simple greedy wrapping and basic pagination.
- Tables use equal-width columns and do not span pages.
- Font coverage is defined by the bundled DejaVu family; unsupported glyphs will still fall back
  to `?`, but that set is far wider than Windows-1252.
- Stream compression is size-aware rather than forced; very small objects may remain uncompressed
  if that produces a smaller file overall.
