# chrys-pkg

Minimal package catalog for ChrysalisOS.

## Features
- Parses local `catalog.json`
- Lists available native catalog entries
- Does not install external app bundles

## Structure
- `catalog.json`: Repository metadata and app list.
- `installed/`: Directory reserved for future native package state.

## Usage
```bash
pkg init
pkg list
```
