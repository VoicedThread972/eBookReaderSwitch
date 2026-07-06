#!/usr/bin/env python3
"""Cross-platform build helper for eBookReaderSwitch.

Usage: python build_all.py [--clean] [--no-pacman-update]

This script detects Windows (MSYS2), Linux, or macOS, ensures basic env vars
are present (prints guidance if not), and runs the standard make sequence.
"""
from __future__ import annotations
import os
import sys
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def run(cmd, **kwargs):
    print("$", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(ROOT), **kwargs)

def is_msys():
    return sys.platform.startswith("win") and "MSYS" in os.environ.get("MSYSTEM", "")

def check_env():
    devkit = os.environ.get("DEVKITPRO")
    devkita64 = os.environ.get("DEVKITA64") or os.environ.get("DEVKITA64")
    missing = []
    if not devkit:
        missing.append("DEVKITPRO")
    if not devkita64:
        missing.append("DEVKITA64 (or DEVKITA64)")
    if missing:
        print("Warning: recommended env vars missing:", ", ".join(missing))
        print("If you use devkitPro/MSYS2, open MSYS2 shell and run: pacman -Syu devkitA64 devkitPro-pacman")
    return True

def check_tools():
    required = ["make", "aarch64-none-elf-gcc"]
    missing = []
    for t in required:
        if shutil.which(t) is None:
            missing.append(t)
    if missing:
        print("Missing required tools:", ", ".join(missing))
        if is_msys():
            print("In MSYS2 you can install toolchain with: pacman -Syu devkitA64 base-devel")
        else:
            print("Please install the cross-toolchain and build tools for your platform.")
        return False
    return True

def pacman_update():
    if is_msys():
        print("Detected MSYS2 environment. You may run optional pacman updates.")
        print("Run in MSYS2: pacman -Syu devkitA64 base-devel mingw-w64-x86_64-toolchain")

def build(clean=False):
    check_env()
    if not check_tools():
        raise RuntimeError("Required build tools missing")
    if clean:
        if (ROOT / "Makefile").exists():
            run(["make", "clean"])
        else:
            print("Makefile not found. Aborting clean.")
    # Default make invocation - preserves existing Makefile behavior
    make_cmd = ["make"]
    try:
        run(make_cmd)
    except subprocess.CalledProcessError as e:
        # Try to capture linker missing -l errors by running verbose make
        print("Build failed. Analyzing error output with verbose build...")
        try:
            out = subprocess.check_output(["make", "V=1"], cwd=str(ROOT), stderr=subprocess.STDOUT, text=True)
        except subprocess.CalledProcessError as e2:
            out = e2.output
        # Look for 'cannot find -lNAME'
        import re
        missing_libs = re.findall(r"cannot find -l(\w+)", out)
        retried_without_twili = False
        if missing_libs:
            for lib in set(missing_libs):
                print(f"Linker cannot find library '-l{lib}'.")
                if lib.lower() == 'twili':
                    print("The project optionally links to the Twili debugger.")
                    print("Attempting automatic fallback: rebuilding with NODEBUG=1 (disables twili)...")
                    try:
                        run(["make", "clean"]) if (ROOT / "Makefile").exists() else None
                    except Exception:
                        pass
                    try:
                        run(["make", "NODEBUG=1"])
                        retried_without_twili = True
                        print("Rebuild with NODEBUG=1 succeeded.")
                    except subprocess.CalledProcessError:
                        print("Rebuild with NODEBUG=1 also failed; please install Twili or remove -ltwili from Makefile.")
                    if is_msys():
                        print("If you want Twili on MSYS2, install or build Twili yourself; it's not always in pacman repos.")
                else:
                    print(f"Search for lib{lib} in DEVKITPRO paths...")
                    candidates = list(ROOT.glob("**/lib" + lib + "*"))
                    if candidates:
                        for c in candidates:
                            print("Found candidate:", c)
                    else:
                        print("No candidate library found in workspace.")
                        if is_msys():
                            print(f"On MSYS2 devkitPro you may need to install the package that provides {lib}.")
                            print("Suggested pacman command (run in MSYS2 shell):")
                            print(f"  pacman -S <package-that-provides-{lib}>")
            print("Full verbose output saved below for inspection:")
            print(out)
        if retried_without_twili:
            return
        else:
            print("No missing -lXXX pattern found; see verbose output below:")
            print(out)
        raise

def main(argv):
    import argparse
    p = argparse.ArgumentParser(description="Cross-platform build helper for eBookReaderSwitch")
    p.add_argument("--clean", action="store_true")
    p.add_argument("--no-pacman-update", action="store_true")
    args = p.parse_args(argv)

    if not args.no_pacman_update:
        pacman_update()

    try:
        build(clean=args.clean)
    except Exception as e:
        print("Error:", e)
        sys.exit(1)

if __name__ == "__main__":
    main(sys.argv[1:])
