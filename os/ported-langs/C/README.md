# ChrysalisOS C SDK

This folder provides a minimal, working C runtime SDK for ChrysalisOS.
It ships libc headers and `libc.so` so you can build C applications on a
host system and run them on ChrysalisOS, and it also includes the on-device
compiler integration when TinyCC is present.

## Layout

- `include/` – libc headers
- `lib/` – runtime libraries (currently `libc.so`)
- `examples/` – sample C programs

## Usage (cross-compile on host)

Build your C program on the host with a cross toolchain and point it at the
SDK headers and libs:

```sh
${CC:-i386-elf-gcc} -I/path/to/ported-langs/C/include \
  -L/path/to/ported-langs/C/lib \
  -Wl,-rpath,/usr/lib \
  -o hello hello.c -lc
```

Copy the resulting binary to ChrysalisOS (for example into `/system/apps` or
`/system/user_apps`) and run it from the shell.

## Usage (on-device compile)

If TinyCC is installed in `/system/bin/tcc`, you can compile inside ChrysalisOS:

```sh
cc hi.c -o hi
./hi
```

The `cc` command is a built-in wrapper that forwards to `/system/bin/tcc`.

## Runtime Notes

- The loader expects `libc.so` in `/usr/lib` (packaged into the ISO build).
- Headers are packaged into `/usr/include`.
- The TinyCC sources live in `ported-langs/C/toolchain/tcc-0.9.27`.
- Build `tcc` via `ported-langs/C/toolchain/build_tcc.sh` before creating the ISO.
