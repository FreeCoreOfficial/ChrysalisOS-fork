# TinyCC Toolchain (On-Device C)

This directory vendors TinyCC sources for the on-device C compiler.

- Sources: `tcc-0.9.27/`
- Build script: `build_tcc.sh`
- Output binary: `bin/tcc` (copied into `/system/bin` during ISO build)

## Notes

- The build assumes you have a ChrysalisOS-compatible cross toolchain.
- If TinyCC fails to build, the most likely cause is missing libc/posix stubs.
  Extend `os/user/libc` as needed, rebuild `libc.so`, then retry.
