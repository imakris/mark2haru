"""Check actual consumers with split and shared configuration destinations."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


def check_fonts(directory, expected):
    for name, digest in expected.items():
        font = directory / "fonts" / name
        if not font.is_file():
            raise RuntimeError(f"missing staged font: {font}")
        if hashlib.sha256(font.read_bytes()).digest() != digest:
            raise RuntimeError(f"incorrect staged font: {font}")
    print(f"All five bundled fonts match in {directory}", flush=True)


def check_staging(args):
    source = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=False)
    expected = {
        font.name: hashlib.sha256(font.read_bytes()).digest()
        for font in (source / "fonts").glob("*.ttf")
    }
    if len(expected) != 5:
        raise RuntimeError("source bundled font set is incomplete")

    subprocess.run([
        args.cmake, "-S", str(source / "tests/font_staging"), "-B", str(build),
        "-G", "Ninja Multi-Config", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
        "-DCMAKE_CONFIGURATION_TYPES=Debug;Release",
        "-DCMAKE_POLICY_DEFAULT_CMP0141=NEW",
        "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded",
    ], check=True)

    def build_targets(config, *targets):
        subprocess.run([
            args.cmake, "--build", str(build), "--config", config,
            "--target", *targets, "--parallel", str(args.parallel),
        ], check=True)

    # Building only the second consumer must stage its own destination.
    build_targets("Debug", "test_pdf_writer")
    check_fonts(build / "debug writer", expected)
    build_targets("Debug", "mark2haru", "test_pdf_writer")
    check_fonts(build / "debug cli", expected)
    check_fonts(build / "debug writer", expected)

    # Both consumers converge on one destination for this configuration.
    build_targets("Release", "mark2haru", "test_pdf_writer")
    check_fonts(build / "release shared", expected)

    # Staging is also required when no compiler or linker work remains.
    (build / "release shared/fonts/DejaVuSans-Oblique.ttf").unlink()
    build_targets("Release", "test_pdf_writer")
    check_fonts(build / "release shared", expected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--parallel", type=int, choices=range(1, 9), default=2)
    arguments = parser.parse_args()
    try:
        check_staging(arguments)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
