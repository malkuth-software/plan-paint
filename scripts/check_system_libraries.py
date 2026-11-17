#!/usr/bin/env python3
"""Refuse a program that imports any library but the operating system's.

A shipped Plan Paint is one module (owner rule, 2026-11-15): the codecs,
GUI.Forms and the C++ runtime are linked into the program, and its imports
name only system libraries. Each package script runs this check on the
program before it is bundled; the command line runs it on any binaries:

    python3 scripts/check_system_libraries.py [--allow-shared-cxx-runtime]
        [--objdump OBJDUMP] BINARY...

Mach-O: every `otool -L` entry is under /System/ or /usr/lib/. ELF: every
NEEDED entry (`readelf -d`) is the C library or a library GUI.Forms' Linux
host links, and every library `ldd` resolves lies in a system directory.
PE: every DLL Name (`objdump -p`) is a Windows system DLL.

--allow-shared-cxx-runtime admits libc++, libc++abi and libunwind: the macOS
harness toolchain (template_harness/harness/cmake/llvm22.cmake) links the
shared runtime when the LLVM tree has no static archives, and the macOS
package then bundles them; package-macos.py passes it only while the build's
HARNESS_STATIC_CXX_RUNTIME is OFF.
"""
from pathlib import Path
import argparse
import re
import subprocess
import sys

MACOS_SYSTEM_PREFIXES = ("/System/", "/usr/lib/")
# The LLVM runtime names --allow-shared-cxx-runtime admits, as a dependency's
# last path component: libc++.1.dylib, libc++abi.1.dylib, libunwind.1.dylib,
# libc++.so.1, libc++.dll and so on.
SHARED_CXX_RUNTIME = re.compile(r"lib(?:c\+\+|c\+\+abi|unwind)(?:\.\d+)?\.(?:dylib|so(?:\.\d+)*|dll)")

# Linux: the C library (glibc or musl, their loaders and the vDSO), GCC's
# unwinder that glibc itself loads, and the system libraries GUI.Forms'
# Linux host links (its CMakeLists.txt GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES:
# X11, Xfixes, Xrender, the optional Xcursor, Xrandr, Xext and Xi, ATK, the
# ATK bridge and GIO with GObject and GLib).
LINUX_SYSTEM_LIBRARY = re.compile(
    r"(?:lib(?:c|m|dl|pthread|rt|resolv|gcc_s)\.so\.\d+"
    r"|ld-linux[\w.-]*\.so\.\d+|ld-musl-[\w]+\.so\.1|libc\.musl-[\w]+\.so\.1"
    r"|linux-(?:vdso|gate)\.so\.1"
    r"|libX(?:11|fixes|render|cursor|randr|ext|i)\.so\.\d+"
    r"|libatk-1\.0\.so\.0|libatk-bridge-2\.0\.so\.0"
    r"|lib(?:gio|gobject|glib)-2\.0\.so\.0)")
LINUX_SYSTEM_DIRECTORIES = ("/lib/", "/lib32/", "/lib64/", "/usr/lib/", "/usr/lib32/", "/usr/lib64/")
LINUX_NEEDED_ENTRY = re.compile(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]")

# Windows system DLLs (scripts/package-windows.py imports this set).
WINDOWS_SYSTEM_DLLS = frozenset({
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll",
    "ole32.dll", "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll",
    "usp10.dll", "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll",
    "dwmapi.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "bcrypt.dll",
    "secur32.dll", "rpcrt4.dll", "setupapi.dll", "winspool.drv", "winmm.dll",
    "crypt32.dll", "normaliz.dll", "uxtheme.dll", "msimg32.dll", "shcore.dll",
    "dbghelp.dll", "psapi.dll", "userenv.dll", "iphlpapi.dll", "uiautomationcore.dll",
    "api-ms-win-core-synch-l1-2-0.dll"})
WINDOWS_DLL_ENTRY = re.compile(r"DLL Name:\s*(\S+)")


def last_component(name):
    return re.split(r"[/\\]", name)[-1]


def binary_format(header):
    """"mach-o", "elf", "pe" or None, from a file's first four bytes."""
    if header[:4] in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce",
                      b"\xca\xfe\xba\xbe"):
        return "mach-o"
    if header[:4] == b"\x7fELF":
        return "elf"
    if header[:2] == b"MZ":
        return "pe"
    return None


def macos_imports(listing, own_id=None):
    """The install names `otool -L` lists, without its header and own id."""
    names = []
    for line in listing.splitlines()[1:]:
        match = re.match(r"\s+(\S.*?) \(compatibility version", line)
        if match and match[1] != own_id and match[1] not in names:
            names.append(match[1])
    return names


def macos_refusals(imports, allow_shared_cxx_runtime=False):
    refused = []
    for name in imports:
        if name.startswith(MACOS_SYSTEM_PREFIXES):
            continue
        if allow_shared_cxx_runtime and SHARED_CXX_RUNTIME.fullmatch(last_component(name)):
            continue
        refused.append(name)
    return refused


def linux_needed(listing):
    """The NEEDED entries `readelf -d` lists, in order, once each."""
    names = []
    for name in LINUX_NEEDED_ENTRY.findall(listing):
        if name not in names:
            names.append(name)
    return names


def linux_refusals(needed, allow_shared_cxx_runtime=False):
    refused = []
    for name in needed:
        if LINUX_SYSTEM_LIBRARY.fullmatch(name):
            continue
        if allow_shared_cxx_runtime and SHARED_CXX_RUNTIME.fullmatch(name):
            continue
        refused.append(name)
    return refused


def ldd_refusals(listing):
    """Each `ldd` line whose library is missing or outside a system directory.

    ldd lists the whole closure, so a system library's own dependencies are
    admitted by location: /usr/local, /opt, a home directory or a Homebrew
    prefix is not a system directory.
    """
    refused = []
    for line in listing.splitlines():
        line = line.strip()
        if not line or "statically linked" in line:
            continue
        if "not found" in line:
            refused.append(line)
            continue
        match = re.search(r"=>\s*(/\S+)", line) or re.match(r"(/\S+)", line)
        if match is None:
            continue  # the vDSO: "linux-vdso.so.1 (0x...)"
        if not match[1].startswith(LINUX_SYSTEM_DIRECTORIES):
            refused.append(line)
    return refused


def windows_imports(listing):
    names = []
    for name in WINDOWS_DLL_ENTRY.findall(listing):
        if name not in names:
            names.append(name)
    return names


def windows_system_import(name):
    key = name.lower()
    return key in WINDOWS_SYSTEM_DLLS or key.startswith(("api-ms-win-", "ext-ms-win-"))


def windows_refusals(imports, allow_shared_cxx_runtime=False):
    refused = []
    for name in imports:
        if windows_system_import(name):
            continue
        if allow_shared_cxx_runtime and SHARED_CXX_RUNTIME.fullmatch(name.lower()):
            continue
        refused.append(name)
    return refused


def output(arguments):
    return subprocess.check_output(arguments, text=True)


def refusals(binary, allow_shared_cxx_runtime=False, objdump="objdump"):
    """Every import of `binary` that is not the system's, as printable lines."""
    binary = Path(binary)
    with binary.open("rb") as stream:
        kind = binary_format(stream.read(4))
    if kind == "mach-o":
        identifiers = output(["otool", "-D", str(binary)]).splitlines()[1:]
        own_id = identifiers[0].strip() if identifiers else None
        imports = macos_imports(output(["otool", "-L", str(binary)]), own_id)
        return macos_refusals(imports, allow_shared_cxx_runtime)
    if kind == "elf":
        refused = linux_refusals(linux_needed(output(["readelf", "-d", str(binary)])), allow_shared_cxx_runtime)
        # ldd exits non-zero for a static program; its text still says so.
        listing = subprocess.run(["ldd", str(binary)], text=True, capture_output=True)
        return refused + ldd_refusals(listing.stdout + listing.stderr)
    if kind == "pe":
        return windows_refusals(windows_imports(output([objdump, "-p", str(binary)])), allow_shared_cxx_runtime)
    return [f"{binary}: not a Mach-O, ELF or PE file"]


def require_system_libraries(binaries, allow_shared_cxx_runtime=False, objdump="objdump"):
    """Raise RuntimeError naming every non-system import of `binaries`."""
    failures = []
    for binary in binaries:
        for name in refusals(binary, allow_shared_cxx_runtime, objdump):
            failures.append(f"{binary}: {name}")
    if failures:
        raise RuntimeError("A shipped program imports only system libraries; found:\n  " + "\n  ".join(failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binaries", nargs="+", type=Path)
    parser.add_argument("--allow-shared-cxx-runtime", action="store_true")
    parser.add_argument("--objdump", default="objdump", help="objdump for PE files")
    args = parser.parse_args()
    try:
        require_system_libraries(args.binaries, args.allow_shared_cxx_runtime, args.objdump)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print("Only system libraries imported: " + ", ".join(str(binary) for binary in args.binaries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
