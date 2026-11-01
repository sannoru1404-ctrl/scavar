<#
.SYNOPSIS
  Downloads the SQLite amalgamation (public domain) into Source/ScavarPersistence/ThirdParty/sqlite and verifies its SHA3-256.
.DESCRIPTION
  1. Open https://www.sqlite.org/download.html and find "C source code as an amalgamation" (sqlite-amalgamation-NNNNNNN.zip).
  2. Take the year folder from the link (e.g. 2025), the version digits from the file name, and the SHA3-256 shown next to it.
.EXAMPLE
  .\tools\fetch_sqlite.ps1 -Year 2025 -Version 3490100 -Sha3 <SHA3-256 from sqlite.org>
#>
param(
  [Parameter(Mandatory)] [string] $Year,
  [Parameter(Mandatory)] [string] $Version,
  [Parameter(Mandatory)] [string] $Sha3
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Dst = Join-Path $Root 'Source\ScavarPersistence\ThirdParty\sqlite'
$Zip = Join-Path $env:TEMP "sqlite-amalgamation-$Version.zip"
Invoke-WebRequest "https://www.sqlite.org/$Year/sqlite-amalgamation-$Version.zip" -OutFile $Zip

# PowerShell has no SHA3; Python's hashlib does.
$actual = (python -c "import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" $Zip).Trim()
if ($actual.ToLower() -ne $Sha3.Trim().ToLower()) { throw "SHA3-256 mismatch: expected $Sha3, got $actual" }

$Tmp = Join-Path $env:TEMP "sqlite-$Version"
Expand-Archive $Zip -DestinationPath $Tmp -Force
New-Item -ItemType Directory -Force -Path $Dst | Out-Null
Get-ChildItem "$Tmp\sqlite-amalgamation-$Version\*" -Include sqlite3.c, sqlite3.h, sqlite3ext.h | Copy-Item -Destination $Dst -Force
"SQLite amalgamation $Version (public domain), SHA3-256 $actual" | Set-Content (Join-Path $Dst 'SOURCE.txt')
Write-Host "SQLite $Version installed to $Dst"
