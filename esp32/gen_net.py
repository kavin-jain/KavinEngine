# PlatformIO pre-build step: embed the device net with the same tool the PC Makefile uses.
import os
import subprocess
import sys

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)
root = os.path.abspath(os.path.join(env["PROJECT_DIR"], ".."))  # noqa: F821
src = os.path.join(root, "nets", "m2-128.bin")
dst = os.path.join(root, "build", "net_esp32.cpp")
os.makedirs(os.path.dirname(dst), exist_ok=True)
if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
    subprocess.check_call([sys.executable, os.path.join(root, "tools", "bin2cpp.py"), src, dst])
