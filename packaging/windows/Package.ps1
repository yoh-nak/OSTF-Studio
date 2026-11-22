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
$stage = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ('ostf-vst3-' + [guid]::NewGuid()))).FullName
$pluginSetup = Join-Path $stage 'OSTF-Studio-VST3-Setup.exe'
$migrationList = Join-Path $stage 'migrate.iss'
try {
    $migration = foreach ($bundle in (@($GmBundle, $ArrangerBundle) + $fxBundles)) {
        $name = Split-Path -Leaf $bundle
        $info = Get-Content -LiteralPath (Join-Path $bundle 'Contents/Resources/moduleinfo.json') -Raw
        if ($info -notmatch '"CID"\s*:\s*"([A-Fa-f0-9]{32})"') { throw "Missing VST3 class ID in $name" }
        $cid = $Matches[1]
        $legacy = if ($bundle -in $fxBundles) { 'OSTF Studio Effects\' } else { '' }
        "MigrateBundle('$legacy', '$name', '$cid');"
    }
    Set-Content -LiteralPath $migrationList -Value $migration -Encoding ascii
    & $InnoCompiler "/DAppVersion=$AppVersion" "/DBinaryVersion=$BinaryVersion" "/DOutputDir=$stage" "/DGmBundle=$GmBundle" "/DArrangerBundle=$ArrangerBundle" "/DEffectsDir=$EffectsDir" (Join-Path $PSScriptRoot 'StudioVST3.iss')
    if ($LASTEXITCODE -ne 0) { throw "VST3 installer build failed: $LASTEXITCODE" }
    & $InnoCompiler "/DAppVersion=$AppVersion" "/DBinaryVersion=$BinaryVersion" "/DAppExe=$AppExe" "/DOutputDir=$OutputDir" "/DPluginSetup=$pluginSetup" "/DMigrationList=$migrationList" "/DGmLicense=$GmLicense" "/DLicenses=$Licenses" $installer
    if ($LASTEXITCODE -ne 0) { throw "Application installer build failed: $LASTEXITCODE" }
} finally {
    foreach ($temporary in @($migrationList, $pluginSetup)) {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary }
    }
    [IO.Directory]::Delete($stage)
}

$portableZip = Join-Path $OutputDir 'OSTF_Studio_Windows_x64_Portable.zip'
Compress-Archive -LiteralPath (@($AppExe, $GmBundle, $ArrangerBundle, $GmLicense, $Licenses) + $fxBundles) -DestinationPath $portableZip -Force
