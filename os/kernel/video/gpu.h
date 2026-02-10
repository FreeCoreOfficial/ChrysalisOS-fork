#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPU Types */
typedef enum {
  GPU_TYPE_UNKNOWN = 0,
  GPU_TYPE_VESA = 1,
  GPU_TYPE_BOCHS = 2,
  GPU_TYPE_QEMU = 3,
  GPU_TYPE_VMWARE = 4
} gpu_type_t;

/* Forward declaration */
struct gpu_device;

/* GPU Operations Table (VTable) */
typedef struct {
  /* Validate and set mode (for VESA this mostly validates) */
  int (*set_mode)(struct gpu_device *dev, uint32_t width, uint32_t height,
                  uint32_t bpp);

  /* Drawing primitives */
  void (*putpixel)(struct gpu_device *dev, uint32_t x, uint32_t y,
                   uint32_t color);
  void (*fillrect)(struct gpu_device *dev, uint32_t x, uint32_t y, uint32_t w,
                   uint32_t h, uint32_t color);
  void (*clear)(struct gpu_device *dev, uint32_t color);

  /* Memory barrier / flush for buffered GPUs */
  void (*flush)(struct gpu_device *dev);
} gpu_ops_t;

/* GPU Device Structure */
typedef struct gpu_device {
  uint32_t device_id;
  gpu_type_t type;

  /* Mode Info */
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;

  /* Memory Info */
  uintptr_t phys_addr;
  void *virt_addr;

  /* Driver Ops */
  gpu_ops_t *ops;

  /* Linked List */
  struct gpu_device *next;
} gpu_device_t;

/* Core Subsystem API */
void gpu_init(void);
void gpu_register_device(gpu_device_t *dev);
gpu_device_t *gpu_get_primary(void);

#ifdef __cplusplus
}
#endif