[CmdletBinding()]
param(
    [ValidatePattern('^[A-Z]$')]
    [string]$DriveLetter = 'J',
    [string]$BuildDir = 'build_safe',
    [switch]$NoDriveAlias
)

$ErrorActionPreference = 'Stop'

function Add-PathIfExists {
    param([string]$Path)
    if ($Path -and (Test-Path -LiteralPath $Path)) {
        $env:PATH = "$Path;$env:PATH"
    }
}

function Add-LatestToolPath {
    param(
        [string]$ToolsRoot,
        [string]$ToolName,
        [string]$InnerPath = '',
        [string]$PreferredVersion = ''
    )
    $root = Join-Path $ToolsRoot $ToolName
    if (-not (Test-Path -LiteralPath $root)) {
        return
    }
    $dir = $null
    if ($PreferredVersion) {
        $preferred = Join-Path $root $PreferredVersion
        if (Test-Path -LiteralPath $preferred) {
            $dir = Get-Item -LiteralPath $preferred
        }
    }
    if ($null -eq $dir) {
        $dir = Get-ChildItem -LiteralPath $root -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1
    }
    if ($null -eq $dir) {
        return
    }
    Add-PathIfExists (Join-Path $dir.FullName $InnerPath)
}

function Update-TextFileLiteral {
    param(
        [string]$Path,
        [string]$OldText,
        [string]$NewText
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    $text = Get-Content -LiteralPath $Path -Raw
    # Empty NewText means remove OldText. String.Contains('') is always true,
    # which previously skipped every requested removal and left broken -L paths.
    if ($NewText.Length -gt 0 -and $text.Contains($NewText)) {
        return $false
    }
    if ($text.Contains($OldText)) {
        Set-Content -LiteralPath $Path -Value ($text.Replace($OldText, $NewText)) -NoNewline
        return $true
    }
    return $false
}

function Repair-ManagedComponentLinkers {
    param([string]$Root)

    $changed = $false
    $audioCodec = Join-Path $Root 'managed_components\espressif__esp_audio_codec\CMakeLists.txt'
    $audioEffects = Join-Path $Root 'managed_components\espressif__esp_audio_effects\CMakeLists.txt'
    $imageEffects = Join-Path $Root 'managed_components\espressif__esp_image_effects\CMakeLists.txt'

    $changed = (Update-TextFileLiteral $audioCodec 'target_link_libraries(${COMPONENT_LIB}  PRIVATE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")' '') -or $changed
    $changed = (Update-TextFileLiteral $audioCodec 'target_link_libraries(${COMPONENT_LIB}  PRIVATE "-L${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")' '') -or $changed

    $changed = (Update-TextFileLiteral $audioEffects @'
idf_component_register(INCLUDE_DIRS "include")
get_filename_component(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_prebuilt_library(${BASE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_audio_effects.a"
                     PRIV_REQUIRES ${BASE_DIR})
target_link_libraries(${COMPONENT_LIB}  INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_audio_effects)
'@ @'
idf_component_register(INCLUDE_DIRS "include")
add_prebuilt_library(esp_audio_effects "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_audio_effects.a"
                     PRIV_REQUIRES ${COMPONENT_NAME})
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_audio_effects)
'@) -or $changed

    $changed = (Update-TextFileLiteral $audioEffects @'
idf_component_register(INCLUDE_DIRS "include")
get_filename_component(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_prebuilt_library(${BASE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_audio_effects.a"
                     PRIV_REQUIRES ${BASE_DIR})
target_link_libraries(${COMPONENT_LIB}  INTERFACE "-L${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_audio_effects)
'@ @'
idf_component_register(INCLUDE_DIRS "include")
add_prebuilt_library(esp_audio_effects "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_audio_effects.a"
                     PRIV_REQUIRES ${COMPONENT_NAME})
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_audio_effects)
'@) -or $changed

    $changed = (Update-TextFileLiteral $imageEffects @'
idf_component_register(INCLUDE_DIRS "include")
target_link_libraries(${COMPONENT_LIB}  INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_image_effects)
'@ @'
idf_component_register(INCLUDE_DIRS "include")
add_prebuilt_library(esp_image_effects "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_image_effects.a"
                     PRIV_REQUIRES ${COMPONENT_NAME})
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_image_effects)
'@) -or $changed

    $changed = (Update-TextFileLiteral $imageEffects @'
idf_component_register(INCLUDE_DIRS "include")
target_link_libraries(${COMPONENT_LIB}  INTERFACE "-L${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_image_effects)
'@ @'
idf_component_register(INCLUDE_DIRS "include")
add_prebuilt_library(esp_image_effects "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_image_effects.a"
                     PRIV_REQUIRES ${COMPONENT_NAME})
target_link_libraries(${COMPONENT_LIB}  INTERFACE esp_image_effects)
'@) -or $changed

    if ($changed) {
        Write-Host 'Repaired managed-component linker wrappers for Windows paths with spaces.'
    }
}

$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$IdfPath = if ($env:IDF_PATH) {
    $env:IDF_PATH
} elseif (Test-Path -LiteralPath 'C:\Espressif\frameworks\esp-idf-v5.5') {
    'C:\Espressif\frameworks\esp-idf-v5.5'
} else {
    'C:\Espressif\frameworks\esp-idf'
}
$IdfToolsPath = if ($env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH } else { Join-Path $env:USERPROFILE '.espressif' }
$EspToolStore = Join-Path $IdfToolsPath 'tools'
$IdfPy = Join-Path $IdfPath 'tools\idf.py'

if (-not (Test-Path -LiteralPath $IdfPy)) {
    throw "ESP-IDF was not found at '$IdfPath'. Set IDF_PATH to the installed ESP-IDF directory."
}
if (-not (Test-Path -LiteralPath $IdfToolsPath)) {
    throw "ESP-IDF tools were not found at '$IdfToolsPath'. Set IDF_TOOLS_PATH to the installed tools directory."
}
if (-not (Test-Path -LiteralPath $EspToolStore)) {
    throw "ESP-IDF binary tools were not found at '$EspToolStore'."
}

if (-not $env:IDF_PYTHON_ENV_PATH) {
    $pythonEnvRoot = Join-Path $IdfToolsPath 'python_env'
    $pythonEnv = Get-ChildItem -LiteralPath $pythonEnvRoot -Directory -Filter 'idf5.5_py*_env' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($null -eq $pythonEnv) {
        $pythonEnv = Get-ChildItem -LiteralPath $pythonEnvRoot -Directory -Filter 'idf*_py*_env' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1
    }
    if ($null -eq $pythonEnv) {
        throw "No ESP-IDF Python environment was found under '$pythonEnvRoot'."
    }
    $env:IDF_PYTHON_ENV_PATH = $pythonEnv.FullName
}

$Python = if ($env:ESP_PYTHON) {
    $env:ESP_PYTHON
} else {
    Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
}
if (-not (Test-Path -LiteralPath $Python)) {
    throw "ESP-IDF Python was not found at '$Python'."
}

if (-not $env:PROCESSOR_ARCHITECTURE) {
    $env:PROCESSOR_ARCHITECTURE = if ([Environment]::Is64BitOperatingSystem) { 'AMD64' } else { 'x86' }
}

if ($env:ESP_PYTHON) {
    $idfSitePackages = Join-Path $env:IDF_PYTHON_ENV_PATH 'Lib\site-packages'
    if (Test-Path -LiteralPath $idfSitePackages) {
        $pathSeparator = [IO.Path]::PathSeparator
        $pythonPaths = @()
        if ($env:PYTHONPATH) {
            $pythonPaths = $env:PYTHONPATH -split [regex]::Escape($pathSeparator)
        }
        if ($pythonPaths -notcontains $idfSitePackages) {
            $env:PYTHONPATH = if ($env:PYTHONPATH) { "$idfSitePackages$pathSeparator$env:PYTHONPATH" } else { $idfSitePackages }
        }
    }
}

$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:ESP_IDF_VERSION = '5.5'

Add-LatestToolPath $EspToolStore 'xtensa-esp-elf-gdb' 'xtensa-esp-elf-gdb\bin'
Add-LatestToolPath $EspToolStore 'riscv32-esp-elf-gdb' 'riscv32-esp-elf-gdb\bin'
Add-LatestToolPath $EspToolStore 'xtensa-esp-elf' 'xtensa-esp-elf\bin' 'esp-14.2.0_20260121'
Add-LatestToolPath $EspToolStore 'riscv32-esp-elf' 'riscv32-esp-elf\bin' 'esp-14.2.0_20260121'
Add-LatestToolPath $EspToolStore 'esp32ulp-elf' 'esp32ulp-elf\bin'
Add-LatestToolPath $EspToolStore 'cmake' 'bin'
Add-LatestToolPath $EspToolStore 'openocd-esp32' 'openocd-esp32\bin'
Add-LatestToolPath $EspToolStore 'ninja'
Add-LatestToolPath $EspToolStore 'idf-exe'
Add-LatestToolPath $EspToolStore 'ccache' 'ccache-4.12.1-windows-x86_64'
Add-LatestToolPath $EspToolStore 'dfu-util' 'dfu-util-0.11-win64'
Add-PathIfExists (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts')
Add-PathIfExists (Join-Path $IdfPath 'tools')

$xtensaGcc = Get-Command 'xtensa-esp32s3-elf-gcc.exe' -ErrorAction SilentlyContinue
$xtensaGxx = Get-Command 'xtensa-esp32s3-elf-g++.exe' -ErrorAction SilentlyContinue
if ($null -eq $xtensaGcc -or $null -eq $xtensaGxx) {
    throw 'ESP32-S3 Xtensa compiler was not found on PATH after configuring ESP-IDF tools.'
}
$env:CC = $xtensaGcc.Source
$env:CXX = $xtensaGxx.Source
$env:ASM = $xtensaGcc.Source

$openOcd = Get-ChildItem -LiteralPath (Join-Path $EspToolStore 'openocd-esp32') -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -ne $openOcd) {
    $env:OPENOCD_SCRIPTS = Join-Path $openOcd.FullName 'openocd-esp32\share\openocd\scripts'
}

$romElf = Get-ChildItem -LiteralPath (Join-Path $EspToolStore 'esp-rom-elfs') -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -ne $romElf) {
    $env:ESP_ROM_ELF_DIR = $romElf.FullName
}

$substDrive = "$($DriveLetter):"
$createdAlias = $false
$buildRoot = $ProjectRoot

try {
    if (-not $NoDriveAlias) {
        if (Test-Path -LiteralPath "$substDrive\") {
            throw "Drive $substDrive already exists. Re-run with a different -DriveLetter, for example -DriveLetter K."
        }
        & cmd.exe /c "subst $substDrive `"$ProjectRoot`""
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create temporary drive alias $substDrive for '$ProjectRoot'."
        }
        $createdAlias = $true
        $buildRoot = "$substDrive\"
        Write-Host "Using temporary build alias $substDrive -> $ProjectRoot"
    }

    if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot 'managed_components'))) {
        Write-Host 'Resolving ESP-IDF managed components before applying local linker repair...'
        Set-Location -LiteralPath $buildRoot
        & $Python $IdfPy -B $BuildDir "-DPYTHON=$Python" "-DPython3_EXECUTABLE=$Python" reconfigure
        if ($LASTEXITCODE -ne 0) {
            throw "ESP-IDF dependency/configure step failed with exit code $LASTEXITCODE."
        }
    }

    Repair-ManagedComponentLinkers $ProjectRoot

    Set-Location -LiteralPath $buildRoot
    if ($env:ESP_PYTHON) {
        & $Python $IdfPy -B $BuildDir "-DPYTHON=$Python" "-DPython3_EXECUTABLE=$Python" reconfigure
        if ($LASTEXITCODE -ne 0) {
            throw "ESP-IDF reconfigure failed with exit code $LASTEXITCODE."
        }
    }
    & $Python $IdfPy -B $BuildDir "-DPYTHON=$Python" "-DPython3_EXECUTABLE=$Python" build
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF build failed with exit code $LASTEXITCODE."
    }
} finally {
    if ($createdAlias) {
        & cmd.exe /c "subst $substDrive /D" | Out-Null
    }
}
