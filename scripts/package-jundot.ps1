param(
    [ValidateSet("editor", "template_release", "template_debug")]
    [string]$Target = "editor",

    [ValidateSet("x86_64", "x86_32", "arm64")]
    [string]$Arch = "x86_64",

    [ValidateSet("windows", "linuxbsd", "macos", "android", "ios", "web")]
    [string]$Platform = "windows",

    [string]$OutputDir = "artifacts/packages",
    [string]$LogDir = "artifacts/logs",
    [string]$PackageName = "",
    [int]$Jobs = 0,
    [string[]]$SConsArgs = @(),
    [switch]$SkipBuild,
    [switch]$InstallSCons,
    [switch]$Mono,
    [switch]$UseMinGW,
    [string]$MingwPrefix = "",
    [switch]$EnableWindowsOptionalDeps,
    [switch]$CleanPackageDir
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Find-Command {
    param([string[]]$Names)

    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    return $null
}

function Test-Tool {
    param(
        [string]$Command,
        [string[]]$Arguments
    )

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Command @Arguments *> $null
        return $LASTEXITCODE -eq 0
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}

function Find-Python {
    $candidates = @("py", "python", "python3")

    foreach ($candidate in $candidates) {
        if ((Find-Command @($candidate)) -and (Test-Tool -Command $candidate -Arguments @("--version"))) {
            return $candidate
        }
    }

    return $null
}

function Find-SConsRunner {
    param([string]$Python)

    if ($Python -and (Test-Tool -Command $Python -Arguments @("-m", "SCons", "--version"))) {
        return @{
            Command = $Python
            Prefix = @("-m", "SCons")
        }
    }

    if ((Find-Command @("scons")) -and (Test-Tool -Command "scons" -Arguments @("--version"))) {
        return @{
            Command = "scons"
            Prefix = @()
        }
    }

    return $null
}

function Get-SConsVersion {
    param([hashtable]$Runner)

    if (-not $Runner) {
        return $null
    }

    $output = & $Runner.Command @($Runner.Prefix + @("--version")) 2>$null
    foreach ($line in $output) {
        if ($line -match "(?<![0-9])([0-9]+)\.([0-9]+)\.([0-9]+)(?![0-9])") {
            return [Version]"$($Matches[1]).$($Matches[2]).$($Matches[3])"
        }
    }

    return $null
}

function Ensure-SConsForMsvc {
    param(
        [hashtable]$Runner,
        [string]$Python,
        [string]$MsvcVersion
    )

    if ($MsvcVersion -ne "14.5") {
        return $Runner
    }

    $sconsVersion = Get-SConsVersion -Runner $Runner
    if ($sconsVersion -and $sconsVersion -ge [Version]"4.10.1") {
        return $Runner
    }

    if (-not $InstallSCons) {
        $reportedVersion = if ($sconsVersion) { $sconsVersion.ToString() } else { "unknown" }
        throw "Visual Studio 2026 requires SCons 4.10.1+, but the detected SCons version is $reportedVersion. Rerun with -InstallSCons to upgrade it."
    }

    Write-Step "Upgrading SCons for Visual Studio 2026"
    & $Python -m pip install --user --upgrade "scons>=4.10.1"
    if ($LASTEXITCODE -ne 0) {
        throw "SCons upgrade failed with exit code $LASTEXITCODE."
    }

    $updatedRunner = @{
        Command = $Python
        Prefix = @("-m", "SCons")
    }
    $updatedVersion = Get-SConsVersion -Runner $updatedRunner
    if (-not $updatedVersion -or $updatedVersion -lt [Version]"4.10.1") {
        $reportedVersion = if ($updatedVersion) { $updatedVersion.ToString() } else { "unknown" }
        throw "SCons was upgraded, but '$Python -m SCons --version' reported $reportedVersion instead of 4.10.1+."
    }

    return $updatedRunner
}

function Write-WindowsCompilerHelp {
    Write-Host ""
    Write-Host "Windows C++ compiler was not found." -ForegroundColor Yellow
    Write-Host "Install one of these toolchains, then run the package script again:"
    Write-Host ""
    Write-Host "  Option A: Visual Studio 2022 Build Tools"
    Write-Host "    1. Install 'Desktop development with C++'."
    Write-Host "    2. Include a Windows 10/11 SDK."
    Write-Host "    3. Open 'x64 Native Tools Command Prompt for VS 2022'."
    Write-Host "    4. Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-jundot.ps1"
    Write-Host ""
    Write-Host "  Option B: MinGW-w64"
    Write-Host "    Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-jundot.ps1 -UseMinGW -MingwPrefix C:\msys64\mingw64"
    Write-Host ""
    Write-Host "If MinGW is already on PATH, you can omit -MingwPrefix."
}

function Find-VsWhere {
    $fromPath = Find-Command @("vswhere")
    if ($fromPath) {
        return $fromPath
    }

    $defaultPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    return $null
}

function Import-VisualStudioEnvironment {
    param([string]$Arch)

    if ($Platform -ne "windows" -or $UseMinGW) {
        return $false
    }

    if ((Find-Command @("cl")) -and $env:VCToolsInstallDir) {
        return $true
    }

    $vswhere = Find-VsWhere
    if (-not $vswhere) {
        return $false
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
        return $false
    }

    $devCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $devCmd)) {
        return $false
    }

    $targetArch = switch ($Arch) {
        "x86_32" { "x86" }
        "arm64" { "arm64" }
        default { "x64" }
    }

    Write-Step "Loading Visual Studio compiler environment"
    $environmentLines = cmd /s /c "`"$devCmd`" -arch=$targetArch -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }

    return (Find-Command @("cl")) -ne $null
}

function Test-MingwCompiler {
    $prefix = if ([string]::IsNullOrWhiteSpace($env:MINGW_PREFIX)) { "" } else { Join-Path $env:MINGW_PREFIX "bin" }
    $gcc = if ($prefix) { Join-Path $prefix "gcc.exe" } else { "gcc" }
    $clang = if ($prefix) { Join-Path $prefix "clang.exe" } else { "clang" }

    if ((Test-Path $gcc) -and (Test-Tool -Command $gcc -Arguments @("--version"))) {
        return $true
    }

    if ((Test-Path $clang) -and (Test-Tool -Command $clang -Arguments @("--version"))) {
        return $true
    }

    if (-not $prefix) {
        return (Test-Tool -Command "gcc" -Arguments @("--version")) -or (Test-Tool -Command "clang" -Arguments @("--version"))
    }

    return $false
}

function Assert-WindowsCompiler {
    if ($Platform -ne "windows") {
        return
    }

    if (Import-VisualStudioEnvironment -Arch $Arch) {
        return
    }

    $hasRequestedMingw = $UseMinGW -or -not [string]::IsNullOrWhiteSpace($MingwPrefix)
    if ($hasRequestedMingw -or $env:MSYSTEM -or $env:MINGW_PREFIX) {
        if (Test-MingwCompiler) {
            return
        }

        Write-WindowsCompilerHelp
        throw "MinGW was requested or detected, but gcc/clang could not be started."
    }

    Write-WindowsCompilerHelp
    throw "Windows C++ compiler is required before building Jundot."
}

function Get-DetectedMsvcVersion {
    if ($Platform -ne "windows" -or $UseMinGW -or -not $env:VCToolsVersion) {
        return ""
    }

    if ($env:VCToolsVersion -match "^14\.5") {
        return "14.5"
    }
    if ($env:VCToolsVersion -match "^14\.4") {
        return "14.4"
    }
    if ($env:VCToolsVersion -match "^14\.3") {
        return "14.3"
    }
    if ($env:VCToolsVersion -match "^14\.2") {
        return "14.2"
    }

    if ($env:VSINSTALLDIR -match "\\18\\") {
        return "14.5"
    }

    return ""
}

function Test-HasSConsArg {
    param(
        [string[]]$Arguments,
        [string]$Name
    )

    foreach ($argument in $Arguments) {
        if ($argument -match "^$([Regex]::Escape($Name))=") {
            return $true
        }
    }

    return $false
}

function Add-SConsArgIfMissing {
    param(
        [string[]]$Arguments,
        [string]$Name,
        [string]$Value
    )

    if (Test-HasSConsArg -Arguments $Arguments -Name $Name) {
        return $Arguments
    }

    return $Arguments + "$Name=$Value"
}

function Add-MsvcEnvironmentImportIfMissing {
    param([string[]]$Arguments)

    if ($Platform -ne "windows" -or $UseMinGW) {
        return $Arguments
    }

    if (Test-HasSConsArg -Arguments $Arguments -Name "import_env_vars") {
        return $Arguments
    }

    $vars = @(
        "PATH",
        "INCLUDE",
        "LIB",
        "LIBPATH",
        "VCToolsInstallDir",
        "VCToolsVersion",
        "VSINSTALLDIR",
        "WindowsSdkDir",
        "WindowsSDKLibVersion",
        "UniversalCRTSdkDir",
        "UCRTVersion"
    )

    return $Arguments + "import_env_vars=$($vars -join ',')"
}

function Get-JundotVersion {
    param([string]$VersionFile)

    $values = @{}
    foreach ($line in Get-Content -Path $VersionFile) {
        if ($line -match '^\s*([A-Za-z_]+)\s*=\s*"?([^"]+)"?\s*$') {
            $values[$Matches[1]] = $Matches[2]
        }
    }

    $version = "$($values.major).$($values.minor)"
    if ($values.patch -and $values.patch -ne "0") {
        $version += ".$($values.patch)"
    }
    if ($values.status) {
        $version += "-$($values.status)"
    }

    return $version
}

function Get-ProductPattern {
    param(
        [string]$Platform,
        [string]$Target,
        [string]$Arch,
        [bool]$MonoBuild
    )

    $platformPart = [Regex]::Escape($Platform)
    $targetPart = [Regex]::Escape($Target)
    $archPart = [Regex]::Escape($Arch)

    if ($Platform -eq "windows") {
        if ($MonoBuild) {
            return "^jundot\.$platformPart\.$targetPart\.$archPart(\..*)?\.mono(\..*)?\.exe$"
        }

        return "^jundot\.$platformPart\.$targetPart\.$archPart(?!.*\.mono)(\..+)?\.exe$"
    }

    if ($MonoBuild) {
        return "^jundot\.$platformPart\.$targetPart\.$archPart(\..*)?\.mono(\..*)?$"
    }

    return "^jundot\.$platformPart\.$targetPart\.$archPart(\..+)?$"
}

function Select-JundotExecutable {
    param([System.IO.FileInfo[]]$Products)

    $consoleProduct = $Products | Where-Object { $_.Name -match "\.console\.exe$" } | Select-Object -First 1
    if ($consoleProduct) {
        return $consoleProduct
    }

    return $Products | Select-Object -First 1
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

if (-not (Test-Path "SConstruct")) {
    throw "SConstruct was not found. Run this script from the Jundot source tree."
}

$Version = Get-JundotVersion -VersionFile (Join-Path $RepoRoot "version.py")
$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $monoSuffix = if ($Mono) { "-mono" } else { "" }
    $PackageName = "jundot-$Version-$Platform-$Target-$Arch$monoSuffix-$Timestamp"
}

$PackageRoot = Join-Path $RepoRoot $OutputDir
$LogRoot = Join-Path $RepoRoot $LogDir
$StagingDir = Join-Path $PackageRoot $PackageName
$ZipPath = Join-Path $PackageRoot "$PackageName.zip"

if (-not $SkipBuild) {
    Write-Step "Checking build tools"
    $python = Find-Python
    if (-not $python) {
        throw "Python was not found in PATH. Install Python, then install SCons with: python -m pip install scons"
    }

    $sconsRunner = Find-SConsRunner -Python $python
    if (-not $sconsRunner) {
        if (-not $InstallSCons) {
            throw "SCons was not found. Install it with: $python -m pip install --user scons, or rerun this script with -InstallSCons."
        }

        Write-Step "Installing SCons"
        & $python -m pip install --user scons
        if ($LASTEXITCODE -ne 0) {
            throw "SCons installation failed with exit code $LASTEXITCODE."
        }

        $sconsRunner = Find-SConsRunner -Python $python
        if (-not $sconsRunner) {
            throw "SCons was installed but still could not be started. Restart the terminal, or add the Python Scripts directory to PATH."
        }
    }

    if ($Platform -eq "windows" -and -not [string]::IsNullOrWhiteSpace($MingwPrefix)) {
        $resolvedMingwPrefix = Resolve-Path $MingwPrefix -ErrorAction SilentlyContinue
        if (-not $resolvedMingwPrefix) {
            throw "MinGW prefix was not found: $MingwPrefix"
        }

        $env:MINGW_PREFIX = $resolvedMingwPrefix.Path
        $UseMinGW = $true
    }

    Assert-WindowsCompiler

    if ($Mono -and -not (Find-Command @("dotnet", "MSBuild"))) {
        throw "Mono builds require the .NET SDK or MSBuild. Install the .NET SDK, then rerun with -Mono."
    }

    $jobs = $Jobs
    if ($jobs -le 0) {
        $jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
        if ($Platform -eq "windows" -and $Target -eq "editor") {
            $jobs = [Math]::Min($jobs, 4)
        }
    }

    $extraBuildArgs = @()
    $detectedMsvcVersion = Get-DetectedMsvcVersion
    if ($detectedMsvcVersion -and -not (Test-HasSConsArg -Arguments $SConsArgs -Name "msvc_version")) {
        $sconsRunner = Ensure-SConsForMsvc -Runner $sconsRunner -Python $python -MsvcVersion $detectedMsvcVersion
        $extraBuildArgs += "msvc_version=$detectedMsvcVersion"
        if ($detectedMsvcVersion -eq "14.5") {
            Write-Host "Detected Visual Studio 2026/MSVC $env:VCToolsVersion; using msvc_version=14.5." -ForegroundColor Yellow
            Write-Host "Jundot requires SCons 4.10.1+ for Visual Studio 2026." -ForegroundColor Yellow
        }
    }

    $buildArgs = @(
        "platform=$Platform",
        "target=$Target",
        "arch=$Arch",
        "debug_symbols=no",
        "-j$jobs"
    ) + $extraBuildArgs + $SConsArgs

    if ($Platform -eq "windows" -and $UseMinGW) {
        $buildArgs += "use_mingw=yes"
        if ($env:MINGW_PREFIX) {
            $buildArgs += "mingw_prefix=$env:MINGW_PREFIX"
        }
    }

    if ($Platform -eq "windows" -and -not $EnableWindowsOptionalDeps) {
        $buildArgs = Add-SConsArgIfMissing -Arguments $buildArgs -Name "d3d12" -Value "no"
        $buildArgs = Add-SConsArgIfMissing -Arguments $buildArgs -Name "accesskit" -Value "no"
        $buildArgs = Add-SConsArgIfMissing -Arguments $buildArgs -Name "angle" -Value "no"
        Write-Host "Windows optional dependencies are disabled by default: d3d12=no accesskit=no." -ForegroundColor Yellow
        Write-Host "Use -EnableWindowsOptionalDeps after installing those SDK dependencies." -ForegroundColor Yellow
    }

    $buildArgs = Add-MsvcEnvironmentImportIfMissing -Arguments $buildArgs

    if ($Platform -eq "windows" -and -not $UseMinGW) {
        $buildArgs = Add-SConsArgIfMissing -Arguments $buildArgs -Name "cxxflags" -Value "/Zm200"
    }

    if ($Mono) {
        $buildArgs = Add-SConsArgIfMissing -Arguments $buildArgs -Name "module_mono_enabled" -Value "yes"
    }

    Write-Step "Building Jundot ($($buildArgs -join ' '))"
    $sconsCommand = $sconsRunner.Command
    $sconsArguments = @($sconsRunner.Prefix) + $buildArgs
    New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
    $buildLogPath = Join-Path $LogRoot "$PackageName-build.log"
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $sconsCommand @sconsArguments 2>&1 | Tee-Object -FilePath $buildLogPath
        $sconsExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($sconsExitCode -ne 0) {
        Write-Host ""
        Write-Host "Build log:" -ForegroundColor Yellow
        Write-Host "  $buildLogPath"

        $errorLines = Get-Content -Path $buildLogPath | Select-String -Pattern "fatal error|error C[0-9]+|scons: \*\*\*|LINK : fatal error|LNK[0-9]+|ERROR:"
        if ($errorLines) {
            Write-Host ""
            Write-Host "Likely error lines:" -ForegroundColor Yellow
            $errorLines | Select-Object -First 25 | ForEach-Object { Write-Host "  $($_.Line)" }
        }

        throw "SCons build failed with exit code $sconsExitCode."
    }

    if ($Mono) {
        Write-Step "Preparing Mono assemblies"
        $BinDir = Join-Path $RepoRoot "bin"
        $monoPattern = Get-ProductPattern -Platform $Platform -Target $Target -Arch $Arch -MonoBuild $true
        $monoProducts = Get-ChildItem -Path $BinDir -File | Where-Object { $_.Name -match $monoPattern }
        if (-not $monoProducts) {
            throw "Mono build completed, but no Mono executable matched '$monoPattern' in $BinDir."
        }

        $monoExecutable = Select-JundotExecutable -Products $monoProducts
        Write-Host "Using Mono executable: $($monoExecutable.FullName)"

        $glueLogPath = Join-Path $LogRoot "$PackageName-mono-glue.log"
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $monoExecutable.FullName --headless --generate-mono-glue ./modules/mono/glue 2>&1 | Tee-Object -FilePath $glueLogPath
            $glueExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        if ($glueExitCode -ne 0) {
            throw "Mono glue generation failed with exit code $glueExitCode. Log: $glueLogPath"
        }

        $assembliesLogPath = Join-Path $LogRoot "$PackageName-mono-assemblies.log"
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $python ./modules/mono/build_scripts/build_assemblies.py --jundot-output-dir=./bin --jundot-platform=$Platform 2>&1 | Tee-Object -FilePath $assembliesLogPath
            $assembliesExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        if ($assembliesExitCode -ne 0) {
            throw "Mono assemblies build failed with exit code $assembliesExitCode. Log: $assembliesLogPath"
        }
    }
} else {
    Write-Step "Skipping build and using existing files in bin"
}

Write-Step "Preparing package folders"
New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null
if ($CleanPackageDir -and (Test-Path $StagingDir)) {
    Remove-Item -Path $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

Write-Step "Collecting build products"
$BinDir = Join-Path $RepoRoot "bin"
if (-not (Test-Path $BinDir)) {
    throw "The bin directory does not exist. Build first or run without -SkipBuild."
}

$pattern = Get-ProductPattern -Platform $Platform -Target $Target -Arch $Arch -MonoBuild ([bool]$Mono)
$products = Get-ChildItem -Path $BinDir -File | Where-Object { $_.Name -match $pattern }

if (-not $products) {
    throw "No build products matched '$pattern' in $BinDir."
}

foreach ($product in $products) {
    Copy-Item -Path $product.FullName -Destination $StagingDir -Force
}

if ($Mono) {
    $jundotSharpDir = Join-Path $BinDir "JundotSharp"
    if (-not (Test-Path $jundotSharpDir)) {
        throw "Mono package requires '$jundotSharpDir', but it was not found. Build assemblies may have failed or been skipped."
    }

    Copy-Item -Path $jundotSharpDir -Destination $StagingDir -Recurse -Force
}

$manifestPath = Join-Path $StagingDir "package-manifest.txt"
$manifest = @(
    "Package: $PackageName",
    "Version: $Version",
    "Platform: $Platform",
    "Target: $Target",
    "Arch: $Arch",
    "Created: $(Get-Date -Format o)",
    "Commit: $(git rev-parse --short HEAD 2>$null)",
    "",
    "Files:"
) + ($products | ForEach-Object { "  $($_.Name)" })
if ($Mono) {
    $manifest += "  JundotSharp/"
}
Set-Content -Path $manifestPath -Value $manifest -Encoding UTF8

Write-Step "Creating zip package"
if (Test-Path $ZipPath) {
    Remove-Item -Path $ZipPath -Force
}
Compress-Archive -Path (Join-Path $StagingDir "*") -DestinationPath $ZipPath -Force

Write-Host ""
Write-Host "Package created:" -ForegroundColor Green
Write-Host "  $ZipPath"
