#Requires -Version 7.0
[CmdletBinding()]
param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildDir = 'build_safe')

# Host-only acceptance checks: never open a serial port or invoke a flash action.
$ErrorActionPreference = 'Stop'
$firmware = Split-Path $PSScriptRoot -Parent
$project = Split-Path $firmware -Parent
$build = Join-Path $firmware $BuildDir
$checks = [Collections.Generic.List[string]]::new()
function Assert-Baseline($Condition, [string]$Message) {
    if (!$Condition) { throw "FAIL: $Message" }
    $checks.Add($Message)
    Write-Output "PASS: $Message"
}

Write-Output "Started: $((Get-Date).ToString('o'))"
$description = Get-Content (Join-Path $build 'project_description.json') -Raw | ConvertFrom-Json
$config = Get-Content (Join-Path $build 'config/sdkconfig.json') -Raw | ConvertFrom-Json -AsHashtable
$sdkconfig = Get-Content (Join-Path $firmware 'sdkconfig')
$flash = Get-Content (Join-Path $build 'flasher_args.json') -Raw | ConvertFrom-Json -AsHashtable
Assert-Baseline ([IO.Path]::GetFullPath($description.project_path) -eq [IO.Path]::GetFullPath($firmware)) 'Build belongs to the current project location'
Assert-Baseline ($description.target -eq 'esp32s3') 'ESP32-S3 build target'
$required = [ordered]@{
    BOARD_TYPE_WAVESHARE_S3_DUALEYE_LCD_1_28 = $true
    IDF_TARGET = 'esp32s3'
    ESPTOOLPY_FLASHSIZE = '16MB'
    SPIRAM_MODE_OCT = $true
    SPIRAM_SPEED = 80
    LANGUAGE_EN_US = $true
    USE_AFE_WAKE_WORD = $true
    SR_WN_WN9_HIESP = $true
}
foreach ($key in $required.Keys) {
    $value = $required[$key]
    Assert-Baseline ($config.ContainsKey($key) -and $config[$key] -ceq $value) "Generated config: $key=$value"
    $literal = if ($value -is [bool]) { 'y' } elseif ($value -is [string]) { '"' + $value + '"' } else { "$value" }
    Assert-Baseline ($sdkconfig -contains "CONFIG_$key=$literal") "Source sdkconfig matches generated $key"
}

$expectedFiles = [ordered]@{
    '0x0' = 'bootloader/bootloader.bin'
    '0x8000' = 'partition_table/partition-table.bin'
    '0xd000' = 'ota_data_initial.bin'
    '0x20000' = 'xiaozhi.bin'
    '0x800000' = 'generated_assets.bin'
}
Assert-Baseline ($flash.flash_files.Count -eq $expectedFiles.Count) 'Flash manifest contains only the five expected images (no NVS)'
$artifacts = @()
foreach ($offset in $expectedFiles.Keys) {
    $relative = $expectedFiles[$offset]
    Assert-Baseline ($flash.flash_files[$offset] -eq $relative) "Flash image mapping: $relative at $offset"
    $file = Get-Item -LiteralPath (Join-Path $build $relative)
    Assert-Baseline ($file.Length -gt 0) "Nonempty artifact: $relative"
    $artifacts += [ordered]@{file="$BuildDir/$relative";offset=$offset;bytes=$file.Length;sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash}
}
$appBytes = (Get-Item (Join-Path $build 'xiaozhi.bin')).Length
Assert-Baseline ($appBytes -le 0x3f0000) 'Application fits each OTA slot'
Assert-Baseline ((Get-Item (Join-Path $build 'generated_assets.bin')).Length -le 8MB) 'Assets fit their partition'
Assert-Baseline ([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $build 'generated_assets.bin'))).Contains('wn9_hiesp')) 'Hi ESP model name is present in the built assets'

$nm = Join-Path (Split-Path $description.c_compiler -Parent) 'xtensa-esp32s3-elf-nm.exe'
$symbols = & $nm --defined-only -C (Join-Path $build $description.app_elf)
Assert-Baseline ($LASTEXITCODE -eq 0) 'ELF symbol inspection succeeded'
foreach ($symbol in @('EyeRenderer::UncannyEyeRenderer::RenderChunk(', 'DisplayManager::EyeRenderTask(', 'SpiLcdDisplayExtended::RenderEye(', 'AudioService::Start(', 'Application::HandleSpeakRequest(', 'BootControlServer::StartServer(')) {
    Assert-Baseline (@($symbols | Where-Object {$_.Contains($symbol)}).Count -gt 0) "Linked implementation: $symbol"
}

$cache = Get-Content (Join-Path $build 'CMakeCache.txt')
$pythonLine = $cache | Where-Object {$_ -match '^PYTHON:[^=]+='} | Select-Object -First 1
if (!$pythonLine) { throw 'Build cache does not identify its Python interpreter' }
$python = ($pythonLine -split '=',2)[1]
& $python -m esptool --chip esp32s3 image_info (Join-Path $build 'xiaozhi.bin')
Assert-Baseline ($LASTEXITCODE -eq 0) 'esptool accepts the built ESP32-S3 application image'
$compilerVersion = & $description.c_compiler --version | Select-Object -First 1
Assert-Baseline ($LASTEXITCODE -eq 0) 'Configured compiler is executable'
$pythonVersion = & $python --version
Assert-Baseline ($LASTEXITCODE -eq 0) 'Configured Python is executable'
$idfVersion = Get-Content (Join-Path $description.idf_path 'tools/cmake/version.cmake') | Where-Object {$_ -match '^set\(IDF_VERSION_(MAJOR|MINOR|PATCH) '}
$sourceHead = & git -C $project rev-parse HEAD
Assert-Baseline ($LASTEXITCODE -eq 0) 'Source repository HEAD is valid'
$inputs = @('sdkconfig','dependencies.lock','sdkconfig.defaults','sdkconfig.defaults.esp32s3','partitions/v2/16m.csv','main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/config.h') | ForEach-Object {
    [ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $firmware $_) -Algorithm SHA256).Hash}
}
$report = [ordered]@{
    checkedAt=(Get-Date).ToString('o'); sourceHead="$sourceHead"; scope='Host build/configuration/artifact checks; no hardware acceptance'
    buildDirectory=$BuildDir; firmwareVersion=$description.project_version
    python=$python; pythonVersion="$pythonVersion"; compilerVersion="$compilerVersion"; idfVersion=$idfVersion
    config=$required; deviceAec=$config.USE_DEVICE_AEC; serverAec=$config.USE_SERVER_AEC
    appPartitionBytes=0x3f0000; appFreeBytes=(0x3f0000-$appBytes)
    artifacts=$artifacts; inputHashes=$inputs; passedChecks=$checks.ToArray()
}
$evidence = Join-Path $project '.jafr/evidence'
New-Item -ItemType Directory -Force $evidence | Out-Null
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'baseline-artifacts.json') -Encoding utf8
Write-Output "Finished: $((Get-Date).ToString('o')); $($checks.Count) host checks passed. Hardware acceptance is separate."
