[CmdletBinding()]
param(
    [string] $ReleaseDirectory,
    [string] $VCRedistPath,
    [string] $InnoCompiler
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'The RadMarky Viewer installer can only be built on Windows.'
}

$packagingDirectory = $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $packagingDirectory '..\..')).Path
$installerScript = Join-Path $packagingDirectory 'radmarky.iss'
$versionFile = Join-Path $repositoryRoot 'cmake\Version.cmake'

if (-not $ReleaseDirectory) {
    $ReleaseDirectory = Join-Path $repositoryRoot 'build\release\Release'
}

if (-not $VCRedistPath) {
    $VCRedistPath = Join-Path $repositoryRoot 'out\prerequisites\vc_redist.x64.exe'
}

if (-not (Test-Path -LiteralPath $installerScript -PathType Leaf)) {
    throw "The Inno Setup script was not found at '$installerScript'."
}

if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw "The version file was not found at '$versionFile'."
}

$versionText = Get-Content -LiteralPath $versionFile -Raw
$versionComponents = foreach ($component in 'MAJOR', 'MINOR', 'BUILD') {
    $componentMatch = [regex]::Match(
        $versionText,
        "set\(RADMARKY_VERSION_$component\s+(?<Value>\d+)\)"
    )
    if (-not $componentMatch.Success) {
        throw "RADMARKY_VERSION_$component could not be read from '$versionFile'."
    }
    $componentMatch.Groups['Value'].Value
}
$numericAppVersion = $versionComponents -join '.'
$suffixMatch = [regex]::Match(
    $versionText,
    'set\(RADMARKY_VERSION_SUFFIX\s+"(?<Value>[^"]*)"\)'
)
if (-not $suffixMatch.Success) {
    throw "RADMARKY_VERSION_SUFFIX could not be read from '$versionFile'."
}
$appVersion = $numericAppVersion + $suffixMatch.Groups['Value'].Value

$ReleaseDirectory = [System.IO.Path]::GetFullPath($ReleaseDirectory)
$releaseExecutable = Join-Path $ReleaseDirectory 'radmarky_viewer.exe'
if (-not (Test-Path -LiteralPath $releaseExecutable -PathType Leaf)) {
    throw "The Release build is incomplete: '$releaseExecutable' was not found."
}

if (-not $InnoCompiler) {
    $compilerCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compilerCommand) {
        $InnoCompiler = $compilerCommand.Source
    }
    else {
        $compilerCandidates = @(
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
        )
        $InnoCompiler = $compilerCandidates |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
    }
}

if (-not $InnoCompiler -or
    -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup 6 was not found. Install it or pass -InnoCompiler <path-to-ISCC.exe>.'
}

$VCRedistPath = [System.IO.Path]::GetFullPath($VCRedistPath)
if (-not (Test-Path -LiteralPath $VCRedistPath -PathType Leaf)) {
    $redistDirectory = Split-Path -Parent $VCRedistPath
    New-Item -ItemType Directory -Path $redistDirectory -Force | Out-Null

    Write-Host 'Downloading the latest Microsoft Visual C++ v14 x64 Redistributable...'
    Invoke-WebRequest `
        -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' `
        -OutFile $VCRedistPath `
        -UseBasicParsing
}

$signature = Get-AuthenticodeSignature -FilePath $VCRedistPath
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
    -not $signature.SignerCertificate -or
    $signature.SignerCertificate.Subject -notmatch '\bMicrosoft Corporation\b') {
    throw "'$VCRedistPath' does not have a valid Microsoft Authenticode signature."
}

$redistVersionText = (Get-Item -LiteralPath $VCRedistPath).VersionInfo.FileVersion
$redistVersionMatch = [regex]::Match(
    $redistVersionText,
    '^(?<Major>\d+)\.(?<Minor>\d+)\.(?<Build>\d+)(?:\.(?<Revision>\d+))?'
)
if (-not $redistVersionMatch.Success) {
    throw "The Visual C++ Redistributable version could not be read from '$VCRedistPath'."
}

$redistVersion = @{
    Major = $redistVersionMatch.Groups['Major'].Value
    Minor = $redistVersionMatch.Groups['Minor'].Value
    Build = $redistVersionMatch.Groups['Build'].Value
    Revision = if ($redistVersionMatch.Groups['Revision'].Success) {
        $redistVersionMatch.Groups['Revision'].Value
    }
    else {
        '0'
    }
}

$outputDirectory = Join-Path $repositoryRoot 'out\installers'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$compilerArguments = @(
    "/DAppVersion=$appVersion",
    "/DNumericAppVersion=$numericAppVersion",
    "/DReleaseDir=$ReleaseDirectory",
    "/DVCRedistPath=$VCRedistPath",
    "/DVCRedistMajor=$($redistVersion.Major)",
    "/DVCRedistMinor=$($redistVersion.Minor)",
    "/DVCRedistBuild=$($redistVersion.Build)",
    "/DVCRedistRevision=$($redistVersion.Revision)",
    $installerScript
)

Write-Host "Building RadMarky Viewer $appVersion installer..."
& $InnoCompiler @compilerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE."
}

$installer = Join-Path $outputDirectory "RadMarky-Viewer-$appVersion-Setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Inno Setup completed but '$installer' was not produced."
}

$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = "$installer.sha256"
$checksumEntry = "$installerHash  $([System.IO.Path]::GetFileName($installer))`r`n"
[System.IO.File]::WriteAllText(
    $checksumFile,
    $checksumEntry,
    [System.Text.Encoding]::ASCII
)

Write-Host "Installer ready: $installer" -ForegroundColor Green
Write-Host "SHA-256 checksum ready: $checksumFile" -ForegroundColor Green
