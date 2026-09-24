Run from an x64 MSVC developer PowerShell with `VCPKG_ROOT` pointing to an
existing static FFmpeg installation with QSV enabled:

```powershell
./tests/ffmpeg_ram/input_lifetime.ps1
```

The test compiles the production RAM encoder with codec opening and packet I/O
replaced. It uses real FFmpeg frame allocation, reference counting and copying;
no GPU is required and FFmpeg is not rebuilt. The fake encoder retains frame
references across calls to force copy-on-write.

For both NV12 and YUV420P, it verifies pixel content after overwriting and freeing
the caller's input, after subsequent inputs, and after encoder cleanup. It checks
normal alignment and a 256-byte input stride that differs from the reallocated
frame's stride. Truncated input must fail without submitting a frame.

These tests verify buffer ownership and layout, not hardware driver behavior.
