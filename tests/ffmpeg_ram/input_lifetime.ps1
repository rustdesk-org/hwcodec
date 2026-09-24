# Run from an x64 MSVC developer PowerShell with VCPKG_ROOT set.
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from an x64 MSVC developer PowerShell.'
}
if (-not $env:VCPKG_ROOT) {
    throw 'Set VCPKG_ROOT to the existing vcpkg installation used by hwcodec.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $repo
try {
    $build = @(& cargo +stable build --release --message-format=json)
    if ($LASTEXITCODE -ne 0) { throw 'Building hwcodec failed.' }
    $native = $build | ForEach-Object { $_ | ConvertFrom-Json } |
        Where-Object {
            $_.reason -eq 'build-script-executed' -and
            (Test-Path (Join-Path $_.out_dir 'hwcodec.lib'))
        } | Select-Object -Last 1
    if (-not $native) { throw 'Cargo did not report the hwcodec native library.' }

    $outDir = (New-Item -ItemType Directory -Force 'target/ram-input-lifetime').FullName
    $vcpkg = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static'
    $testExe = Join-Path $outDir 'input_lifetime.exe'
    & cl.exe /nologo /EHs /O2 /std:c++17 /MT /DNOMINMAX `
        "/I$vcpkg/include" "/I$repo/cpp/common" "/I$repo/cpp/common/platform/win" `
        "/Fo$outDir/input_lifetime.obj" "/Fe$testExe" `
        "$PSScriptRoot/input_lifetime.cpp" /link "/LIBPATH:$vcpkg/lib" `
        (Join-Path $native.out_dir 'hwcodec.lib') `
        avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
        user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Building RAM input lifetime tests failed.' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'RAM input lifetime tests failed.' }
} finally {
    Pop-Location
}
