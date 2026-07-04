$ErrorActionPreference = 'Stop'
$srcPath = 'c:\Users\berta\Desktop\eBookReaderSwitch\eBookReaderSwitch.nro'
$srcName = 'eBookReaderSwitch.nro'
$tmpDir  = 'c:\Users\berta\Desktop\eBookReaderSwitch\_verify_tmp'
$expectedLen = (Get-Item $srcPath).Length
$localHash = (Get-FileHash $srcPath -Algorithm SHA256).Hash

if (Test-Path $tmpDir) { Remove-Item $tmpDir -Recurse -Force }
New-Item -ItemType Directory -Path $tmpDir | Out-Null

$shell = New-Object -ComObject Shell.Application
$switch = $shell.Namespace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' } | Select-Object -First 1
$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match 'microSD|SD card' } | Select-Object -First 1
$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -eq 'switch' } | Select-Object -First 1
$onSd = $sw.GetFolder.Items() | Where-Object { $_.Name -eq $srcName } | Select-Object -First 1
if (-not $onSd) { Write-Host 'NRO not found on SD!'; exit 1 }

Write-Host 'Copying nro back from SD for verification...'
$tmpNs = $shell.Namespace($tmpDir)
$tmpNs.CopyHere($onSd, 16)

$deadline = (Get-Date).AddSeconds(180)
$local = Join-Path $tmpDir $srcName
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if (Test-Path $local) {
        $len = (Get-Item $local).Length
        Write-Host ("copied-back size = {0} / expected {1}" -f $len, $expectedLen)
        if ($len -eq $expectedLen) { break }
    } else { Write-Host 'copying back...' }
}

if (-not (Test-Path $local)) { Write-Host 'FAILED: copy-back did not produce file'; exit 1 }
$sdHash = (Get-FileHash $local -Algorithm SHA256).Hash
Write-Host ("local SHA256 : {0}" -f $localHash)
Write-Host ("on-SD SHA256 : {0}" -f $sdHash)
if ($sdHash -eq $localHash) { Write-Host 'MATCH: the fixed build IS deployed on the Switch.' }
else { Write-Host 'MISMATCH: the SD still has a different (old) build.' }
Remove-Item $tmpDir -Recurse -Force
