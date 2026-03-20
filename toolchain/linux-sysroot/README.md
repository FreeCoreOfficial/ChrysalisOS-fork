# Linux Sysroot (Userland)

Drop a minimal Linux userland sysroot here so the ISO can bundle
`ld-linux` and libc for Linux ABI compatibility.

Expected layout:
- `lib/ld-linux.so.2`
- `lib/libc.so.6`
- `lib/libpthread.so.0` (optional)
- `lib64/ld-linux-x86-64.so.2`
- `lib64/libc.so.6`
- other dependencies as needed by test binaries

When present, `make -C os iso` will copy `lib/` and `lib64/` into the ISO.
hope it works.

And also all rights of the toolchain and other programs that make Linux compatibility in ChrysalisOS possible belong to their respective owners. Check their license files in the Licenses folder.