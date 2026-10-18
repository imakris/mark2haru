"""Check the installed CLI's font discovery through real process launches."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zlib


def embedded_fonts(pdf):
    fonts = []
    for font_id in re.findall(rb"/FontFile2 (\d+) 0 R", pdf):
        stream = re.search(
            rb"(?m)^" + font_id + rb" 0 obj\s*<<(.*?)>>\s*stream\r?\n",
            pdf,
            re.DOTALL,
        )
        if stream is None:
            raise RuntimeError("embedded font stream is missing")
        length = re.search(rb"/Length\s+(\d+)", stream[1])
        if length is None:
            raise RuntimeError("embedded font stream length is missing")
        data = pdf[stream.end():stream.end() + int(length[1])]
        if b"/FlateDecode" in stream[1]:
            data = zlib.decompress(data)
        fonts.append(data)
    return fonts


def check_install(install_prefix):
    with tempfile.TemporaryDirectory(prefix="mark2haru fonts ") as scratch:
        root = Path(scratch)
        prefix = root / "install"
        shutil.copytree(install_prefix, prefix)
        executable = prefix / "bin" / ("mark2haru.exe" if os.name == "nt" else "mark2haru")
        run_dir = root / "unrelated"
        run_dir.mkdir()
        (run_dir / "input.md").write_text(
            "Regular **bold** *italic* ***bold italic*** `monospace`.\n", encoding="utf-8"
        )

        # Harmless trailing bytes distinguish these installed files from an
        # identical system DejaVu installation, even after PDF compression.
        expected_fonts = {}
        for font in (prefix / "share/mark2haru/fonts").glob("*.ttf"):
            data = font.read_bytes() + b"mark2haru installed font fixture"
            font.write_bytes(data)
            expected_fonts[font.name] = data
        if len(expected_fonts) != 5:
            raise RuntimeError("installed bundled font set is incomplete")

        # Windows process lookup uses the launching process's PATH, not the
        # environment block passed for the child by subprocess.run.
        os.environ["PATH"] = str(executable.parent) + os.pathsep + os.environ.get("PATH", "")
        commands = [
            ("absolute", str(executable)),
            ("relative", os.path.relpath(executable, run_dir)),
            ("path", "mark2haru"),
        ]
        if os.name != "nt":
            link_dir = root / "links"
            link_dir.mkdir()
            (link_dir / "mark2haru").symlink_to(executable)
            commands.append(("symlink", str(link_dir / "mark2haru")))

        original_dir = Path.cwd()
        try:
            os.chdir(run_dir)
            for label, command in commands:
                subprocess.run([command, "input.md", label + ".pdf"], check=True)
                fonts = embedded_fonts((run_dir / (label + ".pdf")).read_bytes())
                for name, expected in expected_fonts.items():
                    if expected not in fonts:
                        raise RuntimeError(f"{label} invocation did not embed installed {name}")
                print(f"{label}: all five installed bundled fonts embedded")
        finally:
            os.chdir(original_dir)


if __name__ == "__main__":
    try:
        check_install(Path(sys.argv[1]).resolve())
    except (OSError, RuntimeError, subprocess.CalledProcessError, zlib.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
