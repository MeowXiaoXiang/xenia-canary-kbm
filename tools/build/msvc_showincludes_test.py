"""Regression checks for localized MSVC dependency-prefix detection."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from msvc_showincludes import find_prefix


class PrefixTests(unittest.TestCase):
    def test_english_prefix_retains_trailing_space(self):
        output = (
            b"main.c\r\nNote: including file: C:\\probe\\xenia_showincludes_probe.h\r\n"
        )
        self.assertEqual(find_prefix(output), "Note: including file: ")

    def test_utf8_localized_prefixes(self):
        for prefix in (
            "注意: 包含檔案: ",
            "注意: 包含文件: ",
            "メモ: インクルード ファイル: ",
        ):
            with self.subTest(prefix=prefix):
                output = (
                    prefix + "C:\\probe dir\\xenia_showincludes_probe.h\r\n"
                ).encode("utf-8")
                self.assertEqual(find_prefix(output), prefix)

    @unittest.skipUnless(sys.platform == "win32", "Windows ANSI codec required")
    def test_ansi_localized_prefix(self):
        prefix = "注意: 包含檔案: "
        try:
            output = (prefix + "C:\\probe\\xenia_showincludes_probe.h\n").encode("mbcs")
        except UnicodeEncodeError:
            self.skipTest("Current ANSI code page cannot encode this prefix")
        self.assertEqual(find_prefix(output), prefix)

    def test_unrelated_diagnostics_cannot_become_a_prefix(self):
        with self.assertRaises(ValueError):
            find_prefix(b"main.c\nmain.c(1): fatal error C1083: missing header\n")


@unittest.skipUnless(
    sys.platform == "win32" and os.environ.get("XENIA_TEST_MSVC_DEPS") == "1",
    "Opt in from an MSVC developer environment with XENIA_TEST_MSVC_DEPS=1",
)
class NinjaDependencyTests(unittest.TestCase):
    def test_header_change_rebuilds_c_and_cpp(self):
        module = (
            Path(__file__).resolve().parents[2] / "cmake/XeniaMsvcDependencies.cmake"
        )
        compiler = shutil.which("cl")
        self.assertIsNotNone(compiler)

        def run(*args):
            result = subprocess.run(
                args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
            )
            output = result.stdout.decode("utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, output)
            return output

        with tempfile.TemporaryDirectory(prefix="xenia-msvc-deps-") as directory:
            source = Path(directory) / "source with spaces"
            source.mkdir()
            build = Path(directory) / "build"
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(deps_probe LANGUAGES C CXX)\n'
                'find_package(Python3 REQUIRED COMPONENTS Interpreter)\n'
                f'include("{module.as_posix()}")\n'
                'xe_detect_msvc_include_prefix()\n'
                'add_executable(probe_c main.c)\n'
                'add_executable(probe_cpp main.cc)\n',
                encoding="utf-8",
            )
            header = source / "value.h"
            header.write_text('#define PROBE_VALUE 1\n', encoding="ascii")
            code = (
                '#include <stdio.h>\n#include "value.h"\n'
                'int main(void) { printf("%d\\n", PROBE_VALUE); return 0; }\n'
            )
            for name in ("main.c", "main.cc"):
                (source / name).write_text(code, encoding="ascii")
            run(
                "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                f"-DCMAKE_C_COMPILER={compiler}",
                f"-DCMAKE_CXX_COMPILER={compiler}",
            )
            run("cmake", "--build", str(build))
            for name in ("probe_c.exe", "probe_cpp.exe"):
                self.assertEqual(run(str(build / name)).strip(), "1")
            header.write_text('#define PROBE_VALUE 2\n', encoding="ascii")
            run("cmake", "--build", str(build))
            for name in ("probe_c.exe", "probe_cpp.exe"):
                self.assertEqual(run(str(build / name)).strip(), "2")
            self.assertIn("no work to do", run("cmake", "--build", str(build)))


if __name__ == "__main__":
    unittest.main()
