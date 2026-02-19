#!/usr/bin/env python3
from PIL import Image
import os

icons = [
    (0,  "start.png"),
    (1,  "term.png"),
    (2,  "files.png"),
    (3,  "img.png"),
    (4,  "note.png"),
    (5,  "paint.png"),
    (6,  "calc.png"),
    (7,  "clock.png"),  
    (8,  "calc.png"),     
    (9,  "task.png"),
    (10, "info.png"),
    (11, "3D.png"),
    (12, "mine.png"),
    (13, "net.png"),
    (14, "x0.png"),
    (15, "run.png"),
]

OUT_DIR = "out"
TARGET_SIZE = 128  # 128x128 to keep assets small and installable

os.makedirs(OUT_DIR, exist_ok=True)

for _, path in icons:
    img = Image.open(path).convert("RGBA")
    img = img.resize((TARGET_SIZE, TARGET_SIZE), Image.LANCZOS)
    base = os.path.splitext(os.path.basename(path))[0]
    out_path = os.path.join(OUT_DIR, base + ".bmp")
    img.save(out_path, format="BMP")
    print("wrote", out_path)

print("icons bmp generated")

# --- Wallpaper ---
WALLPAPER_SRC = "../background/wallpaper.png"
WALLPAPER_OUT = os.path.join(OUT_DIR, "bg.bmp")
WALLPAPER_W = 1024
WALLPAPER_H = 768

try:
    wp = Image.open(WALLPAPER_SRC).convert("RGB")  # RGB, fara alpha (BMP 24-bit)
    wp = wp.resize((WALLPAPER_W, WALLPAPER_H), Image.LANCZOS)
    wp.save(WALLPAPER_OUT, format="BMP")
    print("wrote", WALLPAPER_OUT)
    print("wallpaper bmp generated")
except FileNotFoundError:
    print(f"WARN: wallpaper not found at {WALLPAPER_SRC}, skipping.")
