#include "kms.h"

#ifndef __x86_64__
#include "gpu.h"
#include "../mem/kmalloc.h"
#include "../string.h"

extern int copy_from_user(void *dst, const void *src, uint32_t size);
extern int copy_to_user(void *dst, const void *src, uint32_t size);

typedef struct kms_buf {
  uint32_t handle;
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t pitch;
  uint32_t size;
  uint8_t *data;
} kms_buf_t;

#define KMS_MAX_BUFS 16

static gpu_device_t *g_kms_gpu = NULL;
static kms_buf_t g_kms_bufs[KMS_MAX_BUFS];
static uint32_t g_kms_next_handle = 1;

static kms_buf_t *kms_find_buf(uint32_t handle) {
  for (int i = 0; i < KMS_MAX_BUFS; ++i) {
    if (g_kms_bufs[i].handle == handle)
      return &g_kms_bufs[i];
  }
  return NULL;
}

static kms_buf_t *kms_alloc_buf(void) {
  for (int i = 0; i < KMS_MAX_BUFS; ++i) {
    if (g_kms_bufs[i].handle == 0)
      return &g_kms_bufs[i];
  }
  return NULL;
}

static void kms_free_buf(kms_buf_t *b) {
  if (!b)
    return;
  if (b->data)
    kfree(b->data);
  memset(b, 0, sizeof(*b));
}

static int kms_copy_to_fb(const uint8_t *src, uint32_t src_pitch, uint32_t w,
                          uint32_t h) {
  if (!g_kms_gpu || !g_kms_gpu->virt_addr || !src)
    return -1;

  uint32_t dst_w = g_kms_gpu->width;
  uint32_t dst_h = g_kms_gpu->height;
  uint32_t dst_pitch = g_kms_gpu->pitch;
  uint32_t bpp = g_kms_gpu->bpp;
  uint32_t bytespp = bpp / 8;
  if (bytespp == 0)
    return -1;

  uint32_t rows = (h < dst_h) ? h : dst_h;
  uint32_t cols = (w < dst_w) ? w : dst_w;
  uint32_t row_bytes = cols * bytespp;
  uint8_t *dst = (uint8_t *)g_kms_gpu->virt_addr;

  for (uint32_t y = 0; y < rows; ++y) {
    memcpy(dst + y * dst_pitch, src + y * src_pitch, row_bytes);
  }

  if (g_kms_gpu->ops && g_kms_gpu->ops->flush)
    g_kms_gpu->ops->flush(g_kms_gpu);

  return 0;
}

int kms_init(void) {
  g_kms_gpu = gpu_get_primary();
  return g_kms_gpu ? 0 : -1;
}

int kms_ioctl(uint32_t cmd, void *arg) {
  if (!g_kms_gpu)
    return -1;

  switch (cmd) {
  case KMS_IOCTL_GET_INFO: {
    if (!arg)
      return -1;
    kms_info_t info;
    info.width = g_kms_gpu->width;
    info.height = g_kms_gpu->height;
    info.bpp = g_kms_gpu->bpp;
    info.pitch = g_kms_gpu->pitch;
    info.fb_size = g_kms_gpu->pitch * g_kms_gpu->height;
    info.flags = 0;
    return copy_to_user(arg, &info, sizeof(info));
  }
  case KMS_IOCTL_SET_MODE: {
    if (!arg || !g_kms_gpu->ops || !g_kms_gpu->ops->set_mode)
      return -1;
    kms_set_mode_t req;
    if (copy_from_user(&req, arg, sizeof(req)) < 0)
      return -1;
    return g_kms_gpu->ops->set_mode(g_kms_gpu, req.width, req.height, req.bpp);
  }
  case KMS_IOCTL_CREATE_DUMB: {
    if (!arg)
      return -1;
    kms_dumb_create_t req;
    if (copy_from_user(&req, arg, sizeof(req)) < 0)
      return -1;
    if (req.width == 0 || req.height == 0 || req.bpp == 0)
      return -1;
    uint32_t bytespp = req.bpp / 8;
    if (bytespp == 0)
      return -1;
    kms_buf_t *b = kms_alloc_buf();
    if (!b)
      return -1;
    uint32_t pitch = req.width * bytespp;
    uint32_t size = pitch * req.height;
    uint8_t *data = (uint8_t *)kmalloc(size);
    if (!data)
      return -1;
    memset(data, 0, size);
    b->handle = g_kms_next_handle++;
    b->width = req.width;
    b->height = req.height;
    b->bpp = req.bpp;
    b->pitch = pitch;
    b->size = size;
    b->data = data;
    req.handle = b->handle;
    req.pitch = pitch;
    req.size = size;
    return copy_to_user(arg, &req, sizeof(req));
  }
  case KMS_IOCTL_DESTROY_DUMB: {
    if (!arg)
      return -1;
    kms_dumb_destroy_t req;
    if (copy_from_user(&req, arg, sizeof(req)) < 0)
      return -1;
    kms_buf_t *b = kms_find_buf(req.handle);
    if (!b)
      return -1;
    kms_free_buf(b);
    return 0;
  }
  case KMS_IOCTL_PAGE_FLIP: {
    if (!arg)
      return -1;
    kms_page_flip_t req;
    if (copy_from_user(&req, arg, sizeof(req)) < 0)
      return -1;
    kms_buf_t *b = kms_find_buf(req.handle);
    if (!b || !b->data)
      return -1;
    return kms_copy_to_fb(b->data, b->pitch, b->width, b->height);
  }
  case KMS_IOCTL_BLIT: {
    if (!arg)
      return -1;
    kms_blit_t req;
    if (copy_from_user(&req, arg, sizeof(req)) < 0)
      return -1;
    if (!req.user_ptr || req.pitch == 0 || req.height == 0)
      return -1;
    uint32_t max_bytes = g_kms_gpu->pitch * g_kms_gpu->height;
    uint32_t rows = req.height;
    uint32_t row_bytes = req.pitch;
    if (rows == 0 || row_bytes == 0)
      return -1;
    if (row_bytes * rows > max_bytes)
      rows = max_bytes / row_bytes;
    uint8_t *dst = (uint8_t *)g_kms_gpu->virt_addr;
    for (uint32_t y = 0; y < rows; ++y) {
      const uint8_t *src_row =
          (const uint8_t *)req.user_ptr + (uint32_t)(y * row_bytes);
      if (copy_from_user(dst + y * g_kms_gpu->pitch, src_row, row_bytes) < 0)
        return -1;
    }
    if (g_kms_gpu->ops && g_kms_gpu->ops->flush)
      g_kms_gpu->ops->flush(g_kms_gpu);
    return 0;
  }
  default:
    return -1;
  }
}
#else
int kms_init(void) { return -1; }
int kms_ioctl(uint32_t cmd, void *arg) {
  (void)cmd;
  (void)arg;
  return -1;
}
#endif
