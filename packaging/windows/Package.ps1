param(
    [Parameter(Mandatory)] [string] $AppVersion,
    [Parameter(Mandatory)] [string] $BinaryVersion,
    [string] $AppExe = 'build/OSTFStudio_artefacts/Release/OSTF Studio.exe',
    [string] $OutputDir = 'dist',
    [string] $InnoCompiler = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
)

$ErrorActionPreference = 'Stop'
$AppExe = (Get-Item -LiteralPath $AppExe).FullName
$InnoCompiler = (Get-Command $InnoCompiler -CommandType Application -ErrorAction Stop).Source
$OutputDir = (New-Item -Path $OutputDir -ItemType Directory -Force).FullName
$installer = Join-Path $PSScriptRoot 'OSTFStudio.iss'

& $InnoCompiler "/DAppVersion=$AppVersion" "/DBinaryVersion=$BinaryVersion" "/DAppExe=$AppExe" "/DOutputDir=$OutputDir" $installer
if ($LASTEXITCODE -ne 0) { throw "Inno Setup exited with code $LASTEXITCODE" }

$portableZip = Join-Path $OutputDir 'OSTF_Studio_Windows_x64_Portable.zip'
Compress-Archive -LiteralPath $AppExe -DestinationPath $portableZip -Force
