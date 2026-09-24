# Run from an x64 MSVC developer PowerShell with VCPKG_ROOT set.
param([string]$Revision = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
Push-Location $repo
try {
    $build = @(& cargo +stable build --release --features vram --message-format=json)
    if ($LASTEXITCODE -ne 0) { throw 'Building hwcodec failed.' }
    $native = $build | ForEach-Object { $_ | ConvertFrom-Json } |
        Where-Object { $_.reason -eq 'build-script-executed' -and (Test-Path (Join-Path $_.out_dir 'hwcodec.lib')) } |
        Select-Object -Last 1
    if (-not $native) { throw 'Native library not found.' }
    $vcpkg = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static'
    $label = if ($Revision) { (& git rev-parse --short $Revision).Trim() } else { 'working' }
    if ($LASTEXITCODE -ne 0) { throw 'Invalid revision.' }
    $outDir = (New-Item -ItemType Directory -Force "target/codec-comparison/$label").FullName
    $failures = 0
    foreach ($test in @('query', 'decode_query')) {
        $file = if ($test -eq 'query') { 'cpp/common/platform/win/win.cpp' } else { 'cpp/ffmpeg_vram/ffmpeg_vram_decode.cpp' }
        $source = if ($Revision) { (& git show "${Revision}:$file") -join "`n" } else { Get-Content -Raw $file }
        if ($Revision -and $LASTEXITCODE -ne 0) { throw "Cannot read $file at $Revision" }
        if ($test -eq 'query') {
            $method = [regex]::Match($source, '(?ms)^bool NativeDevice::Query\(\) \{.*?^\}')
            if (-not $method.Success) { throw 'Cannot locate production Query method.' }
            Set-Content -LiteralPath "$outDir/query.inc" -Value $method.Value -Encoding UTF8
        } else {
            Set-Content -LiteralPath "$outDir/production.inc" -Value $source -Encoding UTF8
        }
        $exe = "$outDir/$test.exe"
        & cl.exe /nologo /EHs /O2 /std:c++17 /MT /DNOMINMAX `
            "/I$outDir" "/I$vcpkg/include" "/I$repo/cpp/common" "/I$repo/cpp/common/platform/win" `
            "/Fo$outDir/$test.obj" "/Fe$exe" "$PSScriptRoot/$test.cpp" `
            /link "/LIBPATH:$vcpkg/lib" (Join-Path $native.out_dir 'hwcodec.lib') `
            avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
            user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
        if ($LASTEXITCODE -ne 0) { throw "Building $test failed." }
        & $exe | Tee-Object -FilePath "$outDir/$test.log"
        if ($LASTEXITCODE -ne 0) { ++$failures }
    }
    if ($failures) { throw "$failures test suites failed; see $outDir" }
} finally { Pop-Location }
