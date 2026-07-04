#!/bin/bash
export PATH="/opt/devkitpro/devkitA64/bin:$PATH"
cd /c/Users/berta/Desktop/eBookReaderSwitch
echo "== does binary contain __libnx_init_cwd? =="
aarch64-none-elf-nm eBookReaderSwitch.elf | grep -i init_cwd
echo "== who branches to __wrap_chdir? =="
aarch64-none-elf-objdump -d eBookReaderSwitch.elf | grep -nE '<__wrap_chdir>' | head
echo "== who branches (bl) to fsdev_chdir? =="
aarch64-none-elf-objdump -d eBookReaderSwitch.elf | grep -nE 'bl.*<fsdev_chdir>' | head
echo "== full __libnx_init_cwd disasm =="
aarch64-none-elf-objdump -d eBookReaderSwitch.elf | sed -n '/<__libnx_init_cwd>:/,/^$/p'
