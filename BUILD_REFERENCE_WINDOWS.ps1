param(
    [string]$JuceSourceDir = "",
    [string]$SignalsmithStretchSourceDir = "",
    [string]$SignalsmithLinearSourceDir = "",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [switch]$Snapshots
)
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "CMake 3.24+ is required." }
    $Processor = Get-Content -Raw "Source/PluginProcessor.cpp"
    if (-not $Processor.Contains("new SlyceReferenceEditor")) { throw "Wrong source folder: reference editor routing is missing." }
    # A NEW, named build directory avoids picking up the previous editor's .sln/.vcxproj.
    # Do not delete or overwrite the user's existing build or installed VST3.
    $BuildDir = Join-Path $PSScriptRoot "build-reference-ui"
    $Options = @("-S", ".", "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DSLYCE_TESTS_ONLY=OFF")
    if ($JuceSourceDir) { $Options += "-DJUCE_SOURCE_DIR=$JuceSourceDir" }
    if ($SignalsmithStretchSourceDir) { $Options += "-DSIGNALSMITH_STRETCH_SOURCE_DIR=$SignalsmithStretchSourceDir" }
    if ($SignalsmithLinearSourceDir) { $Options += "-DSIGNALSMITH_LINEAR_SOURCE_DIR=$SignalsmithLinearSourceDir" }
    $Options += "-DSLYCE_BUILD_TOOLS=$($Snapshots.IsPresent.ToString().ToUpper())"
    & cmake @Options
    if ($LASTEXITCODE -ne 0) { throw "Configure failed. The old plugin was NOT replaced." }
    & cmake --build $BuildDir --config $Configuration --target VocalChopStudio_Standalone VocalChopStudio_VST3 --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed. Do not use an older binary as the result." }
    $Artefacts = Join-Path $BuildDir "VocalChopStudio_artefacts/$Configuration"
    Write-Host ""
    Write-Host "NEW REFERENCE UI BUILD: REF-20260920-04" -ForegroundColor Cyan
    Write-Host "Standalone: $Artefacts/Standalone/Slyce.exe"
    Write-Host "VST3 bundle: $Artefacts/VST3/Slyce.vst3"
    Write-Host "Open the NEW Standalone executable above first. The ... menu must contain REF-20260920-04."
    Write-Host "For a DAW, close the host, back up the installed Slyce.vst3, replace the ENTIRE bundle, then rescan."
    Write-Host "This script does not silently replace installed plugins."
    if ($Snapshots) {
        & cmake --build $BuildDir --config $Configuration --target SlyceUISnapshot --parallel 4
        if ($LASTEXITCODE -ne 0) { throw "Snapshot tool failed to build." }
        $Tool = Join-Path $BuildDir "SlyceUISnapshot_artefacts/$Configuration/SlyceUISnapshot.exe"
        & $Tool (Join-Path $PSScriptRoot "native-ui-screenshots")
        if ($LASTEXITCODE -ne 0) { throw "Native screenshot generation failed." }
    }
} finally { Pop-Location }
