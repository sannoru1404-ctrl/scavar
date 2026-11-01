<#
.SYNOPSIS
  Downloads the SQLite amalgamation (public domain) into Source/ScavarPersistence/ThirdParty/sqlite.
  Pass the SHA3-256/SHA256 published on https://www.sqlite.org/download.html to verify the download.
.EXAMPLE
  .\tools\fetch_sqlite.ps1 -Year 2025 -Version 3490100 -Sha256 <hash from sqlite.org>
#>
param(
  [Parameter(Mandatory)] [string] $Year,
  [Parameter(Mandatory)] [string] $Version,
  [Parameter(Mandatory)] [string] $Sha256
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Dst = Join-Path $Root 'Source\ScavarPersistence\ThirdParty\sqlite'
$Zip = Join-Path $env:TEMP "sqlite-amalgamation-$Version.zip"
Invoke-WebRequest "https://www.sqlite.org/$Year/sqlite-amalgamation-$Version.zip" -OutFile $Zip
$actual = (Get-FileHash $Zip -Algorithm SHA256).Hash
if ($actual -ne $Sha256.ToUpper()) { throw "SHA256 mismatch: expected $Sha256, got $actual" }
$Tmp = Join-Path $env:TEMP "sqlite-$Version"
Expand-Archive $Zip -DestinationPath $Tmp -Force
New-Item -ItemType Directory -Force -Path $Dst | Out-Null
Get-ChildItem "$Tmp\sqlite-amalgamation-$Version\*" -Include sqlite3.c, sqlite3.h, sqlite3ext.h | Copy-Item -Destination $Dst -Force
"SQLite amalgamation $Version (public domain), SHA256 $actual" | Set-Content (Join-Path $Dst 'SOURCE.txt')
Write-Host "SQLite $Version installed to $Dst"
