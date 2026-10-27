param(
    [ValidateSet('compile','run','test','package','installer')][string]$Action='compile',
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [int]$Jobs=4,
    [string]$InnoPath='',
    [switch]$NoPause
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "$Program failed with exit code $LASTEXITCODE"}
}
try {
    if($Jobs -lt 1 -or $Jobs -gt 64){throw 'Jobs must be between 1 and 64.'}
    $build=Join-Path $projectRoot 'native-build'
    $configure=@('-S',$projectRoot,'-B',$build)
    if(!(Test-Path (Join-Path $build 'CMakeCache.txt'))){$configure+=@('-G','Visual Studio 17 2022','-A','x64')}
    Invoke-Checked 'cmake.exe' $configure
    Invoke-Checked 'cmake.exe' @('--build',$build,'--config',$Configuration,'--target','PhthongosJUCE','--parallel',"$Jobs")
    if($Action -eq 'compile'){exit 0}
    $arguments=@('-X','utf8','package_juce.py','--configuration',$Configuration)
    if($Action -eq 'run' -or $Action -eq 'test'){$arguments+='--dev'}
    $lines=@(& python.exe @arguments)
    if($LASTEXITCODE -ne 0){throw 'Runtime staging failed.'}
    $runtime=$lines[-1]
    if($Action -eq 'run'){
        Start-Process -FilePath (Join-Path $runtime 'Phthongos.exe') -WorkingDirectory $runtime -WindowStyle Normal
    } elseif($Action -eq 'test'){
        Invoke-Checked 'python.exe' @('-X','utf8','tools/check_i18n.py')
        $exe=Join-Path $runtime 'Phthongos.exe'
        foreach($arguments in @(@('--localisation-test'),@('--language','fr','--basic-pitch-test'),@('--language','en','--basic-pitch-test'))){
            $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $runtime -Wait -PassThru -WindowStyle Hidden
            if($process.ExitCode -ne 0){throw "Application checks failed: $arguments"}
        }
    } elseif($Action -eq 'installer'){
        if(!$InnoPath){throw 'Supply -InnoPath pointing to Inno Setup ISCC.exe.'}
        Invoke-Checked $InnoPath @("/DPackageDir=$runtime","/DOutputDir=$projectRoot/installers",'installer/Phthongos.iss')
    } else {Write-Host "Prepared native package: $runtime"}
} catch {Write-Error $_; exit 1}
