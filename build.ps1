<#
.SYNOPSIS
    Build script for STM32CubeIDE project.
.DESCRIPTION
    This script automates building the dc.pid_controller STM32 project.
    It automatically locates the STM32CubeIDE installation on your system (under C:\ST)
    and supports building in two modes: Headless (IDE-based) or Make (fast build).
.PARAMETER Mode
    Build mode: 'Headless' (default, imports & rebuilds project) or 'Make' (faster, uses makefile in Debug folder).
.PARAMETER Clean
    Perform a clean build. Default is $false.
.EXAMPLE
    .\build.ps1 -Mode Headless -Clean
.EXAMPLE
    .\build.ps1 -Mode Make
#>
param (
    [ValidateSet('Headless', 'Make')]
    [string]$Mode = 'Headless',

    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"

# 1. Detect STM32CubeIDE installation under C:\ST
$stPath = "C:\ST"
if (-not (Test-Path $stPath)) {
    Write-Host "Error: Directory '$stPath' does not exist. Please make sure STM32CubeIDE is installed in the default location." -ForegroundColor Red
    exit 1
}

# Get the latest STM32CubeIDE folder
$ideFolder = Get-ChildItem -Path $stPath -Directory -Filter "STM32CubeIDE_*" | 
              Sort-Object Name -Descending | 
              Select-Object -First 1

if ($null -eq $ideFolder) {
    Write-Host "Error: No STM32CubeIDE installation found in '$stPath'." -ForegroundColor Red
    exit 1
}

Write-Host "Found STM32CubeIDE: $($ideFolder.Name)" -ForegroundColor Cyan

# 2. Setup paths
$projectRoot = Resolve-Path "."
$projectName = "dc.pid_controller"
$cubeideExe = Join-Path $ideFolder.FullName "STM32CubeIDE\stm32cubeidec.exe"

if (-not (Test-Path $cubeideExe)) {
    Write-Host "Error: stm32cubeidec.exe not found at '$cubeideExe'" -ForegroundColor Red
    exit 1
}

# 3. Perform Build
if ($Mode -eq 'Headless') {
    Write-Host "Building project '$projectName' using STM32CubeIDE Headless Builder..." -ForegroundColor Yellow
    
    # Use a temp directory for the eclipse workspace so it doesn't conflict with project files
    $tempWorkspace = Join-Path $env:TEMP "stm32cubeide_headless_ws"
    if (Test-Path $tempWorkspace) {
        Remove-Item -Path $tempWorkspace -Recurse -Force -ErrorAction SilentlyContinue
    }
    
    # Determine build flags
    $buildFlag = if ($Clean) { "-cleanBuild" } else { "-build" }
    
    Write-Host "Workspace path: $tempWorkspace" -ForegroundColor Gray
    
    # Run the Eclipse headless build
    $cmdArgs = @(
        "-nosplash",
        "-application", "org.eclipse.cdt.managedbuilder.core.headlessbuild",
        "-data", $tempWorkspace,
        "-import", $projectRoot.Path,
        $buildFlag, "$projectName/Debug"
    )
    
    & $cubeideExe @cmdArgs
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`nBuild Successful!" -ForegroundColor Green
    } else {
        Write-Host "`nBuild Failed with exit code $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}
elseif ($Mode -eq 'Make') {
    Write-Host "Building project using Make utility..." -ForegroundColor Yellow
    
    # Locate make.exe and arm-none-eabi-gcc.exe dynamically
    Write-Host "Locating compiler toolchain..." -ForegroundColor Gray
    $makeFile = Get-ChildItem -Path $ideFolder.FullName -Recurse -Filter "make.exe" -File | Select-Object -First 1
    $gccFile = Get-ChildItem -Path $ideFolder.FullName -Recurse -Filter "arm-none-eabi-gcc.exe" -File | Select-Object -First 1
    
    if ($null -eq $makeFile -or $null -eq $gccFile) {
        Write-Host "Error: Could not locate make.exe or arm-none-eabi-gcc.exe inside STM32CubeIDE directory." -ForegroundColor Red
        exit 1
    }
    
    $makeExe = $makeFile.FullName
    $toolchainDir = $gccFile.DirectoryName
    $makeBinDir = $makeFile.DirectoryName
    
    Write-Host "Compiler: $toolchainDir" -ForegroundColor Gray
    Write-Host "Make: $makeExe" -ForegroundColor Gray
    
    # Temporarily add toolchain to PATH
    $originalPath = $env:PATH
    $env:PATH = "$toolchainDir;$makeBinDir;$env:PATH"
    
    $debugDir = Join-Path $projectRoot.Path "Debug"
    if (-not (Test-Path $debugDir)) {
        Write-Host "Error: 'Debug' directory not found. Please run with Headless mode first to generate makefiles." -ForegroundColor Red
        $env:PATH = $originalPath
        exit 1
    }
    
    try {
        if ($Clean) {
            Write-Host "Cleaning build directory..." -ForegroundColor Yellow
            & $makeExe -C $debugDir clean
        }
        
        $numJobs = $env:NUMBER_OF_PROCESSORS
        if (-not $numJobs) { $numJobs = 4 }
        Write-Host "Running compilation (jobs: $numJobs)..." -ForegroundColor Yellow
        # Run make with multi-threading
        & $makeExe -C $debugDir -j $numJobs all
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "`nBuild Successful!" -ForegroundColor Green
        } else {
            Write-Host "`nBuild Failed!" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
    finally {
        # Restore original path
        $env:PATH = $originalPath
    }
}
