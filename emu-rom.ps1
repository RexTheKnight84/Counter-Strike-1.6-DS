# Copies the freshly built ROM to build\emu for melonDS, marked as homebrew
# (game code ####) so melonDS's DS mode gives it the SD card (DLDI).
$src = Join-Path $PSScriptRoot 'half-life-nds-port.nds'
$dst = Join-Path $PSScriptRoot 'build\emu\half-life-nds-port.nds'
$b = [System.IO.File]::ReadAllBytes($src)
for ($i = 12; $i -lt 16; $i++) { $b[$i] = 0x23 }
$crc = 0xFFFF
for ($i = 0; $i -lt 0x15E; $i++) {
    $crc = $crc -bxor $b[$i]
    for ($k = 0; $k -lt 8; $k++) {
        if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xA001 } else { $crc = $crc -shr 1 }
    }
}
$b[0x15E] = $crc -band 0xFF
$b[0x15F] = ($crc -shr 8) -band 0xFF
[System.IO.File]::WriteAllBytes($dst, $b)
Write-Host "melonDS ROM ready: $dst"
