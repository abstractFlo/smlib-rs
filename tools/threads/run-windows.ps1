# SMLib in several threads on Windows: harness.cpp built with MSVC against the static library
# smlib-sys builds (smlib.lib), every result compared bit for bit with a serial run, and the speedup.
# The Windows side of `tools/threads/run.sh release`; ThreadSanitizer and the globals check run on
# Linux only.
#
#   powershell -ExecutionPolicy Bypass -File tools\threads\run-windows.ps1 [-Threads 1,2,4,8] [-Passes 2]
param([string]$Threads = "1,2,4,8", [int]$Passes = 2)
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Out = Join-Path $Root "target\threads"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

cargo build --quiet --release -p smlib-sys --manifest-path (Join-Path $Root "Cargo.toml")
if ($LASTEXITCODE -ne 0) { throw "smlib-sys did not build" }
$Library = Get-ChildItem -Recurse -Path (Join-Path $Root "target\release\build") -Filter "smlib.lib" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Library) { throw "no smlib.lib under target\release\build" }
Write-Host "library: $($Library.FullName)"

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$installer\vswhere.exe" -latest -products * -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

# as smlib-sys builds SMLib on Windows: release defines, Unicode TCHAR, SM_API_EXPORTS for a static library
$flags = "/nologo /std:c++17 /EHsc /MD /O2 /utf-8 /bigobj /w /DNDEBUG /DNOMINMAX /DSM_GRAPHICS_CALLBACKS=1 /DSM_API_EXPORTS /DUNICODE /D_UNICODE"
$includes = "/I`"$Root\source\SM_API\inc`" /I`"$Root\source\SMLib\inc`""
$exe = Join-Path $Out "harness.exe"
$compile = "call `"$vcvars`" >nul && cl $flags $includes `"$Root\tools\threads\harness.cpp`" /Fo`"$Out\\`" /Fe`"$exe`" /link `"$($Library.FullName)`""
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "harness.cpp did not build" }

& $exe --threads $Threads --passes $Passes
exit $LASTEXITCODE
