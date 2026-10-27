#!/usr/bin/env python3
"""Exchange native RGBA/alpha between two Paint instances on an isolated desktop."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    if os.environ.get("PAINT_CLIPBOARD_TEST_ISOLATED") != "1":
        print("Skipped: native clipboard exchange requires an isolated test desktop.")
        return 77
    executable = str(Path(sys.argv[1]).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="plan-paint-clipboard-") as temporary:
        directory = Path(temporary)
        children = []
        with (directory / "producer.log").open("w", encoding="utf-8") as producer_log, \
                (directory / "consumer.log").open("w", encoding="utf-8") as consumer_log:
            try:
                producer = subprocess.Popen([executable, "--clipboard-producer", str(directory)],
                                            stdout=producer_log, stderr=subprocess.STDOUT)
                children.append(producer)
                deadline = time.monotonic() + 35
                while not (directory / "image-ready").exists():
                    if producer.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Native clipboard producer did not become ready")
                    time.sleep(0.02)
                consumer = subprocess.Popen([executable, "--clipboard-consumer", str(directory)],
                                            stdout=consumer_log, stderr=subprocess.STDOUT)
                children.append(consumer)
                if consumer.wait(timeout=40) != 0:
                    raise RuntimeError("Native clipboard consumer failed")
                if producer.wait(timeout=10) != 0:
                    raise RuntimeError("Native clipboard producer failed")
                if not (directory / "done").exists():
                    raise RuntimeError("Native clipboard exchange did not complete")
                print("Two native Paint processes preserved RGBA bytes and transferred the alpha mask.")
                return 0
            finally:
                for child in children:
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait(timeout=5)
                producer_log.flush()
                consumer_log.flush()
                for name in ("producer.log", "consumer.log"):
                    print((directory / name).read_text(encoding="utf-8", errors="replace"))


if __name__ == "__main__":
    raise SystemExit(main())
