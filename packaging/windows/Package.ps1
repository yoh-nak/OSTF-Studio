param(
    [Parameter(Mandatory)] [string] $AppVersion,
    [Parameter(Mandatory)] [string] $BinaryVersion,
    [string] $AppExe = 'build/OSTFStudio_artefacts/Release/OSTF Studio.exe',
    [string] $OutputDir = 'dist',
    [string] $InnoCompiler = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
    [string] $GmBundle = 'build/OSTFGMSoundModule_artefacts/Release/VST3/OSTF GM Sound Module.vst3',
    [string] $ArrangerBundle = 'build/OSTFArranger_artefacts/Release/VST3/OSTF Arranger.vst3',
    [string] $EffectsDir = 'build/StudioEffects/Release'
)

$ErrorActionPreference = 'Stop'
$AppExe = (Get-Item -LiteralPath $AppExe).FullName
$InnoCompiler = (Get-Command $InnoCompiler -CommandType Application -ErrorAction Stop).Source
$OutputDir = (New-Item -Path $OutputDir -ItemType Directory -Force).FullName
$installer = Join-Path $PSScriptRoot 'OSTFStudio.iss'
$GmBundle = (Get-Item -LiteralPath $GmBundle -ErrorAction Stop).FullName
$ArrangerBundle = (Get-Item -LiteralPath $ArrangerBundle -ErrorAction Stop).FullName
$EffectsDir = (Get-Item -LiteralPath $EffectsDir -ErrorAction Stop).FullName
$catalog = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../src/plugins/effects/catalog.json') -Raw | ConvertFrom-Json
$fxBundles = @(foreach ($effect in $catalog) {
    $bundle = Join-Path $EffectsDir ($effect.name + '.vst3')
    $null = Get-Item -LiteralPath (Join-Path $bundle ("Contents/x86_64-win/" + $effect.name + ".vst3")) -ErrorAction Stop
    $null = Get-Item -LiteralPath (Join-Path $bundle 'Contents/Resources/Licenses/GPL-3.0.txt') -ErrorAction Stop
    $bundle
})
if (@(Get-ChildItem -LiteralPath $EffectsDir -Force).Count -ne $fxBundles.Count) { throw "Unexpected entries in $EffectsDir" }
$GmLicense = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '../../third_party/tinysoundfont/LICENSE.txt')).FullName
$Licenses = (Get-Item -LiteralPath (Join-Path (Split-Path -Parent $AppExe) 'Licenses') -ErrorAction Stop).FullName
if (!(Test-Path -LiteralPath (Join-Path $GmBundle 'Contents/x86_64-win/OSTF GM Sound Module.vst3') -PathType Leaf)) {
    throw "Missing Windows x64 GM Sound Module binary in $GmBundle"
}

if (!(Test-Path -LiteralPath (Join-Path $ArrangerBundle 'Contents/x86_64-win/OSTF Arranger.vst3') -PathType Leaf)) {
    throw "Missing Windows x64 OSTF Arranger binary in $ArrangerBundle"
}
& $InnoCompiler "/DAppVersion=$AppVersion" "/DBinaryVersion=$BinaryVersion" "/DAppExe=$AppExe" "/DOutputDir=$OutputDir" "/DGmBundle=$GmBundle" "/DArrangerBundle=$ArrangerBundle" "/DEffectsDir=$EffectsDir" "/DGmLicense=$GmLicense" "/DLicenses=$Licenses" $installer
if ($LASTEXITCODE -ne 0) { throw "Inno Setup exited with code $LASTEXITCODE" }

$portableZip = Join-Path $OutputDir 'OSTF_Studio_Windows_x64_Portable.zip'
Compress-Archive -LiteralPath (@($AppExe, $GmBundle, $ArrangerBundle, $GmLicense, $Licenses) + $fxBundles) -DestinationPath $portableZip -Force
