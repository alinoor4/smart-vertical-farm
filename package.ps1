# Builds dist\SmartVerticalFarm\ and zips it (plan 19, M12): the two programs, data\, bench\results\, a short README.txt,
# LICENSE and THIRD_PARTY_NOTICES.txt. dist\ is ignored by git. Usage: .\package.ps1 [-BuildDir build]
param([string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& .\build.ps1 -BuildDir $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$dist = Join-Path $PSScriptRoot 'dist\SmartVerticalFarm'
$zip = "$dist.zip"
Remove-Item -Recurse -Force $dist, $zip -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$dist\bench" | Out-Null
Copy-Item "$BuildDir\farm_app.exe", "$BuildDir\farm_cli.exe", 'LICENSE' $dist
Copy-Item 'data' $dist -Recurse
Copy-Item 'bench\results' "$dist\bench" -Recurse

# Text files for Windows: CRLF line ends, and a byte-order mark so that Notepad reads the non-ASCII letter in GLFW's licence.
$utf8 = New-Object System.Text.UTF8Encoding $true
function Save-Text($name, $lines) {
    [IO.File]::WriteAllText("$dist\$name", (($lines -join "`n") -replace "`n", "`r`n") + "`r`n", $utf8)
}

$readme = @'
Smart Vertical Farm
===================

A planning tool for an indoor vertical farm, written in C++17 for a course project (Design and Analysis of Algorithms).
Given a farm (crops, shelves, weekly budgets, delivery contracts, order history) it decides what to plant, where every
tray goes, and when to harvest and replant. Every algorithm is written in this project.

In this folder
  farm_app.exe              the desktop app: double-click it
  farm_cli.exe              the command-line tool: run it in a terminal
  data\                     the sample farms (default_scenario.json, flat_scenario.json)
  bench\results\            the benchmark results: ten CSV files and summary.md
  LICENSE                   the licence of this project (MIT)
  THIRD_PARTY_NOTICES.txt   the licences of the libraries inside the two programs

Nothing needs to be installed. The programs run on 64-bit Windows 10 or 11; the app needs a graphics driver with
OpenGL 3.0. Keep data\ and bench\results\ beside the programs: the app looks for them there.

To try the command-line tool, open a terminal in this folder:
  .\farm_cli.exe run --scenario data\default_scenario.json --packer ga
  .\farm_cli.exe compare --scenario data\flat_scenario.json
  .\farm_cli.exe validate --scenario data\default_scenario.json
  .\farm_cli.exe bench --quick --out quick_results
Run .\farm_cli.exe with no arguments to see every command and option.

The source code, the build instructions, the plan and the complexity analysis are in the project's git repository.
'@
Save-Text 'README.txt' ($readme -split "`r?`n")

# The licence texts are copied from the sources that the build downloaded, so they belong to the pinned versions.
# Keep the versions in step with the table in README.md and with CMakeLists.txt.
$libraries = @(
    @('Dear ImGui 1.93.0, https://github.com/ocornut/imgui (the user interface)', 'imgui-src\LICENSE.txt'),
    @('ImPlot 1.1, https://github.com/epezent/implot (the charts)', 'implot-src\LICENSE'),
    @('GLFW 3.5.1, https://github.com/glfw/glfw (the window and the OpenGL context)', 'glfw-src\LICENSE.md'),
    @('nlohmann/json 3.12.0, https://github.com/nlohmann/json (reading and writing JSON files)', 'json-src\LICENSE.MIT'))
$rule = '-' * 78
$notices = @('THIRD-PARTY NOTICES', '',
    'farm_app.exe contains Dear ImGui, ImPlot, GLFW, stb_image_write and nlohmann/json; farm_cli.exe contains nlohmann/json.',
    'The licence texts below are copied unchanged from the sources that the build downloads into .deps\.', '')
foreach ($library in $libraries) {
    $notices += $rule, $library[0], $rule
    $notices += Get-Content -Encoding UTF8 ".deps\$($library[1])"
    $notices += ''
}
$stb = Get-Content -Encoding UTF8 '.deps\stb-src\stb_image_write.h'
$at = [array]::IndexOf($stb, 'This software is available under 2 licenses -- choose whichever you prefer.')
if ($at -lt 1) { throw 'The licence text at the end of stb_image_write.h was not found.' }
$notices += $rule, 'stb_image_write 1.16, https://github.com/nothings/stb (saving PNG screenshots); use it under either licence', $rule
$notices += $stb[($at - 1)..($stb.Count - 2)]   # from the line of dashes above the sentence to the one before the closing */
$notices += '', $rule, 'Also inside the programs', $rule,
    'Dear ImGui embeds the fonts ProggyClean (Tristan Grimmer) and ProggyForever (Disco Hello, Tristan Grimmer), both under the MIT licence; see docs\FONTS.md in the Dear ImGui sources.',
    'The programs are linked statically with the C and C++ runtime libraries of GCC and MinGW-w64, which have their own licences (the GCC Runtime Library Exception for libstdc++ and libgcc, permissive licences for the MinGW-w64 runtime).'
Save-Text 'THIRD_PARTY_NOTICES.txt' $notices

# The programs must run on a machine without MinGW: start them from an empty folder, with a PATH that holds only the Windows folders.
$path = $env:Path
$work = Join-Path $env:TEMP ('farm_package_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
Push-Location $work
try {
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    & "$dist\farm_cli.exe" validate --scenario "$dist\data\default_scenario.json" | Out-Null
    if ($LASTEXITCODE) { throw 'farm_cli.exe does not run without MinGW.' }
    if ((Start-Process "$dist\farm_app.exe" -ArgumentList '--selftest' -Wait -PassThru).ExitCode) { throw 'farm_app.exe --selftest failed without MinGW.' }
}
finally {
    Pop-Location
    $env:Path = $path
    Remove-Item -Recurse -Force $work
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($dist, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)   # $true: every path starts with SmartVerticalFarm\
'{0}  ({1:N1} MB)' -f $zip, ((Get-Item $zip).Length / 1MB)
