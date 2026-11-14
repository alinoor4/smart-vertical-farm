param([switch]$Test, [switch]$Clean, [string]$Config = 'Release', [string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Tools installed by winget are invisible to shells that were already open: rebuild PATH from the registry.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $probe = @('D:\Tools\winlibs\mingw64\bin') + @(Get-Item "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*\mingw64\bin" -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $bin = $probe | Where-Object { Test-Path (Join-Path $_ 'g++.exe') } | Select-Object -First 1
    if (-not $bin) { throw 'g++ not found. Install it: winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --location D:\Tools\winlibs' }
    $env:Path = "$bin;$env:Path"
}
# WinLibs' CMake fetches over HTTPS with GnuTLS and ships no CA list, so every download fails verification.
# It ignores the CMAKE_TLS_CAINFO environment variable but honours the cache variable: borrow Git for Windows' bundle.
$tls = @()
$git = Get-Command git -ErrorAction SilentlyContinue
$ca = if ($git) { Join-Path (Split-Path (Split-Path $git.Source)) 'mingw64\etc\ssl\certs\ca-bundle.crt' }
if ($ca -and (Test-Path $ca)) { $tls = @('-DCMAKE_TLS_CAINFO=' + $ca.Replace('\', '/')) }   # backslashes break the generated download script
if ($Clean) { Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue }
cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" @tls
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Test) { ctest --test-dir $BuildDir --output-on-failure; exit $LASTEXITCODE }
