# Baut den Windows-Installer von AutoNeg (Inno Setup 6).
#
#   powershell -ExecutionPolicy Bypass -File installer\windows\build_installer.ps1
#
# Schritte: Release bauen (windows-clang), mit "cmake --install" nach build\install-windows
# legen, optional das Handbuch (docs\handbuch\handbuch.pdf) dazulegen, dann ISCC aufrufen.
# Ergebnis: build\installer\AutoNeg-<Version>-Setup-x64.exe
#
# Voraussetzungen: Visual Studio 2022 bzw. Build Tools (vcvars64.bat), LLVM (clang-cl),
# VCPKG_ROOT, Inno Setup 6 (winget install JRSoftware.InnoSetup).

param(
    [switch]$SkipBuild # vorhandenen Release-Bau verwenden
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$stage = Join-Path $repo 'build\install-windows'
$output = Join-Path $repo 'build\installer'

# Version aus CMakeLists.txt (project(... VERSION x.y.z))
$cmakeLists = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw 'Version in CMakeLists.txt nicht gefunden' }
$version = $Matches[1]

# Entwicklungsumgebung (vcvars64.bat) über vswhere finden
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat' }
}
if (-not $vcvars -or -not (Test-Path $vcvars)) { throw 'vcvars64.bat nicht gefunden (Visual Studio 2022 bzw. Build Tools)' }

# Inno Setup: Suchpfad, Benutzer- oder Systeminstallation
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
foreach ($candidate in @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
                         "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                         "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
    if (-not $iscc -and (Test-Path $candidate)) { $iscc = $candidate }
}
if (-not $iscc) { throw 'ISCC.exe nicht gefunden: winget install JRSoftware.InnoSetup' }

# Bauen und installieren (in der VS-Umgebung, clang-cl aus LLVM)
# Nur den Inhalt leeren: der Ordner selbst kann von Explorer oder Virenscanner offen gehalten werden
if (Test-Path $stage) { Get-ChildItem -Force $stage | Remove-Item -Recurse -Force }
$steps = @()
if (-not $SkipBuild) {
    $steps += 'cmake --preset windows-clang'
    $steps += 'cmake --build --preset windows-clang-release --target autoneg autoneg-cli'
}
$steps += "cmake --install build\windows-clang --config Release --prefix `"$stage`""
$script = "call `"$vcvars`" >nul && set `"PATH=C:\Program Files\LLVM\bin;%PATH%`" && cd /d `"$repo`" && " + ($steps -join ' && ')
cmd /c $script
if ($LASTEXITCODE -ne 0) { throw "Bauen bzw. Installieren fehlgeschlagen ($LASTEXITCODE)" }

foreach ($required in @('autoneg.exe', 'autoneg-cli.exe', 'qt.conf', 'plugins\platforms\qwindows.dll', 'vcomp140.dll',
                      'msvcp140.dll', 'vcruntime140.dll')) {
    if (-not (Test-Path (Join-Path $stage $required))) { throw "Installation unvollständig: $required fehlt" }
}

# Handbuch, falls gesetzt (docs\handbuch\handbuch.tex mit pdflatex o. ä.)
$manual = Join-Path $repo 'docs\handbuch\handbuch.pdf'
if (Test-Path $manual) {
    Copy-Item $manual (Join-Path $stage 'handbuch.pdf')
} else {
    Write-Host 'Hinweis: docs\handbuch\handbuch.pdf fehlt; der Installer enthält kein Handbuch.'
}

New-Item -ItemType Directory -Force $output | Out-Null
& $iscc "/DStageDir=$stage" "/DAppVersion=$version" "/DOutputDir=$output" (Join-Path $PSScriptRoot 'autoneg.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC fehlgeschlagen ($LASTEXITCODE)" }
Write-Host "Installer: $output\AutoNeg-$version-Setup-x64.exe"
