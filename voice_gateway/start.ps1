[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$pythonPath = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (!(Test-Path -LiteralPath $pythonPath)) { throw 'Create the pinned voice_gateway environment first; see README.md.' }
if (!$env:MISTRAL_API_KEY) { throw 'Set MISTRAL_API_KEY in the process or user environment before starting the gateway.' }
if (!$env:JAPA_MISTRAL_VOICE_ID) { $env:JAPA_MISTRAL_VOICE_ID = 'cbe96cf0-85ec-4a10-accb-0b35c93b6dfd' }
if (Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue) { throw 'Port 8765 already has a listener; existing service was not interrupted.' }
$runtimeRoot = Join-Path $projectRoot '.jafr\gateway-runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
$process = Start-Process -FilePath $pythonPath -ArgumentList @('-m', 'voice_gateway.server') `
    -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $runtimeRoot 'stdout.log') `
    -RedirectStandardError (Join-Path $runtimeRoot 'stderr.log')
Write-Output "Started gateway launcher PID $($process.Id). Control board: http://localhost:8765"
Write-Output 'This is a host process, not an installed startup service. Xiaozhi does not depend on it.'
