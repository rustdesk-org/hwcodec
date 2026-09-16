Run the VRAM API tests on Windows with supported hardware encoders and decoders:

```powershell
cargo +stable test --release --features vram --lib vram::tests -- --include-ignored --nocapture --test-threads=1
```

The repeat tests cover bitrate changes, encoder recreation for resolution and
H.264/H.265 changes, timestamps, input invalidation, and 120 normal/repeat cycles
per available GPU/codec combination. They decode the outputs to check dimensions
and validity. A content test encodes a four-quadrant image A, overwrites and
releases the source texture, and checks that repeat still produces A. It then
encodes B and repeats the overwrite/release checks for B. Each decoded image is
sampled at nine points per quadrant with a tolerance of 16 per B/G/R channel
for compression and color conversion; source uploads are checked without tolerance.
These tests do not validate live display changes, network reconnects, GPU device
resets, or long-running memory usage.

Run deterministic failure tests from an **x64 MSVC developer PowerShell**, with
`VCPKG_ROOT` pointing to the installation used by hwcodec:

```powershell
./tests/vram/repeat_failures.ps1
```

This compiles the production C++ encoder implementation into a separate test
executable, substituting only `avcodec_send_frame` and `avcodec_receive_packet`.
It checks repeat failure/retry behavior for `EAGAIN`, I/O errors, end-of-stream,
empty packets, output draining without a callback, and normal-input invalidation.
It also queues accepted input timestamps to check delayed output across repeat
calls, and verifies that changing the frame rate preserves the millisecond time base.
The fixture starts with a cached input marked ready and uses a WARP software
device for the device-status check, requiring no hardware GPU. Successful
retries after a scripted error verify the API state; they do not imply a real
failed FFmpeg context or lost GPU device can recover without recreation.

The same script runs encoder and decoder ownership tests with initialization and
cleanup failures, including standard/non-standard C++ exceptions and throwing
error logs. They also check that a throwing release does not skip the remaining
frames, packets, codec context, or hardware buffer. These tests do not prove that
a failing driver releases the resource whose release operation failed.

The tests use `/EHs`, matching the production exception model. A separate fixture
links the production MFX probe with throwing driver calls to verify C++ exception
handling and session cleanup. Windows SEH/access violations run in child test
processes; the script checks their exact exception exit codes to ensure neither
the codec initialization/cleanup paths nor the driver probe swallow these faults.

## Windows CI

The [Windows VRAM failure injection workflow](../../.github/workflows/windows-vram.yml)
runs this script on `windows-2022` for pull requests, pushes to `master`, and manual
dispatches. It uses WARP and does not run the hardware-dependent Rust tests above.
The job builds FFmpeg 7.1.1 and the Intel dispatcher using pinned vcpkg and RustDesk
overlay ports, caches dependency binaries, and uploads test/build logs. The pinned
versions are in the workflow and its [dependency manifest](../../.github/vcpkg/vcpkg.json);
update the workflow's vcpkg commit and the manifest's baseline together.
