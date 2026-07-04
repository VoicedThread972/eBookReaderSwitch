$ErrorActionPreference = 'Stop'
$shell = New-Object -ComObject Shell.Application
$switch = $shell.Namespace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch' } | Select-Object -First 1
if (-not $switch) { Write-Host 'Switch not found via MTP'; exit 1 }
$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match 'microSD|SD card' } | Select-Object -First 1
if (-not $sd) { Write-Host 'SD not found'; exit 1 }
$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -eq 'switch' } | Select-Object -First 1
if (-not $sw) { Write-Host '/switch not found'; exit 1 }
Write-Host '== /switch entries matching book/ebook/reader =='
$sw.GetFolder.Items() | Where-Object { $_.Name -match 'book|ebook|reader' } | ForEach-Object { "[$($_.IsFolder)] $($_.Name)" }
Write-Host '== nro files directly under /switch =='
$sw.GetFolder.Items() | Where-Object { $_.Name -match '\.nro$' } | ForEach-Object { $_.Name }
