[CmdletBinding()]
param(
    [string] $SvgPath = '',
    [string] $IcoPath = '',
    [string] $BuildDirectory = '',
    [string] $InnoCompiler = '',
    [switch] $SkipInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SvgPath) {
    $SvgPath = Join-Path $repositoryRoot 'resources\icons\app-icon.svg'
}
if (-not $IcoPath) {
    $IcoPath = Join-Path $repositoryRoot 'resources\platform\windows\radmarky.ico'
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repositoryRoot 'build\release'
}

$SvgPath = [System.IO.Path]::GetFullPath($SvgPath)
$IcoPath = [System.IO.Path]::GetFullPath($IcoPath)
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$releaseDirectory = Join-Path $BuildDirectory 'Release'

if (-not (Test-Path -LiteralPath $SvgPath -PathType Leaf)) {
    throw "SVG not found: '$SvgPath'."
}

$vcpkgRoot = Join-Path $repositoryRoot 'build\vcpkg-tool'
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = $vcpkgRoot
}
if (-not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
    throw "VCPKG_ROOT is not a vcpkg checkout: '$env:VCPKG_ROOT'."
}

$vcpkgInstalled = Join-Path $BuildDirectory 'vcpkg_installed\x64-windows'
$qtBin = Join-Path $vcpkgInstalled 'bin'
$qtPlugins = Join-Path $vcpkgInstalled 'Qt6\plugins'
if (-not (Test-Path -LiteralPath $qtBin -PathType Container)) {
    throw "Qt binaries were not found at '$qtBin'. Configure the release preset first."
}

function Invoke-RepositoryCommand {
    param(
        [Parameter(Mandatory)]
        [scriptblock] $ScriptBlock
    )

    Push-Location $repositoryRoot
    try {
        & $ScriptBlock
    }
    finally {
        Pop-Location
    }
}

Invoke-RepositoryCommand {
    Write-Host 'Configuring the release tree without reinstalling vcpkg packages...'
    cmake --preset release -DVCPKG_MANIFEST_INSTALL=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building update-app-icon...'
    cmake --build --preset release --target radmarky_update_app_icon
    if ($LASTEXITCODE -ne 0) {
        throw "Build of radmarky_update_app_icon failed with exit code $LASTEXITCODE."
    }
}

$tool = Join-Path $releaseDirectory 'update-app-icon.exe'
if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
    throw "update-app-icon.exe was not produced at '$tool'."
}

$env:PATH = "$qtBin;$env:PATH"
$env:QT_PLUGIN_PATH = $qtPlugins
$env:QT_QPA_PLATFORM = 'minimal'

Write-Host "Rasterizing '$SvgPath' -> '$IcoPath'"
& $tool $SvgPath $IcoPath
if ($LASTEXITCODE -ne 0) {
    throw "update-app-icon failed with exit code $LASTEXITCODE."
}

Invoke-RepositoryCommand {
    Write-Host 'Rebuilding radmarky_viewer so the executable embeds the new ICO...'
    cmake --build --preset release --target radmarky_viewer
    if ($LASTEXITCODE -ne 0) {
        throw "Build of radmarky_viewer failed with exit code $LASTEXITCODE."
    }
}

if (-not $SkipInstaller) {
    $installerScript = Join-Path $repositoryRoot 'packaging\windows\build-installer.ps1'
    if (-not (Test-Path -LiteralPath $installerScript -PathType Leaf)) {
        throw "The installer script was not found at '$installerScript'."
    }

    Write-Host 'Rebuilding the Windows installer so shortcuts pick up the new ICO...'
    $installerArguments = @{
        ReleaseDirectory = $releaseDirectory
    }
    if ($InnoCompiler) {
        $installerArguments.InnoCompiler = $InnoCompiler
    }
    & $installerScript @installerArguments
}

Write-Host 'App icon updated in the ICO, executable, and installer.' -ForegroundColor Green
if ($SkipInstaller) {
    Write-Host 'Installer rebuild was skipped. Rerun without -SkipInstaller to update setup.exe.'
}
else {
    Write-Host 'Reinstall from out\installers to refresh the desktop shortcut. If Explorer still shows the old icon, restart it or clear the Windows icon cache.'
}
