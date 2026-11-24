"""Detect the compiler's localized /showIncludes prefix without ANSI loss."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def find_prefix(output):
    try:
        text = output.decode("utf-8")
    except UnicodeDecodeError:
        # Older MSVC versions may still use the Windows ANSI code page.
        text = output.decode("mbcs")
    for line in text.splitlines():
        match = re.match(
            r"^(.*?)(?:[A-Za-z]:[\\/]|/|\.\.?[\\/])"
            r".*xenia_showincludes_probe\.h$",
            line,
        )
        if match and match[1]:
            return match[1]
    raise ValueError("Compiler output did not identify the probe header")


def main():
    probe_root = Path(sys.argv[1])
    compiler = sys.argv[2:]
    probe_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=probe_root) as directory:
        probe = Path(directory)
        (probe / "xenia_showincludes_probe.h").write_text("\n", encoding="ascii")
        source = probe / "main.c"
        source.write_text('#include "xenia_showincludes_probe.h"\n', encoding="ascii")
        result = subprocess.run(
            compiler + ["/nologo", "/showIncludes", "/Zs", str(source)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        )
        prefix = find_prefix(result.stdout)
    # CMake explicitly reads UTF-8, independently of Python's console encoding.
    sys.stdout.buffer.write(prefix.encode("utf-8"))


if __name__ == "__main__":
    main()
