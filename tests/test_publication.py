import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import publication_version as versions
import publish_tested_release as publisher


class PublicationTests(unittest.TestCase):
    def test_version_selection(self):
        self.assertEqual(versions.select("1.1.4", "push", "refs/heads/main", 40),
                         {"version": "1.1.44", "tag": "v1.1.44", "publish": "true"})
        self.assertEqual(versions.select("1.1.4", "pull_request", "refs/pull/4/merge", 40)["publish"], "false")
        self.assertEqual(versions.select("1.1.4", "workflow_dispatch", "refs/heads/main", 40)["version"], "1.1.4")
        self.assertEqual(versions.select("1.1.4", "workflow_dispatch", "refs/heads/main", 40, True)["version"], "1.1.44")
        self.assertEqual(versions.select("1.1.4", "push", "refs/tags/v1.1.50", 40)["version"], "1.1.50")
        with self.assertRaises(ValueError):
            versions.select("1.1.4", "workflow_dispatch", "refs/heads/topic", 40, True)
        for tag in ("v1.1.3", "v2.0.0", "v1.1.04", "v1.1.4-beta"):
            with self.assertRaises(ValueError):
                versions.select("1.1.4", "push", "refs/tags/" + tag, 40)
        with self.assertRaises(ValueError):
            versions.select("1.1.4", "push", "refs/heads/main", 65535)

    def packages(self, directory):
        for suffix in ("windows-x64.zip", "macos-arm64.pkg", "linux-x64.tar.gz", "linux-arm64.tar.gz"):
            (directory / ("plan-paint-1.1.44-" + suffix)).write_bytes(b"tested package")

    def test_complete_package_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.packages(directory)
            self.assertEqual(len(publisher.package_files(directory, "1.1.44")), 4)
            (directory / "unexpected.zip").write_bytes(b"extra")
            with self.assertRaises(ValueError):
                publisher.package_files(directory, "1.1.44")
            (directory / "unexpected.zip").unlink()
            (directory / "plan-paint-1.1.44-windows-x64.zip").write_bytes(b"")
            with self.assertRaises(ValueError):
                publisher.package_files(directory, "1.1.44")

    def exercise(self, directory, responses, gh_responses):
        environment = {"GITHUB_REPOSITORY": "owner/paint", "GITHUB_SHA": "a" * 40,
                       "GITHUB_REF": "refs/heads/main", "RELEASE_VERSION": "1.1.44", "RELEASE_TAG": "v1.1.44"}
        with patch.dict(os.environ, environment), patch.object(sys, "argv", ["publish", "--artifacts", str(directory)]), \
             patch.object(publisher.subprocess, "run", side_effect=responses), \
             patch.object(publisher, "gh", side_effect=gh_responses) as calls:
            publisher.main()
            return calls.call_args_list

    def test_stale_main_does_not_publish(self):
        calls = self.exercise(Path("unused"), [], ["b" * 40])
        self.assertEqual(len(calls), 1)

    def test_conflicting_tag_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.packages(directory)
            with self.assertRaisesRegex(ValueError, "different commit"):
                self.exercise(directory, [subprocess.CompletedProcess([], 0)], ["a" * 40, "a" * 40, "b" * 40])

    def test_draft_upload_precedes_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.packages(directory)
            absent = subprocess.CompletedProcess([], 1, stdout="", stderr="HTTP 404")
            calls = self.exercise(directory, [absent, absent],
                                  ["a" * 40, "a" * 40, "", "a" * 40, "", "", ""])
            self.assertIn("--draft", calls[-3].args)
            self.assertEqual(calls[-2].args[:2], ("release", "upload"))
            self.assertEqual(calls[-1].args[:2], ("release", "edit"))
            self.assertIn("--draft=false", calls[-1].args)
            self.assertEqual(len((directory / "SHA256SUMS").read_text().splitlines()), 4)

    def test_published_release_is_immutable(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.packages(directory)
            calls = self.exercise(directory, [subprocess.CompletedProcess([], 0),
                subprocess.CompletedProcess([], 0, stdout='{"isDraft":false,"targetCommitish":"' + "a" * 40 + '"}')],
                ["a" * 40] * 4)
            self.assertFalse(any(call.args[:1] == ("release",) for call in calls))

    def test_failed_upload_never_publishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.packages(directory)
            absent = subprocess.CompletedProcess([], 1, stdout="", stderr="HTTP 404")
            with self.assertRaises(subprocess.CalledProcessError):
                self.exercise(directory, [absent, absent], ["a" * 40, "a" * 40, "", "a" * 40, "",
                    subprocess.CalledProcessError(1, "upload")])


if __name__ == "__main__":
    unittest.main()
