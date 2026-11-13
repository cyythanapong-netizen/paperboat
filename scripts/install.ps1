# Install or update paperboat for this user on Windows, with nothing but this script:
#
#   irm https://raw.githubusercontent.com/paperboatlab/paperboat/HEAD/scripts/install.ps1 | iex
#
#   (no flag)        the newest GitHub release's .zip (prereleases included)
#   -Version V       that release instead of the newest
#   -Archive FILE    install this release .zip instead of downloading one (offline, or CI)
#   -Check           say what is installed and what is available, change nothing
#
# Running it again updates; `paperboat update` runs it, and so does the one-file paperboat.exe on its first
# run. Layout: $env:PAPERBOAT_HOME (default %LOCALAPPDATA%\paperboat) holds self\<version>\ and apps\
# (`paperboat pkg`); bin\paperboat.cmd runs the one in use, and bin\ is put on the user's PATH.
# The source and main channels are POSIX only (scripts/install.sh); Windows takes releases.
param([string]$Version = "", [string]$Archive = "", [switch]$Check)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is many times slower drawing its bar
$Repo = "paperboatlab/paperboat"
$Root = if ($env:PAPERBOAT_HOME) { $env:PAPERBOAT_HOME } else { Join-Path $env:LOCALAPPDATA "paperboat" }
$Bin = if ($env:PAPERBOAT_BIN) { $env:PAPERBOAT_BIN } else { Join-Path $Root "bin" }
$Shim = Join-Path $Bin "paperboat.cmd"

# What is installed now: the version the shim points at.
$have = ""
if (Test-Path $Shim) {
  $m = [regex]::Match((Get-Content $Shim -Raw), 'self\\([^\\]+)\\bin')
  if ($m.Success) { $have = $m.Groups[1].Value }
}
# paperboat-<version>-Windows-<arch>.zip
if ($Archive) { $Version = [IO.Path]::GetFileName($Archive) -replace '^paperboat-(.*)-Windows-[^-]*\.zip$', '$1' }
if (-not $Version) {
  $Version = ((Invoke-RestMethod "https://api.github.com/repos/$Repo/releases?per_page=1")[0].tag_name) -replace '^v', ''
  if (-not $Version) { throw "cannot read the releases of $Repo (offline, or rate limited)" }
}

if ($Check) {
  Write-Output "installed: $(if ($have) { $have } else { 'nothing' }) (release)"
  Write-Output "available: $Version (release)"
  exit 0
}
if ($have -eq $Version) { Write-Output "paperboat $Version is up to date"; exit 0 }

$dest = Join-Path $Root "self\$Version"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("paperboat-" + [guid]::NewGuid())
New-Item -ItemType Directory $tmp | Out-Null
try {
  $zip = Join-Path $tmp "pb.zip"
  if ($Archive) {
    Write-Output "installing $Archive…"
    Copy-Item $Archive $zip
  } else {
    $asset = "paperboat-$Version-Windows-AMD64.zip"
    Write-Output "downloading $asset…"
    Invoke-WebRequest "https://github.com/$Repo/releases/download/v$Version/$asset" -OutFile $zip
  }
  Expand-Archive $zip (Join-Path $tmp "x")
  $top = Get-ChildItem (Join-Path $tmp "x") -Directory | Select-Object -First 1
  if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
  New-Item -ItemType Directory (Split-Path $dest) -Force | Out-Null
  Move-Item $top.FullName $dest
} finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
# ponytail: older versions stay in self\ (a rollback is editing the shim); prune by hand if disk matters.
New-Item -ItemType Directory $Bin -Force | Out-Null
Set-Content $Shim "@`"$dest\bin\paperboat.exe`" %*" -Encoding ASCII
$path = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not (($path -split ';') -contains $Bin)) {
  [Environment]::SetEnvironmentVariable("Path", "$Bin;$path", "User")
  Write-Output "added $Bin to your PATH (open a new terminal)"
}
Write-Output "paperboat → $dest\bin\paperboat.exe"
Write-Output "installed paperboat $Version"
