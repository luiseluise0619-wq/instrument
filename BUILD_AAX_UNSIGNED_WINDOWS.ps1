param(
    [string]$AaxSdkPath = $env:AAX_SDK_PATH,
    [string]$BuildDir = "build-aax-windows"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($AaxSdkPath) -or -not (Test-Path -LiteralPath $AaxSdkPath)) {
    throw "AAX SDK path is required. Example: .\BUILD_AAX_UNSIGNED_WINDOWS.ps1 -AaxSdkPath 'C:\SDK\AAX_SDK'"
}

$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = "cmake" }

& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot $BuildDir) `
    -G "Visual Studio 17 2022" -A x64 `
    -DSLYCE_AAX_SDK_PATH="$AaxSdkPath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& $cmake --build (Join-Path $PSScriptRoot $BuildDir) --config Release --target VocalChopStudio_AAX
if ($LASTEXITCODE -ne 0) { throw "AAX build failed" }

Write-Host "Unsigned developer AAX build completed. Use only with a compatible developer/test Pro Tools build."
