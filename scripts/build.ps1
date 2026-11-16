param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$CompilerBin,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$WorkRoot = (Join-Path $env:TEMP ('EDA-Shiyan-' + [guid]::NewGuid().ToString('N'))),
    [ValidateSet('MinGW','MSVC')][string]$Toolchain = 'MinGW',
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
if ($CompilerBin) { $CompilerBin = (Resolve-Path -LiteralPath $CompilerBin).Path }
$CMake = (Get-Command $CMake -ErrorAction Stop).Source
$CTest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
$Ninja = (Get-Command $Ninja -ErrorAction Stop).Source
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
if ($WorkRoot -match '[^\x00-\x7F]') { throw 'WorkRoot must use an ASCII path for this Windows Qt kit.' }
if (Test-Path -LiteralPath $WorkRoot) { throw 'Choose a new WorkRoot; this script never overwrites an existing directory.' }
New-Item -ItemType Directory -Path $WorkRoot | Out-Null
foreach ($entry in @('core','tests','examples','app','render','feat_fp','feat_pcb','CMakeLists.txt')) {
    $item = Join-Path $sourceRoot $entry
    if (Test-Path -LiteralPath $item) { Copy-Item -LiteralPath $item -Destination $WorkRoot -Recurse }
}
$oldPath = $env:PATH
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
try {
    $env:PATH = (@("$QtRoot\bin", $CompilerBin, $oldPath) | Where-Object { $_ }) -join ';'
    if ($Toolchain -eq 'MSVC' -and (!$env:VCToolsInstallDir -or !$env:INCLUDE -or !$env:LIB)) {
        throw 'Run the MSVC build from the Visual Studio x64 Developer PowerShell.'
    }
    $compilerName = if ($Toolchain -eq 'MSVC') { 'cl.exe' } else { 'g++.exe' }
    $compiler = (Get-Command $compilerName -CommandType Application -ErrorAction Stop).Source
    $compilerOptions = @()
    if ($Toolchain -eq 'MSVC') {
        $compilerOptions = @('-DCMAKE_POLICY_DEFAULT_CMP0141=NEW', '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded')
    }
    & $CMake -S $WorkRoot -B "$WorkRoot/build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON @compilerOptions
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    & $CMake --build "$WorkRoot/build" --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    $testTemp = Join-Path $WorkRoot 'test-temp'
    New-Item -ItemType Directory -Path $testTemp | Out-Null
    $env:TEMP = $testTemp
    $env:TMP = $testTemp
    & $CTest --test-dir "$WorkRoot/build" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed: $LASTEXITCODE" }
    Write-Output "Build and tests completed: $WorkRoot"
} finally {
    $env:PATH = $oldPath
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
}
