#!/usr/bin/env python3
import math
import struct
import sys
from pathlib import Path

SECTOR_SIZE = 512
TOTAL_SECTORS = 2880
ROOT_ENTRIES = 224
SECTORS_PER_FAT = 9
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
SECTORS_PER_TRACK = 18
HEADS = 2
MEDIA = 0xF0
IMAGE_SIZE = TOTAL_SECTORS * SECTOR_SIZE

ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE

FILES = [
    ("KERNEL.BIN", "kernel.bin"),
    ("KERNEL64.BIN", "kernel64.bin"),
    ("HELLO64.ELF", "hello64.elf"),
]


def short_name(name: str) -> bytes:
    parts = name.upper().split(".", 1)
    stem = parts[0][:8].ljust(8)
    ext = (parts[1][:3] if len(parts) > 1 else "").ljust(3)
    return (stem + ext).encode("ascii")


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + (cluster // 2)
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def write_le16(buf: bytearray, off: int, value: int) -> None:
    buf[off:off + 2] = struct.pack("<H", value)


def build_image(stage1: Path, stage2: Path, boot_dir: Path, out_img: Path) -> None:
    stage1_data = bytearray(stage1.read_bytes())
    stage2_data = stage2.read_bytes()

    if len(stage1_data) != 512:
        raise SystemExit(f"stage1 must be exactly 512 bytes, got {len(stage1_data)}")

    stage2_sectors = math.ceil(len(stage2_data) / SECTOR_SIZE)
    reserved_sectors = 1 + stage2_sectors
    first_data_sector = reserved_sectors + FAT_COUNT * SECTORS_PER_FAT + ROOT_DIR_SECTORS
    data_sectors = TOTAL_SECTORS - first_data_sector
    max_clusters = data_sectors // SECTORS_PER_CLUSTER

    write_le16(stage1_data, 14, reserved_sectors)
    write_le16(stage1_data, 17, ROOT_ENTRIES)
    write_le16(stage1_data, 19, TOTAL_SECTORS)
    stage1_data[21] = MEDIA
    write_le16(stage1_data, 22, SECTORS_PER_FAT)
    write_le16(stage1_data, 24, SECTORS_PER_TRACK)
    write_le16(stage1_data, 26, HEADS)

    image = bytearray(IMAGE_SIZE)
    image[:SECTOR_SIZE] = stage1_data
    image[SECTOR_SIZE:SECTOR_SIZE + len(stage2_data)] = stage2_data

    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat[0] = MEDIA
    fat[1] = 0xFF
    fat[2] = 0xFF

    root = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)

    cluster = 2
    root_index = 0
    for disk_name, source_name in FILES:
        source_path = boot_dir / source_name
        if not source_path.exists():
            continue
        data = source_path.read_bytes()
        needed_clusters = max(1, math.ceil(len(data) / SECTOR_SIZE))
        if cluster - 2 + needed_clusters > max_clusters:
            raise SystemExit("boot image is too small for selected files")

        first_cluster = cluster
        for i in range(needed_clusters):
            current = cluster + i
            nxt = 0xFFF if i == needed_clusters - 1 else current + 1
            set_fat12_entry(fat, current, nxt)

        data_offset_sector = first_data_sector + (first_cluster - 2)
        data_offset = data_offset_sector * SECTOR_SIZE
        image[data_offset:data_offset + len(data)] = data

        entry_off = root_index * 32
        root[entry_off:entry_off + 11] = short_name(disk_name)
        root[entry_off + 11] = 0x20
        root[entry_off + 26:entry_off + 28] = struct.pack("<H", first_cluster)
        root[entry_off + 28:entry_off + 32] = struct.pack("<I", len(data))
        root_index += 1
        cluster += needed_clusters

    fat1_off = reserved_sectors * SECTOR_SIZE
    fat2_off = fat1_off + len(fat)
    root_off = (reserved_sectors + FAT_COUNT * SECTORS_PER_FAT) * SECTOR_SIZE
    image[fat1_off:fat1_off + len(fat)] = fat
    image[fat2_off:fat2_off + len(fat)] = fat
    image[root_off:root_off + len(root)] = root

    out_img.write_bytes(image)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("usage: build_boot_image.py <stage1.bin> <stage2.bin> <boot_dir> <out_img>")
    build_image(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]))
