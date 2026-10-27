#!/usr/bin/env python3
"""Restore the exact Windows compiler published with the pinned GUI.Forms cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def verify_file(path, expected):
    with path.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != expected:
        raise ValueError('Pinned toolchain digest mismatch: ' + path.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path,
                        default=Path(os.environ.get('RUNNER_TEMP', str(ROOT / '.build/toolchain'))))
    args = parser.parse_args()
    lock = json.loads((ROOT / 'third_party/gui-forms.lock.json').read_text())
    pin = lock['windows_toolchain']
    staging = ROOT / '.build/downloads' / lock['commit']
    staging.mkdir(parents=True, exist_ok=True)
    for name, digest in ((pin['archive'], pin['sha256']), (pin['manifest'], pin['manifest_sha256'])):
        path = staging / name
        if not path.exists():
            subprocess.run(['gh', 'release', 'download', lock['cache_release'], '--repo', lock['repository'],
                            '--pattern', name, '--dir', str(staging)], check=True)
        verify_file(path, digest)
    manifest = json.loads((staging / pin['manifest']).read_text())
    if manifest['spec']['llvm'] != lock['compiler'] or manifest['archive_sha256'] != pin['sha256']:
        raise ValueError('Toolchain manifest does not match the consumer lock')
    # Reuse the provider's validated extraction and MSYS2 wrapper contract.
    sys.path.insert(0, str(ROOT / 'gui_forms/tools'))
    import windows_toolchain
    destination = args.destination.resolve()
    msys_root = windows_toolchain.extract(staging / pin['archive'], manifest, destination)
    wrapper = windows_toolchain.write_wrapper(msys_root, destination / 'paint-msys2', 'CLANG64')
    if os.environ.get('GITHUB_PATH'):
        windows_toolchain.append_line('GITHUB_PATH', str(wrapper.parent))
        windows_toolchain.append_line('GITHUB_ENV', 'MSYSTEM=CLANG64')
    print(msys_root)


if __name__ == '__main__':
    main()
