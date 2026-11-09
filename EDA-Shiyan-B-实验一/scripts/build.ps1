param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$CompilerBin,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$WorkRoot = (Join-Path $env:TEMP ('EDA-Shiyan-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$CompilerBin = (Resolve-Path -LiteralPath $CompilerBin).Path
$CMake = (Get-Command $CMake -ErrorAction Stop).Source
$Ninja = (Get-Command $Ninja -ErrorAction Stop).Source
$CTest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $CTest)) { throw "ctest.exe not found beside CMake: $CTest" }
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
if ($WorkRoot -match '[^\x00-\x7F]') { throw 'WorkRoot must use an ASCII path for this Windows Qt kit.' }
if (Test-Path -LiteralPath $WorkRoot) { throw 'Choose a new WorkRoot; this script never overwrites an existing directory.' }
New-Item -ItemType Directory -Path $WorkRoot | Out-Null
foreach ($entry in @('core','tests','examples','app','render','feat_fp','feat_pcb','CMakeLists.txt')) {
    $item = Join-Path $sourceRoot $entry
    if (Test-Path -LiteralPath $item) { Copy-Item -LiteralPath $item -Destination $WorkRoot -Recurse }
}
$oldPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$CompilerBin;$oldPath"
    & $CMake -S $WorkRoot -B "$WorkRoot/build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Debug
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    & $CMake --build "$WorkRoot/build" --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Push-Location "$WorkRoot/build"
    try {
        $oldQtPlatform = $env:QT_QPA_PLATFORM
        try {
            $env:QT_QPA_PLATFORM = 'offscreen'
            & $CTest --test-dir "$WorkRoot/build" --output-on-failure --timeout 30
            if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }
        } finally {
            $env:QT_QPA_PLATFORM = $oldQtPlatform
        }
        & ./eda_core_demo.exe "$WorkRoot/demo-library"
        if ($LASTEXITCODE -ne 0) { throw 'Demo failed' }
    } finally { Pop-Location }
    Write-Output "Verified build and demo: $WorkRoot"
} finally { $env:PATH = $oldPath }
