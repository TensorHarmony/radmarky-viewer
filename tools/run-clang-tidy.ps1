[CmdletBinding()]
param(
    [string] $BuildDirectory = '',
    [string] $ClangTidy = '',
    [switch] $Tests,
    [switch] $Fix,
    [int] $Parallel = 6
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Parallel -lt 1) {
    throw 'Parallel must be at least 1.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repositoryRoot 'build\release'
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$sourceRoot = Join-Path $repositoryRoot 'src'
$vcpkgInstalledRoot = Join-Path $BuildDirectory 'vcpkg_installed'
$vcpkgTriplet = @('x64-windows-static-md', 'x64-windows') |
    Where-Object { Test-Path -LiteralPath (Join-Path $vcpkgInstalledRoot $_\include) -PathType Container } |
    Select-Object -First 1
if (-not $vcpkgTriplet) {
    $vcpkgTriplet = 'x64-windows-static-md'
}
$vcpkgTripletDir = Join-Path $vcpkgInstalledRoot $vcpkgTriplet
$vcpkgInclude = Join-Path $vcpkgTripletDir 'include'
$reportDirectory = Join-Path $repositoryRoot 'build\clang-tidy'
$compileCommandsPath = Join-Path $reportDirectory 'compile_commands.json'

function Find-ClangTidy {
    param([string] $Requested)

    if ($Requested) {
        if (-not (Test-Path -LiteralPath $Requested -PathType Leaf)) {
            throw "clang-tidy was not found at '$Requested'."
        }
        return (Resolve-Path -LiteralPath $Requested).Path
    }

    $fromPath = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $candidates = @(
        "${env:ProgramFiles}\LLVM\bin\clang-tidy.exe"
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw 'clang-tidy was not found. Install the Visual Studio C++ Clang tools or LLVM, or pass -ClangTidy.'
}

function Get-NewestDirectory {
    param([string] $Parent)

    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        return $null
    }

    return Get-ChildItem -LiteralPath $Parent -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1
}

function Get-MsvcIncludeDirectories {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $roots = @()
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installationPath) {
            $roots += $installationPath
        }
    }

    $roots += @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community"
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional"
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
    )

    foreach ($root in $roots) {
        $msvcRoot = Join-Path $root 'VC\Tools\MSVC'
        $version = Get-NewestDirectory -Parent $msvcRoot
        if ($version) {
            $include = Join-Path $version.FullName 'include'
            $atlmfc = Join-Path $version.FullName 'atlmfc\include'
            if (Test-Path -LiteralPath $include -PathType Container) {
                $directories = @($include)
                if (Test-Path -LiteralPath $atlmfc -PathType Container) {
                    $directories += $atlmfc
                }
                return $directories
            }
        }
    }

    throw 'MSVC include directories were not found.'
}

function Get-WindowsSdkIncludeDirectories {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $version = Get-NewestDirectory -Parent $sdkRoot
    if (-not $version) {
        throw "Windows SDK headers were not found under '$sdkRoot'."
    }

    $directories = @()
    foreach ($name in @('ucrt', 'um', 'shared', 'winrt')) {
        $candidate = Join-Path $version.FullName $name
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $directories += $candidate
        }
    }
    if (-not $directories) {
        throw "Windows SDK include subdirectories were not found under '$($version.FullName)'."
    }
    return $directories
}

function Get-VcpkgSystemIncludes {
    param([string] $IncludeRoot)

    $directories = @(
        (Join-Path $IncludeRoot 'vxl\core')
        (Join-Path $IncludeRoot 'vxl\vcl')
        (Join-Path $IncludeRoot 'vxl\v3p\netlib')
        (Join-Path $IncludeRoot 'ITK-5.4')
        (Join-Path $IncludeRoot 'eigen3')
        (Join-Path $IncludeRoot 'vxl\core\vnl\algo')
        (Join-Path $IncludeRoot 'vxl\core\vnl')
        $IncludeRoot
        (Join-Path $IncludeRoot 'vtk-9.3')
        (Join-Path $IncludeRoot 'Qt6\QtCore')
        (Join-Path $IncludeRoot 'Qt6')
        (Join-Path $vcpkgTripletDir 'share\Qt6\mkspecs\win32-msvc')
        (Join-Path $IncludeRoot 'Qt6\QtConcurrent')
        (Join-Path $IncludeRoot 'Qt6\QtGui')
        (Join-Path $IncludeRoot 'Qt6\QtOpenGLWidgets')
        (Join-Path $IncludeRoot 'Qt6\QtOpenGL')
        (Join-Path $IncludeRoot 'Qt6\QtWidgets')
        (Join-Path $IncludeRoot 'Qt6\QtSvg')
    )

    return $directories | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
}

function Get-ProjectIncludeDirectories {
    $directories = @($sourceRoot)
    $itkFactories = Join-Path $BuildDirectory 'ITKFactoryRegistration'
    if (Test-Path -LiteralPath $itkFactories -PathType Container) {
        $directories += $itkFactories
    }

    $autogen = Join-Path $BuildDirectory 'radmarky_viewer_autogen\include_Release'
    if (Test-Path -LiteralPath $autogen -PathType Container) {
        $directories += $autogen
    }

    return $directories
}

function Get-CompileArguments {
    $arguments = @(
        '--target=x86_64-pc-windows-msvc'
        '-std=c++20'
        '-fms-compatibility'
        '-fms-extensions'
        '-w'
        '-DWIN32'
        '-D_WINDOWS'
        '-DWIN64'
        '-D_WIN64'
        '-DUNICODE'
        '-D_UNICODE'
        '-D_ENABLE_EXTENDED_ALIGNED_STORAGE'
        '-D_CRT_USE_BUILTIN_OFFSETOF'
        '-DITK_IMAGEIO_FACTORY_REGISTER_MANAGER'
        '-DQT_CORE_LIB'
        '-DQT_CONCURRENT_LIB'
        '-DQT_GUI_LIB'
        '-DQT_OPENGLWIDGETS_LIB'
        '-DQT_OPENGL_LIB'
        '-DQT_WIDGETS_LIB'
        '-DQT_SVG_LIB'
        '-Dkiss_fft_scalar=double'
        '-DKISSFFT_DLL_IMPORT=1'
    )

    foreach ($directory in Get-ProjectIncludeDirectories) {
        $arguments += @('-I', $directory)
    }
    foreach ($directory in @(Get-MsvcIncludeDirectories) + @(Get-WindowsSdkIncludeDirectories) + @(Get-VcpkgSystemIncludes -IncludeRoot $vcpkgInclude)) {
        $arguments += @('-isystem', $directory)
    }

    return $arguments
}

function Get-SourceFiles {
    $files = @(
        Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter *.cpp |
            Where-Object { $_.FullName -notmatch '[\\/]autogen[\\/]' } |
            Sort-Object FullName
    )

    if ($Tests) {
        $testRoot = Join-Path $repositoryRoot 'tests'
        $files += Get-ChildItem -LiteralPath $testRoot -Recurse -Filter *.cpp |
            Sort-Object FullName
    }

    return $files
}

function ConvertTo-CommandLine {
    param([string[]] $Arguments)

    ($Arguments | ForEach-Object {
        $normalized = $_ -replace '\\', '/'
        if ($normalized -match '[\s"]') {
            '"{0}"' -f ($normalized -replace '"', '\"')
        }
        else {
            $normalized
        }
    }) -join ' '
}

function Write-CompileCommands {
    param(
        [System.IO.FileInfo[]] $Files,
        [string[]] $CompileArguments
    )

    New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null

    $commandPrefix = ConvertTo-CommandLine -Arguments $CompileArguments
    $directoryJson = ($repositoryRoot -replace '\\', '/')
    $entries = foreach ($file in $Files) {
        $fileJson = ($file.FullName -replace '\\', '/')
        [ordered]@{
            directory = $directoryJson
            file = $fileJson
            command = "clang-tidy $commandPrefix `"$fileJson`""
        }
    }

    ,$entries | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $compileCommandsPath -Encoding utf8
}

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "Source directory was not found at '$sourceRoot'."
}
if (-not (Test-Path -LiteralPath $vcpkgInclude -PathType Container)) {
    throw "vcpkg includes were not found at '$vcpkgInclude'. Build the Release tree first."
}

$clangTidyPath = Find-ClangTidy -Requested $ClangTidy
$compileArguments = @(Get-CompileArguments)
$sourceFiles = @(Get-SourceFiles)
if (-not $sourceFiles) {
    throw 'No C++ translation units were found to analyze.'
}

Write-CompileCommands -Files $sourceFiles -CompileArguments $compileArguments

Write-Host "clang-tidy: $clangTidyPath"
Write-Host "Analyzing $($sourceFiles.Count) translation units (parallel=$Parallel)..."

$results = New-Object System.Collections.Concurrent.ConcurrentBag[object]
$queue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
foreach ($file in $sourceFiles) {
    $queue.Enqueue($file.FullName)
}

$workers = @()
$handles = @()
$workerCount = [Math]::Min($Parallel, $sourceFiles.Count)
for ($index = 0; $index -lt $workerCount; $index++) {
    $worker = [powershell]::Create().AddScript({
        param($Executable, $CompileArguments, $ApplyFixes, $Queue, $Results)

        $sourceFile = $null
        while ($Queue.TryDequeue([ref]$sourceFile)) {
            $tidyArguments = @(
                $sourceFile
                '--quiet'
                '--use-color=false'
            )
            if ($ApplyFixes) {
                $tidyArguments += '--fix'
            }

            $allArguments = $tidyArguments + '--' + $CompileArguments
            $output = & $Executable @allArguments 2>&1 | ForEach-Object { "$_" }
            $filtered = @($output | Where-Object {
                $_ -and
                $_ -notmatch '^\d+ warnings generated\.$' -and
                $_ -notmatch 'Suppressed \d+ warnings' -and
                $_ -notmatch 'Use -header-filter'
            })

            $Results.Add([pscustomobject]@{
                File = $sourceFile
                ExitCode = $LASTEXITCODE
                Output = ($filtered -join [Environment]::NewLine).Trim()
            })
        }
    }).AddArgument($clangTidyPath).AddArgument($compileArguments).AddArgument([bool]$Fix).AddArgument($queue).AddArgument($results)

    $workers += $worker
    $handles += $worker.BeginInvoke()
}

for ($index = 0; $index -lt $workers.Count; $index++) {
    $workers[$index].EndInvoke($handles[$index]) | Out-Null
    $errorRecords = @($workers[$index].Streams.Error)
    $workers[$index].Dispose()
    if ($errorRecords) {
        throw ($errorRecords | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    }
}

$ordered = @($results | Sort-Object File)
$findings = @($ordered | Where-Object { $_.Output })
$failed = @($ordered | Where-Object { $_.ExitCode -notin @(0, $null) })
$reportPath = Join-Path $reportDirectory 'report.txt'

$report = New-Object System.Collections.Generic.List[string]
$report.Add("clang-tidy report for $repositoryRoot")
$report.Add("Analyzed $($sourceFiles.Count) translation units.")
$report.Add("Files with diagnostics: $($findings.Count)")
$report.Add('')
foreach ($item in $findings) {
    $report.Add("=== $($item.File) ===")
    $report.Add($item.Output)
    $report.Add('')
}
$report | Set-Content -LiteralPath $reportPath -Encoding utf8

foreach ($item in $findings) {
    Write-Host $item.Output
    Write-Host ''
}

if ($failed) {
    Write-Host "clang-tidy returned a non-zero exit code for $($failed.Count) file(s)." -ForegroundColor Yellow
}

if ($findings) {
    Write-Host "clang-tidy reported diagnostics in $($findings.Count) file(s). See $reportPath" -ForegroundColor Yellow
    exit 1
}

Write-Host "clang-tidy reported no project diagnostics. See $reportPath" -ForegroundColor Green
exit 0
