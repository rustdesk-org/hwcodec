Run `./tests/ffmpeg_io/run.ps1` from an x64 MSVC developer PowerShell with
`VCPKG_ROOT` set. The eight polling checks compile the exact production
`NativeDevice::Query()` body with scripted GetData results, a fake steady clock
and a 16 ms Sleep(1). They cover completion, errors and elapsed-time bounds.

The three VRAM decoder checks use a real WARP NV12 texture, scripted FFmpeg I/O,
and injected conversion/completion results. They check the public FFI return
and callbacks on success, Query failure, and failure after a previous output.
These are deterministic comparisons, not measured GPU-hang experiments.

Use `-Revision <commit>` to run the same fixture against that revision's source.
Historical revisions with the bugs return nonzero. Per-case results are saved
under `target/codec-comparison/<revision>/`.
