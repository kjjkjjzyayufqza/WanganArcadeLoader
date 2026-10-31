param(
    [string]$ToolchainRoot = (Join-Path $env:SystemDrive 'msys64\mingw64'),
    [ValidateRange(1, 64)][int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$toolsRoot = Join-Path $repoRoot '.build-tools'
$python = (Get-Command python -ErrorAction Stop).Source
$nasmCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'bin\NASM\nasm.exe'),
    (Join-Path $env:ProgramFiles 'NASM\nasm.exe'),
    (Join-Path $ToolchainRoot 'bin\nasm.exe')
)
$nasmCommand = Get-Command nasm -ErrorAction SilentlyContinue
if ($nasmCommand) { $nasmCandidates = @($nasmCommand.Source) + $nasmCandidates }
$nasm = $nasmCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $nasm) { throw 'NASM is required. Install with: winget install --id NASM.NASM --exact' }
if (-not (Test-Path -LiteralPath (Join-Path $ToolchainRoot 'bin\g++.exe'))) {
    throw "MinGW-w64 GCC was not found under $ToolchainRoot"
}

if (-not (Test-Path -LiteralPath (Join-Path $toolsRoot 'mesonbuild'))) {
    & $python -m pip install --target $toolsRoot 'meson==1.12.1'
    if ($LASTEXITCODE -ne 0) { throw 'Meson installation failed.' }
}

$oldPath = $env:Path
$oldPythonPath = $env:PYTHONPATH
Push-Location $repoRoot
try {
    $env:Path = "$(Join-Path $ToolchainRoot 'bin');$(Split-Path $nasm);$oldPath"
    $env:PYTHONPATH = "$toolsRoot;$oldPythonPath"
    $compilerBin = (Join-Path $ToolchainRoot 'bin').Replace('\', '/')
    $nasmPath = $nasm.Replace('\', '/')
    $nativeFile = @"
[binaries]
c = '$compilerBin/gcc.exe'
cpp = '$compilerBin/g++.exe'
ar = '$compilerBin/ar.exe'
strip = '$compilerBin/strip.exe'
nasm = '$nasmPath'
"@
    $nativePath = Join-Path $repoRoot 'native-mingw.ini'
    if (-not (Test-Path -LiteralPath $nativePath) -or [IO.File]::ReadAllText($nativePath) -ne $nativeFile) {
        [IO.File]::WriteAllText($nativePath, $nativeFile)
    }
    if (-not (Test-Path -LiteralPath 'build\build.ninja')) {
        & $python -m mesonbuild.mesonmain setup build --native-file native-mingw.ini
        if ($LASTEXITCODE -ne 0) { throw 'Meson setup failed.' }
    }
    & $python -m mesonbuild.mesonmain compile -C build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'DLL compilation failed.' }
    New-Item -ItemType Directory -Path 'out' -Force | Out-Null
    Copy-Item -LiteralPath 'build\bngrw.dll' -Destination 'out\bngrw.dll' -Force
    Get-ChildItem -LiteralPath 'dist' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination 'out' -Force
    }
    $dllPath = Join-Path $repoRoot 'out\bngrw.dll'
    $hashAlgorithm = [Security.Cryptography.SHA256]::Create()
    $dllStream = [IO.File]::OpenRead($dllPath)
    try {
        $dllHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash($dllStream)).Replace('-', '').ToLowerInvariant()
        Write-Output "Built: $dllPath"
        Write-Output "SHA256: $dllHash"
    } finally {
        $dllStream.Dispose()
        $hashAlgorithm.Dispose()
    }
} finally {
    Pop-Location
    $env:Path = $oldPath
    $env:PYTHONPATH = $oldPythonPath
}
