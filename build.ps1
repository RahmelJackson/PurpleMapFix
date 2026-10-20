# Purple Map Fix - one-click build
# Run through build.bat (or install-tools-and-build.bat to also install missing tools).
# Everything printed here is also saved to build-log.txt next to this script.

param([switch]$InstallMissing)

$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
Set-Location $root
Start-Transcript -Path (Join-Path $root 'build-log.txt') -Force | Out-Null

function Done($code) {
    if ($code -eq 0) {
        Write-Host "`nRESULT: SUCCESS" -ForegroundColor Green
    } else {
        Write-Host "`nRESULT: FAILED (code $code)" -ForegroundColor Red
    }
    Stop-Transcript | Out-Null
    Write-Host "`nYou can close this window."
    exit $code
}

# Tools installed moments ago aren't on this window's PATH yet; add the usual spots.
$env:Path = "$env:ProgramFiles\Git\cmd;$env:ProgramFiles\xmake;$env:LOCALAPPDATA\Programs\xmake;$env:Path"

function Have($exe) { [bool](Get-Command $exe -ErrorAction SilentlyContinue) }

function Find-MSVC {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

Write-Host "== Checking tools =="
$git = Have 'git'
$xmake = Have 'xmake'
$msvc = Find-MSVC
Write-Host ("Git:          " + $(if ($git) { 'found' } else { 'MISSING' }))
Write-Host ("xmake:        " + $(if ($xmake) { 'found' } else { 'MISSING' }))
Write-Host ("C++ compiler: " + $(if ($msvc) { "found ($msvc)" } else { 'MISSING' }))

if (-not ($git -and $xmake -and $msvc)) {
    if (-not $InstallMissing) {
        Write-Host "`nSome tools are missing. Run install-tools-and-build.bat to install them, or install them yourself (see README.md)."
        Done 2
    }
    if (-not (Have 'winget')) {
        Write-Host "winget isn't available, so tools can't be installed automatically. See README.md for manual install links."
        Done 3
    }
    $common = @('--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    if (-not $git) {
        Write-Host "`n== Installing Git =="
        winget install --id Git.Git @common
    }
    if (-not $xmake) {
        Write-Host "`n== Installing xmake =="
        winget install --id Xmake-io.Xmake @common
    }
    if (-not $msvc) {
        Write-Host "`n== Installing Visual Studio 2022 Build Tools (C++). This is a large download and can take 10-30 minutes =="
        winget install --id Microsoft.VisualStudio.2022.BuildTools @common --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    }
    $env:Path = "$env:ProgramFiles\Git\cmd;$env:ProgramFiles\xmake;$env:LOCALAPPDATA\Programs\xmake;$env:Path"
    if (-not ((Have 'git') -and (Have 'xmake') -and (Find-MSVC))) {
        Write-Host "`nA tool still isn't found after installing. Restart the PC and run build.bat again."
        Done 4
    }
}

Write-Host "`n== Getting CommonLibSSE-NG =="
# Pinned to the commit the code was written and tested against.
$commonLibCommit = 'b93280e832f263dbef44e44cbe2936622a02f91a'
if (-not (Test-Path (Join-Path $root 'lib\commonlibsse-ng\xmake.lua'))) {
    git clone --recurse-submodules https://github.com/CharmedBaryon/CommonLibSSE-NG.git lib\commonlibsse-ng
    if ($LASTEXITCODE -ne 0) { Write-Host "Download failed."; Done 5 }
} else {
    Write-Host "Already downloaded."
}
git -C lib\commonlibsse-ng checkout --quiet $commonLibCommit
if ($LASTEXITCODE -ne 0) { git -C lib\commonlibsse-ng fetch --quiet origin; git -C lib\commonlibsse-ng checkout --quiet $commonLibCommit }
if ($LASTEXITCODE -ne 0) { Write-Host "Couldn't check out CommonLibSSE-NG $commonLibCommit."; Done 5 }
git -C lib\commonlibsse-ng submodule update --init --recursive --quiet

Write-Host "`n== Building (first build takes several minutes) =="
xmake f -m releasedbg -y
if ($LASTEXITCODE -ne 0) { Done 6 }
xmake -y
if ($LASTEXITCODE -ne 0) { Done 7 }

$dll = Join-Path $root 'dist\SKSE\Plugins\PurpleMapFix.dll'
if (-not (Test-Path $dll)) { Write-Host "Build finished but the DLL wasn't found in dist."; Done 8 }

Write-Host "`n== Packaging =="
$zip = Join-Path $root 'PurpleMapFix-mod.zip'
if (Test-Path $zip) { Remove-Item $zip }
Get-ChildItem (Join-Path $root 'dist') | Where-Object { $_.Extension -ne '.pdb' } |
    Compress-Archive -DestinationPath $zip
Write-Host "Mod ready to install: $zip"
Done 0
