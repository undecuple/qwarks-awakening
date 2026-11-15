[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Eboot,

    [ValidatePattern("^[a-z0-9_]+$")]
    [string]$Game = "tods",

    [string]$Disc = "",
    [string]$Build = "",
    [string]$Stage = "",

    [ValidateRange(0, 64)]
    [int]$Jobs = 0,

    [ValidateSet("RelWithDebInfo", "Release")]
    [string]$Configuration = "RelWithDebInfo",

    [switch]$SkipToolchainDownload
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot

function Resolve-ExistingPath([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-Checked([string]$Command, [string[]]$Arguments, [string]$Failure) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Failure (exit $LASTEXITCODE)"
    }
}

$PythonCommand = $null
$PythonPrefix = @()
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)"
    if ($LASTEXITCODE -eq 0) {
        $PythonCommand = "py"
        $PythonPrefix = @("-3")
    }
}
if (-not $PythonCommand -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)"
    if ($LASTEXITCODE -eq 0) {
        $PythonCommand = "python"
    }
}
if (-not $PythonCommand) {
    throw "Python 3.10 or newer was not found. Install native 64-bit Python for Windows and run this script again."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git for Windows was not found. Install it and run this script again."
}

$EbootPath = Resolve-ExistingPath $Eboot "EBOOT"
$DiscPath = if ($Disc) { Resolve-ExistingPath $Disc "Game files" } else { "" }

$ToolkitSource = Join-Path $Root "third_party\ps3recomp\CMakeLists.txt"
if (-not (Test-Path -LiteralPath $ToolkitSource -PathType Leaf)) {
    Invoke-Checked "git" @("-C", $Root, "submodule", "update", "--init", "--depth", "1", "third_party/ps3recomp") `
        "Could not initialize the pinned ps3recomp submodule"
}

$Python = @($PythonPrefix) + @("-X", "utf8")
$Cache = Join-Path $Root "work\release\toolchain-cache"
$Trees = Join-Path $Cache "trees"
if (-not $SkipToolchainDownload) {
    $Fetch = Join-Path $Root "tools\release\fetch_toolchain.py"
    Invoke-Checked $PythonCommand ($Python + @($Fetch, "--cache", $Cache, "--only", "llvm-mingw,sdl3,cmake,ninja")) `
        "Could not prepare the pinned native Windows toolchain"
}

$RequiredTools = @(
    (Join-Path $Trees "llvm-mingw\bin\x86_64-w64-mingw32-clang.exe"),
    (Join-Path $Trees "cmake\bin\cmake.exe"),
    (Join-Path $Trees "ninja\ninja.exe"),
    (Join-Path $Trees "sdl3\x86_64-w64-mingw32\bin\SDL3.dll")
)
foreach ($Tool in $RequiredTools) {
    if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) {
        throw "The pinned native Windows toolchain is incomplete. Run without -SkipToolchainDownload first."
    }
}

$FirstRun = Join-Path $Root "tools\release\firstrun.py"
$Arguments = $Python + @(
    $FirstRun,
    "--root", $Root,
    "--game", $Game,
    "--eboot", $EbootPath,
    "--toolchain", $Trees,
    "--config", $Configuration
)
if ($DiscPath) {
    $Arguments += @("--disc", $DiscPath)
}
if ($Build) {
    $Arguments += @("--build", [IO.Path]::GetFullPath($Build))
}
if ($Stage) {
    $Arguments += @("--stage", [IO.Path]::GetFullPath($Stage))
}
if ($Jobs -gt 0) {
    $Arguments += @("--jobs", [string]$Jobs)
}

Write-Host "Building $Game with the pinned native Windows toolchain."
& $PythonCommand @Arguments
exit $LASTEXITCODE
