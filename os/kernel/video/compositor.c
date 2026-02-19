#include "compositor.h"
#include "../cmds/fat.h" /* fat32_get_file_size, fat32_read_file, fat_automount */
#include "../drivers/mouse.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "framebuffer.h" /* fb_putpixel, fb_clear, fb_get_info */
#include "gpu.h"

/* Import serial logging */
extern void serial(const char *fmt, ...);
extern const void *ramfs_read_file(const char *name, size_t *out_size);

#define MAX_RESOLUTION_WIDTH 1280
#define MAX_RESOLUTION_HEIGHT 800
static uint32_t back_buffer[MAX_RESOLUTION_WIDTH * MAX_RESOLUTION_HEIGHT];

/* ---- Wallpaper ----------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
} WP_FILEHEADER;

typedef struct {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} WP_INFOHEADER;
#pragma pack(pop)

static uint32_t *g_wallpaper_pixels = NULL;
static uint32_t g_wallpaper_w = 0;
static uint32_t g_wallpaper_h = 0;
static bool g_wallpaper_tried = false; /* Set after first load attempt */

static void wallpaper_load(void) {
  uint8_t *data = NULL;
  size_t sz = 0;
  bool is_ramfs = false;

  /* 1. Try RAMFS first (for Live CD) */
  const void *rdata = ramfs_read_file("bg.bmp", &sz);
  if (rdata && sz > 54) {
    data = (uint8_t *)rdata;
    is_ramfs = true;
    serial("[COMPOSITOR] bg.bmp found in RAMFS (multiboot module).\n");
  } else {
    /* 2. Try FAT32 (installed system) */
    fat_automount();
    const char *path = "/system/bg.bmp";
    int32_t fsz = fat32_get_file_size(path);
    if (fsz > 54) {
      sz = (size_t)fsz;
      data = (uint8_t *)kmalloc(sz);
      if (data) {
        if (fat32_read_file(path, data, (uint32_t)sz) < (int)sz) {
          kfree(data);
          data = NULL;
        } else {
          serial("[COMPOSITOR] bg.bmp found on FAT partition.\n");
        }
      }
    }
  }

  if (!data) {
    serial("[COMPOSITOR] bg.bmp not found, using solid color.\n");
    return;
  }

  WP_FILEHEADER *fh = (WP_FILEHEADER *)data;
  WP_INFOHEADER *ih = (WP_INFOHEADER *)(data + sizeof(WP_FILEHEADER));

  if (fh->bfType != 0x4D42) {
    serial("[COMPOSITOR] bg.bmp: bad magic.\n");
    if (!is_ramfs)
      kfree(data);
    return;
  }

  int32_t w = ih->biWidth;
  int32_t h = ih->biHeight;
  uint16_t bpp = ih->biBitCount;

  if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32)) {
    serial("[COMPOSITOR] bg.bmp: unsupported format (%dx%d, %dbpp).\n", w, h,
           bpp);
    if (!is_ramfs)
      kfree(data);
    return;
  }

  int absH = (h > 0) ? h : -h;
  int rowSize = ((w * bpp + 31) / 32) * 4;

  size_t pixCount = (size_t)w * (size_t)absH;
  uint32_t *pixels = (uint32_t *)kmalloc(pixCount * 4);
  if (!pixels) {
    serial("[COMPOSITOR] OOM for wallpaper pixel buffer.\n");
    if (!is_ramfs)
      kfree(data);
    return;
  }

  const uint8_t *base = data + fh->bfOffBits;
  for (int i = 0; i < absH; i++) {
    const uint8_t *row = base + (size_t)i * rowSize;
    int screenY = (h > 0) ? (absH - 1 - i) : i;
    for (int x = 0; x < w; x++) {
      const uint8_t *px = row + x * (bpp / 8);
      uint8_t b = px[0], g = px[1], r = px[2];
      pixels[(size_t)screenY * (size_t)w + (size_t)x] =
          0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
  }

  if (!is_ramfs) {
    kfree(data);
  }

  g_wallpaper_pixels = pixels;
  g_wallpaper_w = (uint32_t)w;
  g_wallpaper_h = (uint32_t)absH;
  serial("[COMPOSITOR] Wallpaper loaded: %dx%d.\n", w, absH);
}
/* -------------------------------------------------------------------------- */

void compositor_init(void) { serial("[COMPOSITOR] Initialized.\n"); }

void compositor_render_surfaces(surface_t **surfaces, int count) {
  uint32_t fb_w = 0, fb_h = 0, fb_pitch = 0;
  fb_get_info(&fb_w, &fb_h, &fb_pitch, 0, 0);

  if (fb_w == 0 || fb_h == 0) {
    return;
  }

  if (fb_w > MAX_RESOLUTION_WIDTH)
    fb_w = MAX_RESOLUTION_WIDTH;
  if (fb_h > MAX_RESOLUTION_HEIGHT)
    fb_h = MAX_RESOLUTION_HEIGHT;

  /* Load wallpaper on first call */
  if (!g_wallpaper_tried) {
    g_wallpaper_tried = true;
    wallpaper_load();
  }

  gpu_device_t *gpu = gpu_get_primary();
  uint8_t *fb_base = (gpu && gpu->virt_addr) ? (uint8_t *)gpu->virt_addr : 0;

  /* 1. Fill back buffer: wallpaper or solid fallback */
  if (g_wallpaper_pixels && g_wallpaper_w > 0 && g_wallpaper_h > 0) {
    /* Blit wallpaper, nearest-neighbour scaled to screen size */
    for (uint32_t y = 0; y < fb_h; y++) {
      uint32_t src_y = y * g_wallpaper_h / fb_h;
      if (src_y >= g_wallpaper_h)
        src_y = g_wallpaper_h - 1;
      for (uint32_t x = 0; x < fb_w; x++) {
        uint32_t src_x = x * g_wallpaper_w / fb_w;
        if (src_x >= g_wallpaper_w)
          src_x = g_wallpaper_w - 1;
        back_buffer[y * fb_w + x] =
            g_wallpaper_pixels[src_y * g_wallpaper_w + src_x];
      }
    }
  } else {
    /* Fallback: Chrysalis Slate */
    uint32_t bg_color = 0xFF12171D;
    for (uint32_t i = 0; i < fb_w * fb_h; i++) {
      back_buffer[i] = bg_color;
    }
  }

  /* 2. Render all surfaces to back buffer */
  for (int surf_idx = 0; surf_idx < count; surf_idx++) {
    surface_t *s = surfaces[surf_idx];
    if (!s || !s->visible)
      continue;

    int s_x = s->x;
    int s_y = s->y;
    int s_w = s->width;
    int s_h = s->height;

    for (int y = 0; y < s_h; y++) {
      int screen_y = s_y + y;
      if (screen_y < 0 || screen_y >= (int)fb_h)
        continue;

      for (int x = 0; x < s_w; x++) {
        int screen_x = s_x + x;
        if (screen_x < 0 || screen_x >= (int)fb_w)
          continue;

        uint32_t pixel = s->pixels[y * s_w + x];
        uint8_t a = (pixel >> 24) & 0xFF;

        if (a == 255) {
          /* Fully opaque: direct write */
          back_buffer[screen_y * fb_w + screen_x] = pixel;
        } else if (a > 0) {
          /* Semi-transparent: alpha blending */
          uint32_t bg = back_buffer[screen_y * fb_w + screen_x];

          uint8_t r = (pixel >> 16) & 0xFF;
          uint8_t g = (pixel >> 8) & 0xFF;
          uint8_t b = pixel & 0xFF;

          uint8_t bgr = (bg >> 16) & 0xFF;
          uint8_t bgg = (bg >> 8) & 0xFF;
          uint8_t bgb = bg & 0xFF;

          /* result = (source * alpha + dest * (255 - alpha)) / 255 */
          uint8_t res_r = (uint8_t)((r * a + bgr * (255 - a)) / 255);
          uint8_t res_g = (uint8_t)((g * a + bgg * (255 - a)) / 255);
          uint8_t res_b = (uint8_t)((b * a + bgb * (255 - a)) / 255);

          back_buffer[screen_y * fb_w + screen_x] =
              0xFF000000 | ((uint32_t)res_r << 16) | ((uint32_t)res_g << 8) |
              res_b;
        }
        /* a == 0: Skip (fully transparent) */
      }
    }
  }

  /* 3. Render mouse cursor on top of back buffer */
  int mx, my;
  mouse_get_coords(&mx, &my);
  const uint8_t *cursor = mouse_get_cursor_bitmap();

  for (int cy = 0; cy < 16; cy++) {
    int screen_y = my + cy;
    if (screen_y < 0 || screen_y >= (int)fb_h)
      continue;

    for (int cx = 0; cx < 16; cx++) {
      int screen_x = mx + cx;
      if (screen_x < 0 || screen_x >= (int)fb_w)
        continue;

      uint8_t type = cursor[cy * 16 + cx];
      if (type == 1) {
        back_buffer[screen_y * fb_w + screen_x] = 0xFF000000; /* Black border */
      } else if (type == 2) {
        back_buffer[screen_y * fb_w + screen_x] = 0xFFFFFFFF; /* White fill */
      }
    }
  }

  /* 4. Copy entire back buffer to framebuffer in one operation */
  if (fb_base) {
    /* Fast path: copy entire frame at once */
    for (uint32_t y = 0; y < fb_h; y++) {
      memcpy(fb_base + (y * gpu->pitch), &back_buffer[y * fb_w], fb_w * 4);
    }
  } else {
    /* Slow path: use putpixel */
    for (uint32_t y = 0; y < fb_h; y++) {
      for (uint32_t x = 0; x < fb_w; x++) {
        fb_putpixel(x, y, back_buffer[y * fb_w + x]);
      }
    }
  }
}
