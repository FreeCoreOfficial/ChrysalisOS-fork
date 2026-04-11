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
  echo "Set CHRYSALIS_TCC_CC to your ChrysalisOS cross-compiler." >&2
  echo "Example: CHRYSALIS_TCC_CC=i386-chrysalis-gcc" >&2
  exit 1
fi

CHRYSALIS_TCC_AR="${CHRYSALIS_TCC_AR:-i386-chrysalis-ar}"
CHRYSALIS_TCC_RANLIB="${CHRYSALIS_TCC_RANLIB:-i386-chrysalis-ranlib}"

mkdir -p "$OUT" "$INSTALL_ROOT"

pushd "$SRC" >/dev/null
make distclean >/dev/null 2>&1 || true

./configure \
  --cc="$CHRYSALIS_TCC_CC" \
  --ar="$CHRYSALIS_TCC_AR" \
  --ranlib="$CHRYSALIS_TCC_RANLIB" \
  --prefix="$INSTALL_ROOT" \
  --cpu=i386

make -j"$(getconf _NPROCESSORS_ONLN)"
make install
popd >/dev/null

cp "$INSTALL_ROOT/bin/tcc" "$OUT/tcc"
echo "TinyCC built -> $OUT/tcc"
