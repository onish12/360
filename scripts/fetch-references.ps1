$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$external = Join-Path $root 'external'
New-Item -ItemType Directory -Force -Path $external | Out-Null

$repos = @(
    @{ Name='sklhdaudbus'; Url='https://github.com/coolstar/sklhdaudbus.git' },
    @{ Name='da7219'; Url='https://github.com/coolstar/da7219.git' },
    @{ Name='max98357a'; Url='https://github.com/coolstar/max98357a.git' },
    @{ Name='sof'; Url='https://github.com/thesofproject/sof.git' }
)

foreach ($r in $repos) {
    $dst = Join-Path $external $r.Name
    if (Test-Path $dst) {
        Write-Host "SKIP $($r.Name): already exists"
        continue
    }
    git clone --depth 1 $r.Url $dst
    if ($LASTEXITCODE -ne 0) { throw "git clone failed: $($r.Url)" }
}
