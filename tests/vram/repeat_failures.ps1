# Run from an x64 MSVC developer PowerShell with VCPKG_ROOT set.
# Exercises the production repeat methods with scripted FFmpeg return values;
# this does not simulate GPU device loss or driver recovery.
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from an x64 MSVC developer PowerShell.'
}
if (-not $env:VCPKG_ROOT) {
    throw 'Set VCPKG_ROOT to the vcpkg installation used to build hwcodec.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $repo
try {
    $build = @(& cargo +stable build --release --features vram --message-format=json)
    if ($LASTEXITCODE -ne 0) { throw 'Building hwcodec failed.' }
    $native = $build | ForEach-Object { $_ | ConvertFrom-Json } |
        Where-Object {
            $_.reason -eq 'build-script-executed' -and
            (Test-Path (Join-Path $_.out_dir 'hwcodec.lib'))
        } | Select-Object -Last 1
    if (-not $native) { throw 'Cargo did not report the hwcodec native library.' }

    $outDir = (New-Item -ItemType Directory -Force 'target/repeat-failures').FullName
    $vcpkg = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static'
    foreach ($test in @('repeat_failures', 'encoder_cleanup', 'decoder_cleanup', 'mfx_exceptions')) {
        $testExe = Join-Path $outDir "$test.exe"
        $source = if ($test -in @('encoder_cleanup', 'decoder_cleanup')) { 'cleanup_failures.cpp' } else { "$test.cpp" }
        $defines = @(if ($test -eq 'encoder_cleanup') { '/DTEST_ENCODER' })
        & cl.exe /nologo /EHs /O2 /std:c++17 /MT /DNOMINMAX @defines `
            "/I$vcpkg/include" "/I$repo/cpp/common" `
            "/I$repo/externals/MediaSDK_22.5.4/api/include" `
            "/Fo$outDir/$test.obj" "/Fe$testExe" `
            "$PSScriptRoot/$source" /link "/LIBPATH:$vcpkg/lib" `
            (Join-Path $native.out_dir 'hwcodec.lib') `
            avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
            user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
        if ($LASTEXITCODE -ne 0) { throw "Building $test failed." }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw "$test failed." }
        if ($test -ne 'repeat_failures') {
            foreach ($mode in @(3, 4)) {
                # The injected 0xE0424242 exception and STATUS_ACCESS_VIOLATION.
                $expectedExit = if ($mode -eq 3) { -532528574 } else { -1073741819 }
                $sites = if ($test -eq 'mfx_exceptions') { @('probe') } else { @('init', 'cleanup') }
                foreach ($site in $sites) {
                    $testArgs = switch ($site) {
                        'probe' { @($mode) }
                        'init' { @($mode, 0) }
                        'cleanup' { @(0, $mode) }
                    }
                    & $testExe @testArgs
                    if ($LASTEXITCODE -ne $expectedExit) {
                        throw "$test $site mode=$mode returned $LASTEXITCODE instead of SEH exit $expectedExit."
                    }
                    Write-Output "PASS $test $site mode=$mode remains unhandled (exit $LASTEXITCODE)"
                }
            }
        }
    }
    & "$PSScriptRoot/../ffmpeg_io/run.ps1"
} finally {
    Pop-Location
}
