#!/usr/bin/env bash
set -e
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
cd /c/Users/berta/Desktop/eBookReaderSwitch

echo "=== make clean ==="
make clean >/dev/null
echo "=== make NODEBUG=1 ==="
make NODEBUG=1 2>&1 | tail -8

PREFIX=$DEVKITPRO/devkitA64/bin/aarch64-none-elf
ELF=eBookReaderSwitch.elf

echo ""
echo "=== newlib _open_r: where it STORES r->deviceData ==="
a=$($PREFIX-nm "$ELF" | grep -E ' _open_r$' | awk '{print "0x"$1}')
s=$((a))
$PREFIX-objdump -d "$ELF" --start-address=$a --stop-address=$((s+0xa0)) | grep -E 'ldr .*#176|str .*x21' | head

echo ""
echo "=== libnx fsdev_chdir: where it LOADS r->deviceData ==="
b=$($PREFIX-nm "$ELF" | grep -E ' fsdev_chdir$' | awk '{print "0x"$1}')
t=$((b))
$PREFIX-objdump -d "$ELF" --start-address=$b --stop-address=$((t+0x30)) | grep -E 'ldr' | head -3

echo ""
echo "=== build id ==="
$PREFIX-readelf -n "$ELF" | grep -i 'build id'
ls -l eBookReaderSwitch.nro
