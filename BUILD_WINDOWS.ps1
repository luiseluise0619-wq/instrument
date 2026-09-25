param(
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [switch]$TestsOnly
)
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "CMake 3.24+ must be installed and available in PATH."
    }
    if ($TestsOnly) {
        & cmake -S . -B $BuildDir -DSLYCE_TESTS_ONLY=ON -DSLYCE_BUILD_ENGINE_TESTS=ON -DSLYCE_TEST_LOCAL_SHIM=ON
    } else {
        & cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64
    }
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    & cmake --build $BuildDir --config $Configuration --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed. See compiler diagnostics above." }
    if ($TestsOnly) {
        & ctest --test-dir $BuildDir -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    } else {
        Write-Host "Build completed: $BuildDir/VocalChopStudio_artefacts/$Configuration"
    }
} finally { Pop-Location }
