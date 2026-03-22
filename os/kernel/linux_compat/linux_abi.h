#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum linux_abi {
  LINUX_ABI_I386 = 0,
  LINUX_ABI_X86_64 = 1,
};

struct linux_i386_syscall_regs {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esi;
  uint32_t edi;
  uint32_t ebp;
};

struct linux_x86_64_syscall_regs {
  uint64_t rax;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t r10;
  uint64_t r8;
  uint64_t r9;
};

struct linux_utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

struct linux_timespec64 {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct linux_timeval64 {
  int64_t tv_sec;
  int64_t tv_usec;
};

struct linux_sysinfo {
  int64_t uptime;
  uint64_t loads[3];
  uint64_t totalram;
  uint64_t freeram;
  uint64_t sharedram;
  uint64_t bufferram;
  uint64_t totalswap;
  uint64_t freeswap;
  uint16_t procs;
  uint16_t pad;
  uint64_t totalhigh;
  uint64_t freehigh;
  uint32_t mem_unit;
  char _f[8];
};

/* --- evdev Support --- */
struct linux_input_event {
  struct linux_timeval64 time;
  uint16_t type;
  uint16_t code;
  int32_t value;
};

#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_SYN 0x00

#define SYN_REPORT 0

#define REL_X 0x00
#define REL_Y 0x01

/* --- DRM/KMS Support --- */
#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40

#define DRM_IOCTL_VERSION 0xC0406400
#define DRM_IOCTL_GET_RESOURCES 0xC01064A0
#define DRM_IOCTL_GET_CONNECTOR 0xC04064A7
#define DRM_IOCTL_GET_ENCODER 0xC01464A6
#define DRM_IOCTL_GET_CRTC 0xC06864A1
#define DRM_IOCTL_MODE_GETRESOURCES 0xC01064A0
#define DRM_IOCTL_MODE_GETCONNECTOR 0xC05064A7
#define DRM_IOCTL_MODE_CREATE_DUMB 0xC02064B2
#define DRM_IOCTL_MODE_MAP_DUMB 0xC01064B3
#define DRM_IOCTL_MODE_DESTROY_DUMB 0xC00464B4
#define DRM_IOCTL_MODE_ADDFB 0xC01C64AE
#define DRM_IOCTL_MODE_RMFB 0xC00464AF
#define DRM_IOCTL_SET_MASTER 0x0000641E
#define DRM_IOCTL_DROP_MASTER 0x0000641F

struct linux_drm_version {
  int version_major;
  int version_minor;
  int version_patchlevel;
  uint64_t name_len;
  uint64_t name;
  uint64_t date_len;
  uint64_t date;
  uint64_t desc_len;
  uint64_t desc;
};

struct linux_drm_mode_card_res {
  uint64_t fb_id_ptr;
  uint64_t crtc_id_ptr;
  uint64_t connector_id_ptr;
  uint64_t encoder_id_ptr;
  uint32_t count_fbs;
  uint32_t count_crtcs;
  uint32_t count_connectors;
  uint32_t count_encoders;
  uint32_t min_width, max_width;
  uint32_t min_height, max_height;
};

#ifdef __cplusplus
}
#endif
