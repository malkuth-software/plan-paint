"""Cache admission must reject a wrong revision before extracting any files."""
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import unittest

SPEC = importlib.util.spec_from_file_location("cache", Path(__file__).resolve().parents[1] / "scripts/restore-toolkit-cache.py")
CACHE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CACHE)


class CacheAdmission(unittest.TestCase):
    def archive(self, revision="reviewed", extra=()):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as bundle:
            data = json.dumps({"revision": revision}).encode()
            entry = tarfile.TarInfo("cache-manifest.json")
            entry.size = len(data)
            bundle.addfile(entry, io.BytesIO(data))
            for entry in extra:
                bundle.addfile(entry)
        stream.seek(0)
        return tarfile.open(fileobj=stream)

    def test_revision_checked_before_extraction(self):
        with self.archive("wrong") as bundle, self.assertRaises(ValueError):
            CACHE.validated_members(bundle, "reviewed")

    def test_rejects_traversal_and_special_files(self):
        for name in (".ccache/../../outside", "C:/outside", ".ccache\\outside"):
            with self.subTest(name=name), self.archive(extra=[tarfile.TarInfo(name)]) as bundle:
                with self.assertRaises(ValueError):
                    CACHE.validated_members(bundle, "reviewed")
        entry = tarfile.TarInfo(".ccache/device")
        entry.type = tarfile.CHRTYPE
        with self.archive(extra=[entry]) as bundle, self.assertRaises(ValueError):
            CACHE.validated_members(bundle, "reviewed")

    def test_rejects_symlink_parent(self):
        link = tarfile.TarInfo(".ccache/link")
        link.type = tarfile.SYMTYPE
        link.linkname = "target"
        with self.archive(extra=[link, tarfile.TarInfo(".ccache/link/file")]) as bundle:
            with self.assertRaises(ValueError):
                CACHE.validated_members(bundle, "reviewed")

    def test_selects_only_cache_and_runtime(self):
        entries = [tarfile.TarInfo(name) for name in (".ccache/a", ".build/toolchain/runtime/lib", "unrelated")]
        with self.archive(extra=entries) as bundle:
            admitted = [entry.name for entry in CACHE.validated_members(bundle, "reviewed")]
        self.assertEqual(admitted, ["cache-manifest.json", ".ccache/a", ".build/toolchain/runtime/lib"])


if __name__ == "__main__":
    unittest.main()
