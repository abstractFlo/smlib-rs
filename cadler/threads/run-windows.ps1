# SMLib in several threads on Windows: harness.cpp built with MSVC against the release libraries
# (SM_API and SMLib in _build), every result compared bit for bit with a serial run, and the speedup.
# The Windows side of `cadler/threads/run.sh release`; ThreadSanitizer and the globals check run on
# Linux only.
#
#   powershell -ExecutionPolicy Bypass -File cadler\threads\run-windows.ps1 [-Threads 1,2,4,8] [-Passes 2]
param([string]$Threads = "1,2,4,8", [int]$Passes = 2)
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Build = Join-Path $Root "_build"
$Out = Join-Path $Build "threads"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

function Find-Built([string]$Name) {
    $found = Get-ChildItem -Recurse -Path $Build -Filter $Name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "release" -and $_.FullName -notmatch "\\threads\\" } | Select-Object -First 1
    if (-not $found) { throw "no $Name under $Build" }
    return $found.FullName
}
$Dll = Find-Built "SM_API.dll"
$ApiLib = Find-Built "SM_API.lib"
$SmLib = Find-Built "SMLib.lib"
Write-Host "libraries: $Dll, $ApiLib, $SmLib"

# MSVC from the newest Visual Studio; the libraries' v142 and its v143 share one ABI
$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$installer\vswhere.exe" -latest -products * -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

# as premake builds SMLib on Windows: release defines, Unicode TCHAR (premake's default character set)
$flags = "/nologo /std:c++17 /EHsc /MD /O2 /utf-8 /bigobj /w /DNDEBUG /DNOMINMAX /DSM_GRAPHICS_CALLBACKS=1 /DUNICODE /D_UNICODE"
$includes = "/I`"$Root\source\SM_API\inc`" /I`"$Root\source\SMLib\inc`""
$exe = Join-Path $Out "harness.exe"
$compile = "call `"$vcvars`" >nul && cl $flags $includes `"$Root\cadler\threads\harness.cpp`" /Fo`"$Out\\`" /Fe`"$exe`" /link `"$ApiLib`" `"$SmLib`""
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "harness.cpp did not build" }

# SM_API.dll, SMLib.dll and what they load sit in one folder
$env:PATH = (Split-Path $Dll) + ";" + $env:PATH
& $exe --threads $Threads --passes $Passes
exit $LASTEXITCODE
