$ErrorActionPreference = 'Stop'
$srcName = 'eBookReaderSwitch.nro'
$shell = New-Object -ComObject Shell.Application
$switch = $shell.Namespace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' } | Select-Object -First 1
$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match 'microSD|SD card' } | Select-Object -First 1
$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -eq 'switch' } | Select-Object -First 1
$old = $sw.GetFolder.Items() | Where-Object { $_.Name -eq $srcName } | Select-Object -First 1
Write-Host '== verbs on the on-SD nro =='
$i = 0
foreach ($v in $old.Verbs()) { Write-Host ("[{0}] '{1}'" -f $i, $v.Name); $i++ }
