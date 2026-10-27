# Lagging tools

Archived stroke-stabilization experiments; excluded from the shipping app.
Includes the relational state tracker, momentum damping and sparse cubic
reconstruction. Conventional distance-lag stabilization remains in Paint.

The complete integration patch applies to base commit `00aef2038842c4d4f184c8629d65364794def501`.
The snapshot contains every modified file plus the untracked numerical test
and model notes. Restore in a separate checkout, apply `integration.patch`,
and copy `snapshot/tests/state_tracker_tests.cpp` and
`snapshot/docs/STATE_TRACKER.md` to their corresponding paths.

Local trial apps remain in the user's Applications folder. No production
acceptance or benefit from injected dither is claimed.
