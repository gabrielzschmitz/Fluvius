#!/usr/bin/env python3
"""Package fluvius build outputs into a zip for releases.

Usage:
    python3 scripts/package_release.py <root> --out <out.zip> <entry> [<entry> ...]

Each <entry> is a file or directory inside <root> and is stored in the zip
under its path relative to <root>. Directories are walked recursively.
Missing entries are skipped with a warning.
"""
import argparse
import os
import sys
import zipfile


def add_entry(zip_out: zipfile.ZipFile, root: str, entry: str,
              out_abs: str) -> None:
    path = os.path.join(root, entry)
    if os.path.isdir(path):
        for base, _dirs, files in os.walk(path):
            for filename in files:
                full = os.path.join(base, filename)
                if os.path.abspath(full) == out_abs:
                    continue
                rel = os.path.relpath(full, root)
                zip_out.write(full, rel)
    elif os.path.isfile(path):
        if os.path.abspath(path) != out_abs:
            zip_out.write(path, entry)
    else:
        sys.stderr.write(f"warning: skipping missing entry: {entry}\n")


def main() -> int:
    parser = argparse.ArgumentParser(
            description="Package fluvius build outputs into a zip.")
    parser.add_argument("root", help="directory containing the build outputs")
    parser.add_argument("--out", required=True, help="output zip path")
    parser.add_argument("entries", nargs="+",
                        help="files/directories inside root to include")
    args = parser.parse_args()
    out_abs = os.path.abspath(args.out)

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as zip_out:
        for entry in args.entries:
            add_entry(zip_out, args.root, entry, out_abs)
    print(f"Packaged {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
