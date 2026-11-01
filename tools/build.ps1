<#
.SYNOPSIS
  Builds SCAVAR targets on Windows and keeps a log (AT-049).
.EXAMPLE
  .\tools\build.ps1 -Target Editor
  .\tools\build.ps1 -Target Server -Config Development
.NOTES
  Set SCAVAR_UE_ROOT to the engine root, e.g. C:\Program Files\Epic Games\UE_5.8 (launcher) or D:\UE\UnrealEngine (source build).
  Server and Client targets REQUIRE a source-built engine; the launcher build can only build Game and Editor.
#>
param(
  [ValidateSet('Editor', 'Game', 'Server', 'Client')] [string] $Target = 'Editor',
  [ValidateSet('Development', 'DebugGame', 'Shipping')] [string] $Config = 'Development',
  [string] $EngineRoot = $env:SCAVAR_UE_ROOT
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot -or -not (Test-Path $EngineRoot)) { throw "Set SCAVAR_UE_ROOT or pass -EngineRoot (engine root folder)." }

$Name = switch ($Target) { 'Editor' { 'SCAVAREditor' } 'Game' { 'SCAVAR' } 'Server' { 'SCAVARServer' } 'Client' { 'SCAVARClient' } }
$Ubt = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$Project = Join-Path $Root 'SCAVAR.uproject'
$LogDir = Join-Path $Root 'Saved\BuildLogs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Log = Join-Path $LogDir ("{0}-{1}-{2}.log" -f $Name, $Config, (Get-Date -Format 'yyyyMMdd-HHmmss'))

# Balance tables are staged next to the content so packaged builds find them.
$DataDst = Join-Path $Root 'Content\Data'
New-Item -ItemType Directory -Force -Path $DataDst | Out-Null
Copy-Item (Join-Path $Root 'data\*.csv') $DataDst -Force

$sw = [Diagnostics.Stopwatch]::StartNew()
& $Ubt $Name Win64 $Config "-Project=$Project" -WaitMutex -NoHotReload 2>&1 | Tee-Object -FilePath $Log
$code = $LASTEXITCODE
$sw.Stop()
Write-Host ("Build {0} {1} finished with exit code {2} in {3:n1} min. Log: {4}" -f $Name, $Config, $code, $sw.Elapsed.TotalMinutes, $Log)
exit $code
