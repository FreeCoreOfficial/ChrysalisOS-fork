#include <stdint.h>

#define MULTIBOOT2_MAGIC 0xE85250D6
#define MULTIBOOT2_ARCH 0

#define MULTIBOOT2_TAG_END 0
#define MULTIBOOT2_TAG_INFORMATION_REQUEST 1
#define MULTIBOOT2_TAG_FRAMEBUFFER 5

struct multiboot2_header {
  uint32_t magic;
  uint32_t architecture;
  uint32_t header_length;
  uint32_t checksum;
} __attribute__((packed));

struct multiboot2_tag {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
} __attribute__((packed));

struct multiboot2_tag_information_request {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
  uint32_t requests[8];
} __attribute__((packed));

struct multiboot2_tag_framebuffer {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} __attribute__((packed));

/*
   Structură completă cu padding explicit pentru aliniere la 8 bytes.
   Header: 16 bytes
   FB Tag: 20 bytes
   Padding: 4 bytes (20 + 4 = 24, divizibil cu 8)
   End Tag: 8 bytes
   Total: 48 bytes
*/
#ifndef NO_FRAMEBUFFER
struct multiboot2_header_complete {
  struct multiboot2_header header;
  struct multiboot2_tag_information_request info_req;
  struct multiboot2_tag_framebuffer fb;
  uint8_t padding[4];
  struct multiboot2_tag end;
} __attribute__((packed));

__attribute__((section(".multiboot"), aligned(8), used))
const struct multiboot2_header_complete multiboot2_header = {
    .header = {.magic = MULTIBOOT2_MAGIC,
               .architecture = MULTIBOOT2_ARCH,
               .header_length = sizeof(struct multiboot2_header_complete),
               .checksum =
                   (uint32_t)-(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH +
                               sizeof(struct multiboot2_header_complete))},

    .info_req = {.type = MULTIBOOT2_TAG_INFORMATION_REQUEST,
                 .flags = 0,
                 .size = sizeof(struct multiboot2_tag_information_request),
                 .requests = {1, 2, 3, 4, 5, 6, 8, 9}},

    .fb = {.type = MULTIBOOT2_TAG_FRAMEBUFFER,
           .flags = 0,
           .size = sizeof(struct multiboot2_tag_framebuffer), // 20 bytes
           .width = 1024,
           .height = 768,
           .depth = 32},

    .padding = {0, 0, 0, 0},

    .end = {.type = MULTIBOOT2_TAG_END, .flags = 0, .size = 8}};
#else
/* Minimal header for text-mode only (Installer etc) */
struct multiboot2_header_textonly {
  struct multiboot2_header header;
  struct multiboot2_tag_information_request info_req;
  struct multiboot2_tag end;
} __attribute__((packed));

__attribute__((section(".multiboot"), aligned(8), used))
const struct multiboot2_header_textonly multiboot2_header = {
    .header = {.magic = MULTIBOOT2_MAGIC,
               .architecture = MULTIBOOT2_ARCH,
               .header_length = sizeof(struct multiboot2_header_textonly),
               .checksum =
                   (uint32_t)-(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH +
                               sizeof(struct multiboot2_header_textonly))},

    .info_req = {.type = MULTIBOOT2_TAG_INFORMATION_REQUEST,
                 .flags = 0,
                 .size = sizeof(struct multiboot2_tag_information_request),
                 .requests = {1, 2, 3, 4, 5, 6, 8, 9}},

    .end = {.type = MULTIBOOT2_TAG_END, .flags = 0, .size = 8}};
#endif
