"""Exercise OS lease release across actual abrupt process termination."""
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix="rainstar-recovery-crash-") as root:
    crashed = subprocess.run([sys.argv[1], "--crash", root], timeout=30)
    if crashed.returncode != 23:
        raise RuntimeError("Recovery fixture did not reach its abrupt-exit checkpoint")
    subprocess.run([sys.argv[1], "--after-crash", root], check=True, timeout=30)
