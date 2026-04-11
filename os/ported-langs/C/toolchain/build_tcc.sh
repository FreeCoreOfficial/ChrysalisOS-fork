#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/tcc-0.9.27"
OUT="$ROOT/bin"
INSTALL_ROOT="$ROOT/_install"

if [[ ! -d "$SRC" ]]; then
  echo "TinyCC sources not found at $SRC" >&2
  exit 1
fi

if [[ -z "${CHRYSALIS_TCC_CC:-}" ]]; then
  if command -v i386-elf-gcc >/dev/null 2>&1; then
    CHRYSALIS_TCC_CC="i386-elf-gcc"
  elif command -v gcc >/dev/null 2>&1; then
    CHRYSALIS_TCC_CC="gcc -m32"
  else
    echo "Set CHRYSALIS_TCC_CC to your ChrysalisOS cross-compiler." >&2
    echo "Example: CHRYSALIS_TCC_CC=i386-chrysalis-gcc" >&2
    exit 1
  fi
fi

# Auto-detect AR and RANLIB tools
if [[ -z "${CHRYSALIS_TCC_AR:-}" ]]; then
  if command -v i386-elf-ar >/dev/null 2>&1; then
    CHRYSALIS_TCC_AR="i386-elf-ar"
  else
    CHRYSALIS_TCC_AR="ar"
  fi
fi

if [[ -z "${CHRYSALIS_TCC_RANLIB:-}" ]]; then
  if command -v i386-elf-ranlib >/dev/null 2>&1; then
    CHRYSALIS_TCC_RANLIB="i386-elf-ranlib"
  else
    CHRYSALIS_TCC_RANLIB="ranlib"
  fi
fi

export CFLAGS="${CFLAGS:-} -static -DCONFIG_TCC_ELFINTERP=\"\""
export LDFLAGS="${LDFLAGS:-} -static"

mkdir -p "$OUT" "$INSTALL_ROOT"

pushd "$SRC" >/dev/null
make distclean >/dev/null 2>&1 || true

./configure \
  --cc="$CHRYSALIS_TCC_CC" \
  --ar="$CHRYSALIS_TCC_AR" \
  --ranlib="$CHRYSALIS_TCC_RANLIB" \
  --prefix="$INSTALL_ROOT" \
  --cpu=i386

# Ensure empty interpreter strings are preserved for tcc builds.
# The configure-generated config.mak may collapse "" into an empty define.
sed -i 's/-DCONFIG_TCC_ELFINTERP=""/-DCONFIG_TCC_ELFINTERP=\\"\\"/g' config.mak

make -j"$(getconf _NPROCESSORS_ONLN)"
make install
popd >/dev/null

cp "$INSTALL_ROOT/bin/tcc" "$OUT/tcc"
echo "TinyCC built -> $OUT/tcc"
