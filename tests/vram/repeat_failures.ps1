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
    $testExe = Join-Path $outDir 'repeat_failures.exe'
    & cl.exe /nologo /EHsc /std:c++17 /MT /DNOMINMAX `
        "/I$vcpkg/include" "/I$repo/cpp/common" `
        "/Fo$outDir/repeat_failures.obj" "/Fe$testExe" `
        "$PSScriptRoot/repeat_failures.cpp" /link "/LIBPATH:$vcpkg/lib" `
        (Join-Path $native.out_dir 'hwcodec.lib') `
        avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
        user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Building repeat failure tests failed.' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Repeat failure tests failed.' }
} finally {
    Pop-Location
}
