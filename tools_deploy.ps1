$ErrorActionPreference = 'Stop'
$srcPath = 'c:\Users\berta\Desktop\eBookReaderSwitch\eBookReaderSwitch.nro'
$srcName = 'eBookReaderSwitch.nro'

$shell = New-Object -ComObject Shell.Application
$switch = $shell.Namespace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' } | Select-Object -First 1
$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match 'microSD|SD card' } | Select-Object -First 1
$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -eq 'switch' } | Select-Object -First 1
$swFolder = $sw.GetFolder

# Delete existing nro if present
$old = $swFolder.Items() | Where-Object { $_.Name -eq $srcName } | Select-Object -First 1
if ($old) {
    Write-Host 'Deleting existing nro on SD...'
    $delVerb = $old.Verbs() | Where-Object { $_.Name -match '[Ee]limina|[Dd]elete|ösch' } | Select-Object -First 1
    if ($delVerb) { $delVerb.DoIt() } else { Write-Host 'WARN: delete verb not found'; }
    # wait until it is actually gone
    $dl = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $dl) {
        Start-Sleep -Seconds 3
        $still = $swFolder.Items() | Where-Object { $_.Name -eq $srcName } | Select-Object -First 1
        if (-not $still) { Write-Host 'old nro deleted.'; break }
    }
}

Write-Host 'Copying new nro to /switch (this can take a while for 44MB over MTP)...'
$srcItem = $shell.Namespace((Split-Path $srcPath)).ParseName($srcName)
$swFolder.CopyHere($srcItem, 16)

# Poll for completion up to ~180s. MTP usually exposes the file only once the
# transfer has finished, so first stable appearance is our completion signal.
$deadline = (Get-Date).AddSeconds(180)
$seen = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    $now = $swFolder.Items() | Where-Object { $_.Name -eq $srcName } | Select-Object -First 1
    if ($now) {
        $seen++
        Write-Host ("present ({0}): modified={1}" -f $seen, $now.ModifyDate)
        if ($seen -ge 2) { Write-Host 'DEPLOY OK: nro copied to /switch/eBookReaderSwitch.nro'; break }
    } else {
        Write-Host 'copying...'
    }
}
