# Builds Counter-Strike DS: a ROM of its own, with the game in it -- nothing
# else needed on the card (tools/cs16_rom.py). The Half-Life ROM is built as
# always beside it, and left as it is.
#
#   powershell -ExecutionPolicy Bypass -File .\build-cs16.ps1
#
# The game put in it is the one the converter made on your card folder
# (cook-map ... -o <CardData>): convert the maps first. The ROM is yours
# alone -- it holds your copy of the game: do not share it, nor commit it.
#
#   .\build-cs16.ps1 -CardData <card>\_nds\hl_nds\data\cstrike -GameIcon <Half-Life>\cstrike\game.tga
param(
    [Parameter(Mandatory = $true)][string]$CardData,
    [string]$GameIcon = ''
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not (Test-Path (Join-Path $CardData 'manifest.bin'))) {
    throw "No converted Counter-Strike at $CardData (cook the maps first)."
}
$local = Join-Path $PSScriptRoot 'local\cs16'
$game = Join-Path $local 'cstrike'
if (Test-Path $game) { Remove-Item -Recurse -Force $game }
New-Item -ItemType Directory -Force -Path $game | Out-Null
Copy-Item (Join-Path $CardData 'common.npac') $game
Copy-Item (Join-Path $CardData 'manifest.bin') $game
Copy-Item -Recurse (Join-Path $CardData 'maps') $game
if (Test-Path (Join-Path $CardData 'music')) { Copy-Item -Recurse (Join-Path $CardData 'music') $game }
# (What a console with no card is sent by Download Play: the converter's air/.)
if (Test-Path (Join-Path $CardData 'air')) { Copy-Item -Recurse (Join-Path $CardData 'air') $game }
else { Write-Host 'No air/ in the converted game: cook the maps again for Download Play.' }
$iconArg = ''
if ($GameIcon -and (Test-Path $GameIcon)) {
    Copy-Item $GameIcon (Join-Path $local 'icon.tga')
    $iconArg = '--icon local/cs16/icon.tga'
}

docker compose run --rm dev ./make.sh
if ($LASTEXITCODE -ne 0) { throw 'make.sh failed' }
docker compose run --rm dev bash -c "source `"`${HLDS_VENV:-env}/bin/activate`" && python tools/cs16_rom.py --data local/cs16/cstrike $iconArg --out cs16-ds.nds --emu build/emu/cs16-ds.nds"
if ($LASTEXITCODE -ne 0) { throw 'cs16_rom.py failed' }
Write-Host ''
Write-Host 'Counter-Strike DS: cs16-ds.nds (for the DSi / 3DS, TWiLight Menu++)'
Write-Host '          melonDS: build\emu\cs16-ds.nds (DS mode)'
