#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildDir = 'build_safe',
    [string]$Cxx = ''
)

# Compile-time acceptance scenarios run on the host. No link, board, serial port,
# hardware SDK headers or external provider is needed. -Cxx accepts a host g++/
# clang++ too; default to the same C++ compiler as this firmware's build cache.
$ErrorActionPreference = 'Stop'
$firmware = Split-Path $PSScriptRoot -Parent
if (!$Cxx) {
    $descriptionPath = Join-Path $firmware "$BuildDir/project_description.json"
    $description = Get-Content -LiteralPath $descriptionPath -Raw | ConvertFrom-Json
    $Cxx = $description.c_compiler -replace 'gcc(\.exe)?$', 'g++$1'
}
if (!(Test-Path -LiteralPath $Cxx -PathType Leaf)) {
    throw "C++ compiler not found: $Cxx. Supply -Cxx or configure the firmware build first."
}
$test = Join-Path $firmware 'tests/eye_conversation_state_test.cc'
Write-Output "Started: $((Get-Date).ToString('o'))"
& $Cxx -std=c++17 -Wall -Wextra -Werror -pedantic -fsyntax-only $test
if ($LASTEXITCODE -ne 0) { throw "Eye conversation state acceptance failed (exit $LASTEXITCODE)." }
Write-Output 'PASS: 24 compile-time acceptance checks for attention, expression, turn cleanup, PCM gating, and repeated-event continuity.'
Write-Output 'Scope: compiled production state reducer; real-time rendering and subjective naturalness require device/user acceptance.'
