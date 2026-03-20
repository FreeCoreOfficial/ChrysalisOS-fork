#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Simple Chrysalis KMS ioctls */
#define KMS_IOCTL_GET_INFO     0x4B01
#define KMS_IOCTL_SET_MODE     0x4B02
#define KMS_IOCTL_CREATE_DUMB  0x4B03
#define KMS_IOCTL_DESTROY_DUMB 0x4B04
#define KMS_IOCTL_PAGE_FLIP    0x4B05
#define KMS_IOCTL_BLIT         0x4B06

typedef struct kms_info {
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t pitch;
  uint32_t fb_size;
  uint32_t flags;
} kms_info_t;

typedef struct kms_set_mode {
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
} kms_set_mode_t;

typedef struct kms_dumb_create {
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t handle;
  uint32_t pitch;
  uint32_t size;
} kms_dumb_create_t;

typedef struct kms_dumb_destroy {
  uint32_t handle;
} kms_dumb_destroy_t;

typedef struct kms_page_flip {
  uint32_t handle;
} kms_page_flip_t;

typedef struct kms_blit {
  const void *user_ptr;
  uint32_t size;
  uint32_t pitch;
  uint32_t height;
} kms_blit_t;

int kms_init(void);
int kms_ioctl(uint32_t cmd, void *arg);

#ifdef __cplusplus
}
#endif
