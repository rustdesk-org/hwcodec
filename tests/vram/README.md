Run the VRAM API tests on Windows with supported hardware encoders and decoders:

```powershell
cargo +stable test --release --features vram --lib vram::tests -- --include-ignored --nocapture --test-threads=1
```

The repeat tests cover bitrate changes, encoder recreation for resolution and
H.264/H.265 changes, timestamps, input invalidation, and 120 normal/repeat cycles
per available GPU/codec combination. They decode the outputs to check dimensions
and validity. They do not validate pixel accuracy, live display changes, network
reconnects, GPU device resets, or long-running memory usage.

Run deterministic failure tests from an **x64 MSVC developer PowerShell**, with
`VCPKG_ROOT` pointing to the installation used by hwcodec:

```powershell
./tests/vram/repeat_failures.ps1
```

This compiles the production C++ encoder implementation into a separate test
executable, substituting only `avcodec_send_frame` and `avcodec_receive_packet`.
It checks repeat failure/retry behavior for `EAGAIN`, I/O errors, end-of-stream,
empty packets, output draining without a callback, and normal-input invalidation.
The fixture starts with a cached input marked ready and uses no GPU. Successful
retries after a scripted error verify the API state; they do not imply a real
failed FFmpeg context or lost GPU device can recover without recreation.
