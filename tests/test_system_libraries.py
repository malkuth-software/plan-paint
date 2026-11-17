"""The package check's classification of otool, readelf, ldd and objdump text."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_system_libraries as check  # noqa: E402

OTOOL_DYNAMIC_CODECS = """plan-paint:
\t/opt/homebrew/opt/libtiff/lib/libtiff.6.dylib (compatibility version 8.0.0, current version 8.0.0)
\t/opt/homebrew/opt/webp/lib/libwebp.7.dylib (compatibility version 9.0.0, current version 9.1.0)
\t/opt/homebrew/opt/dav1d/lib/libdav1d.7.dylib (compatibility version 7.0.0, current version 7.0.0)
\t@rpath/libc++.1.dylib (compatibility version 1.0.0, current version 1.0.0)
\t/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa (compatibility version 1.0.0, current version 24.0.0)
\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
"""

READELF = """
Dynamic section at offset 0x2d0 contains 30 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libX11.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libatk-bridge-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libgio-2.0.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libwebp.so.7]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
"""

LDD = """\tlinux-vdso.so.1 (0x00007ffd4b5f2000)
\tlibX11.so.6 => /lib/x86_64-linux-gnu/libX11.so.6 (0x00007f0a1c000000)
\tlibxcb.so.1 => /usr/lib/x86_64-linux-gnu/libxcb.so.1 (0x00007f0a1bf00000)
\tlibtiff.so.6 => /usr/local/lib/libtiff.so.6 (0x00007f0a1be00000)
\tlibdav1d.so.7 => not found
\t/lib64/ld-linux-x86-64.so.2 (0x00007f0a1c400000)
"""

OBJDUMP_PE = """
The Import Tables (interpreted .idata section contents)
\tDLL Name: KERNEL32.dll
\tDLL Name: api-ms-win-crt-runtime-l1-1-0.dll
\tDLL Name: libwebp-7.dll
\tDLL Name: libc++.dll
\tDLL Name: USER32.dll
"""


class SystemLibraries(unittest.TestCase):
    def test_formats_from_headers(self):
        self.assertEqual(check.binary_format(b"\xcf\xfa\xed\xfe"), "mach-o")
        self.assertEqual(check.binary_format(b"\xca\xfe\xba\xbe"), "mach-o")
        self.assertEqual(check.binary_format(b"\x7fELF"), "elf")
        self.assertEqual(check.binary_format(b"MZ\x90\x00"), "pe")
        self.assertIsNone(check.binary_format(b"#!/b"))

    def test_macos_refuses_homebrew_codecs(self):
        imports = check.macos_imports(OTOOL_DYNAMIC_CODECS)
        self.assertEqual(len(imports), 6)
        self.assertEqual(check.macos_refusals(imports, allow_shared_cxx_runtime=True), [
            "/opt/homebrew/opt/libtiff/lib/libtiff.6.dylib",
            "/opt/homebrew/opt/webp/lib/libwebp.7.dylib",
            "/opt/homebrew/opt/dav1d/lib/libdav1d.7.dylib"])
        self.assertIn("@rpath/libc++.1.dylib", check.macos_refusals(imports))

    def test_macos_skips_a_dylib_own_id(self):
        listing = "libc++.1.dylib:\n\t@rpath/libc++.1.dylib (compatibility version 1.0.0, current version 1.0.0)\n" \
                  "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)\n"
        self.assertEqual(check.macos_imports(listing, "@rpath/libc++.1.dylib"), ["/usr/lib/libSystem.B.dylib"])

    def test_linux_admits_the_host_libraries_only(self):
        needed = check.linux_needed(READELF)
        self.assertEqual(len(needed), 7)
        self.assertEqual(check.linux_refusals(needed), ["libwebp.so.7"])
        self.assertEqual(check.linux_refusals(["libc++.so.1"]), ["libc++.so.1"])
        self.assertEqual(check.linux_refusals(["libc++.so.1"], allow_shared_cxx_runtime=True), [])
        self.assertEqual(check.linux_refusals(["libc.musl-x86_64.so.1", "libXi.so.6", "libgcc_s.so.1"]), [])

    def test_ldd_refuses_missing_and_nonsystem_locations(self):
        refused = check.ldd_refusals(LDD)
        self.assertEqual(len(refused), 2)
        self.assertTrue(refused[0].startswith("libtiff.so.6 => /usr/local/lib/"))
        self.assertEqual(refused[1], "libdav1d.so.7 => not found")
        self.assertEqual(check.ldd_refusals("\tstatically linked\n"), [])

    def test_windows_refuses_codec_and_runtime_dlls(self):
        imports = check.windows_imports(OBJDUMP_PE)
        self.assertEqual(check.windows_refusals(imports), ["libwebp-7.dll", "libc++.dll"])
        self.assertEqual(check.windows_refusals(imports, allow_shared_cxx_runtime=True), ["libwebp-7.dll"])

    def test_require_names_every_failure(self):
        script = Path(__file__)
        with self.assertRaises(RuntimeError) as raised:
            check.require_system_libraries([script])
        self.assertIn("not a Mach-O, ELF or PE file", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
