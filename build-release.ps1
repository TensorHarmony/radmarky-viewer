[CmdletBinding()]
param(
    [switch] $Reconfigure,
    [switch] $Test,
    [switch] $Launch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'build-release.ps1 supports Windows only.'
}

$repositoryRoot = $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot 'build\release'
$cmakeCache = Join-Path $buildDirectory 'CMakeCache.txt'
$installedDependencies = Join-Path $buildDirectory 'vcpkg_installed\x64-windows'
$defaultVcpkgRoot = Join-Path $repositoryRoot 'build\vcpkg-tool'
$viewerExecutable = Join-Path $buildDirectory 'Release\radmarky_viewer.exe'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found on PATH. Install CMake 3.25 or newer.'
}

if (-not $env:VCPKG_ROOT) {
    $defaultToolchain = Join-Path $defaultVcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path -LiteralPath $defaultToolchain -PathType Leaf)) {
        throw 'VCPKG_ROOT is not set and the repository-local vcpkg checkout was not found.'
    }
    $env:VCPKG_ROOT = $defaultVcpkgRoot
}

$vcpkgToolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
    throw "The vcpkg toolchain was not found at '$vcpkgToolchain'."
}

function Invoke-CMake {
    param([Parameter(Mandatory)][string[]] $Arguments)

    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake failed with exit code $LASTEXITCODE."
    }
}

Push-Location $repositoryRoot
try {
    if ($Reconfigure -or -not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
        if (-not (Test-Path -LiteralPath $installedDependencies -PathType Container)) {
            throw "Prebuilt dependencies are missing from '$installedDependencies'. See docs/local-build.md before configuring to avoid rebuilding ITK and VTK."
        }

        Write-Host 'Configuring the Windows Release preset using existing dependencies...'
        Invoke-CMake -Arguments @(
            '--preset', 'release',
            '-DVCPKG_MANIFEST_INSTALL=OFF'
        )
    }

    Write-Host 'Building RadMarky Viewer (Release)...'
    Invoke-CMake -Arguments @(
        '--build', '--preset', 'release',
        '--target', 'radmarky_viewer'
    )

    if ($Test) {
        Write-Host 'Building Release test targets...'
        Invoke-CMake -Arguments @(
            '--build', '--preset', 'release',
            '--target', 'ALL_BUILD'
        )

        Write-Host 'Running Release tests...'
        & ctest --preset release --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "CTest failed with exit code $LASTEXITCODE."
        }
    }

    if (-not (Test-Path -LiteralPath $viewerExecutable -PathType Leaf)) {
        throw "The build completed but '$viewerExecutable' was not produced."
    }

    Write-Host "Release build ready: $viewerExecutable" -ForegroundColor Green

    if ($Launch) {
        Start-Process -FilePath $viewerExecutable -WorkingDirectory (Split-Path $viewerExecutable) -WindowStyle Normal
    }
}
finally {
    Pop-Location
}
