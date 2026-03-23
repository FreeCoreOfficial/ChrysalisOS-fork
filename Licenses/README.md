# Licenses

This folder collects license texts for third‑party userland/toolchain components
(e.g., `ld-linux`, libc, binutils, GCC, etc.). ChrysalisOS itself remains MIT,
but external components may have different licenses.

When you fork it add the full upstream license texts here when you import external binaries or
sources into `toolchain/linux-sysroot` or `toolchain/gcc` so you can avoid legal troubles.

## Files

| File | Components |
|------|-----------|
| `BINUTILS.txt` | GNU Binutils |
| `GCC.txt` | GNU Compiler Collection |
| `GLIBC.txt` | GNU C Library |
| `dwm.txt` | dwm
| `LD_LINUX.txt` | Dynamic linker (ld-linux) |
| `XORG_X11.txt` | Xorg Server, libX11, libxcb, libXext, libXrender, libXrandr, pixman, libdrm, Mesa |
