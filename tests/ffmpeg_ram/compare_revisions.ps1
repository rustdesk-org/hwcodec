param([string]$Revision = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
Push-Location $repo
try {
    $build = @(& cargo +stable build --release --message-format=json)
    if ($LASTEXITCODE -ne 0) { throw 'Building hwcodec failed.' }
    $native = $build | ForEach-Object { $_ | ConvertFrom-Json } |
        Where-Object { $_.reason -eq 'build-script-executed' -and (Test-Path (Join-Path $_.out_dir 'hwcodec.lib')) } |
        Select-Object -Last 1
    if (-not $native) { throw 'Native library not found.' }
    $label = if ($Revision) { (& git rev-parse --short $Revision).Trim() } else { 'working' }
    $outDir = (New-Item -ItemType Directory -Force "target/ram-comparison/$label").FullName
    $file = 'cpp/ffmpeg_ram/ffmpeg_ram_encode.cpp'
    $source = if ($Revision) { (& git show "${Revision}:$file") -join "`n" } else { Get-Content -Raw $file }
    if ($Revision -and $LASTEXITCODE -ne 0) { throw 'Cannot read revision.' }
    Set-Content -LiteralPath "$outDir/production.inc" -Value $source -Encoding UTF8
    $vcpkg = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static'
    $exe = "$outDir/compare.exe"
    & cl.exe /nologo /EHs /O2 /std:c++17 /MT /DNOMINMAX `
        "/I$outDir" "/I$vcpkg/include" "/I$repo/cpp/common" "/I$repo/cpp/common/platform/win" `
        "/Fo$outDir/compare.obj" "/Fe$exe" "$PSScriptRoot/compare_revisions.cpp" `
        /link "/LIBPATH:$vcpkg/lib" (Join-Path $native.out_dir 'hwcodec.lib') `
        avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
        user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Building comparison failed.' }
    & $exe | Tee-Object -FilePath "$outDir/result.log"
    $failed = $LASTEXITCODE -ne 0
    & $exe null | Tee-Object -FilePath "$outDir/null.log"
    $nullExit = $LASTEXITCODE
    "null-input exit=$nullExit" | Tee-Object -FilePath "$outDir/null.log" -Append
    if ($failed -or $nullExit -ne 0) { throw "Regression found; see $outDir" }
} finally { Pop-Location }
