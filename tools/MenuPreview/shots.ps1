# Renders every menu page at four sizes into _PLAN/menu-shots/ (gitignored). Usage: shots.ps1 [-Tag before|after] [-Build]
param([string]$Tag = 'shot', [switch]$Build)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'bin\MenuPreview.exe'
if ($Build -or -not (Test-Path $exe)) {
    $ms = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    & $ms (Join-Path $here 'MenuPreview.vcxproj') /p:Configuration=Release /p:Platform=Win32 -nologo -v:m
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}
$outDir = Join-Path $here '..\..\..\_PLAN\menu-shots'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outDir = (Resolve-Path $outDir).Path

$pages = @(
    @{ name = 'settings-bloom';       args = @('--window','settings','--select','Shaders.Bloom') },
    @{ name = 'settings-shadows';     args = @('--window','settings','--select','Shaders.ShadowsExteriors') },
    @{ name = 'settings-performance'; args = @('--window','settings','--select','@Performance') },
    @{ name = 'settings-style';       args = @('--window','settings','--select','@Style') },
    @{ name = 'locations-presets';    args = @('--window','locations','--tab','presets','--preset','Neon Nights Strip Glow') },
    @{ name = 'locations-current';    args = @('--window','locations','--tab','current') }
)
$sizes = @('1280x720','1920x1080','2560x1440','3840x2160')
foreach ($p in $pages) {
    foreach ($s in $sizes) {
        $file = Join-Path $outDir ("{0}-{1}-{2}.png" -f $Tag, $p.name, $s)
        & $exe @($p.args) --size $s --out $file --frames 4 | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Warning "failed: $file" }
    }
}
Write-Host "done: $outDir"
