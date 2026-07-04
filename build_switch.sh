#!/bin/bash
set -e
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"
cd /c/Users/berta/Desktop/eBookReaderSwitch
echo "=== toolchain ==="
which aarch64-none-elf-gcc
which sdl2-config
echo "=== build ==="
make "$@"
